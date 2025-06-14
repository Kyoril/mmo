#include "cascaded_shadow_camera_setup.h"

#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "scene_graph/light.h"
#include "math/aabb.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
    CascadedShadowCameraSetup::CascadedShadowCameraSetup(uint32 cascadeCount)
        : m_cascadeCount(std::clamp(cascadeCount, 1u, MAX_CASCADES))
        , m_splitLambda(0.5f)
        , m_cascadeOverlap(0.1f)
        , m_shadowDistanceMultiplier(3.0f)
    {
        m_cascades.resize(m_cascadeCount);
        m_cascadeSplits.resize(m_cascadeCount + 1);
    }

    void CascadedShadowCameraSetup::SetupShadowCamera(Scene& scene, Camera& camera, Light& light, Camera& shadowCamera)
    {
        // For backwards compatibility, set up only the first cascade
        // This method is called by the existing renderer
        
        if (light.GetType() != LightType::Directional)
        {
            return;
        }

        // Calculate extended shadow distance for CSM
        float shadowDistance = GetExtendedShadowDistance(camera, light);
        
        // Setup cascade splits
        CalculateCascadeSplits(camera.GetNearClipDistance(), shadowDistance);
        
        // Setup the first cascade only (for compatibility)
        SetupCascadeCamera(scene, camera, light, shadowCamera, 0);
        
        // Store this camera in the first cascade
        m_cascades[0].shadowCamera = &shadowCamera;
    }

    void CascadedShadowCameraSetup::SetupCascadedShadowCameras(Scene& scene, Camera& camera, Light& light, 
                                                               std::vector<Camera*>& shadowCameras)
    {
        if (light.GetType() != LightType::Directional)
        {
            return;
        }

        // Ensure we have enough shadow cameras
        if (shadowCameras.size() < m_cascadeCount)
        {
            ELOG("Not enough shadow cameras provided for CSM: need " << m_cascadeCount << ", got " << shadowCameras.size());
            return;
        }

        // Calculate extended shadow distance for CSM
        float shadowDistance = GetExtendedShadowDistance(camera, light);
        
        // Setup cascade splits
        CalculateCascadeSplits(camera.GetNearClipDistance(), shadowDistance);

        // Setup each cascade
        for (uint32 i = 0; i < m_cascadeCount; ++i)
        {
            SetupCascadeCamera(scene, camera, light, *shadowCameras[i], i);
            m_cascades[i].shadowCamera = shadowCameras[i];
        }
    }

    void CascadedShadowCameraSetup::SetCascadeCount(uint32 count)
    {
        m_cascadeCount = std::clamp(count, 1u, MAX_CASCADES);
        m_cascades.resize(m_cascadeCount);
        m_cascadeSplits.resize(m_cascadeCount + 1);
    }

    float CascadedShadowCameraSetup::GetExtendedShadowDistance(const Camera& camera, const Light& light) const
    {
        float shadowDist = light.GetShadowFarDistance();
        if (shadowDist <= 0.0f)
        {
            // Use camera far distance as base, but extend it for CSM
            shadowDist = camera.GetFarClipDistance() * m_shadowDistanceMultiplier;
        }
        else
        {
            // Extend the explicitly set shadow distance
            shadowDist *= m_shadowDistanceMultiplier;
        }
        
        return shadowDist;
    }

    void CascadedShadowCameraSetup::CalculateCascadeSplits(float nearDistance, float farDistance)
    {
        // Use practical split scheme (blend between uniform and logarithmic)
        // This provides good distribution of shadow quality
        
        m_cascadeSplits[0] = nearDistance;
        
        for (uint32 i = 1; i < m_cascadeCount; ++i)
        {
            float ratio = static_cast<float>(i) / static_cast<float>(m_cascadeCount);
            
            // Logarithmic split
            float logSplit = nearDistance * std::pow(farDistance / nearDistance, ratio);
            
            // Uniform split
            float uniformSplit = nearDistance + (farDistance - nearDistance) * ratio;
            
            // Blend between logarithmic and uniform
            float split = m_splitLambda * logSplit + (1.0f - m_splitLambda) * uniformSplit;
            
            m_cascadeSplits[i] = split;
        }
        
        m_cascadeSplits[m_cascadeCount] = farDistance;

        // Add overlap between cascades to reduce shadow popping
        for (uint32 i = 1; i < m_cascadeCount; ++i)
        {
            float overlap = (m_cascadeSplits[i] - m_cascadeSplits[i-1]) * m_cascadeOverlap;
            m_cascadeSplits[i] += overlap;
        }
    }

    void CascadedShadowCameraSetup::SetupCascadeCamera(Scene& scene, Camera& camera, Light& light, 
                                                       Camera& shadowCamera, uint32 cascadeIndex)
    {
        // Reset custom matrices
        shadowCamera.SetCustomViewMatrix(false);
        shadowCamera.SetCustomProjMatrix(false);
        
        // Set up for orthographic projection
        shadowCamera.SetProjectionType(ProjectionType::Orthographic);
        
        // Get cascade distances
        float nearDist = m_cascadeSplits[cascadeIndex];
        float farDist = m_cascadeSplits[cascadeIndex + 1];
        
        shadowCamera.SetNearClipDistance(0.1f);
        shadowCamera.SetFarClipDistance(farDist - nearDist + 100.0f); // Add some padding
        
        // Calculate light direction
        Vector3 lightDir = -light.GetDerivedDirection();
        lightDir.Normalize();
        
        // Calculate tight bounds for this cascade
        Vector3 cascadeCenter;
        float cascadeRadius;
        CalculateCascadeBounds(camera, cascadeIndex, lightDir, cascadeCenter, cascadeRadius);
        
        // Store cascade info
        m_cascades[cascadeIndex].nearDistance = nearDist;
        m_cascades[cascadeIndex].farDistance = farDist;
        m_cascades[cascadeIndex].center = cascadeCenter;
        m_cascades[cascadeIndex].radius = cascadeRadius;
        
        // Set tight orthographic window for this cascade
        float orthoSize = cascadeRadius * 2.0f;
        shadowCamera.SetOrthoWindow(orthoSize, orthoSize);
        
        // Position shadow camera
        float extrusionDistance = scene.GetShadowDirectionalLightExtrusionDistance();
        Vector3 shadowCamPos = cascadeCenter + lightDir * extrusionDistance;
        
        // Setup camera orientation vectors
        Vector3 up = Vector3::UnitY;
        if (std::abs(up.Dot(lightDir)) >= 0.99f)
        {
            up = Vector3::UnitZ;
        }
        
        // Build orthonormal basis
        Vector3 right = up.Cross(lightDir);
        right.Normalize();
        up = lightDir.Cross(right);
        up.Normalize();
        
        // Snap to texel boundaries for stable shadows
        float shadowMapResolution = 2048.0f; // Per cascade resolution
        float worldTexelSize = orthoSize / shadowMapResolution;
        
        // Create orientation quaternion
        Quaternion q;
        q.FromAxes(right, up, lightDir);
        
        // Transform position to light space for snapping
        Vector3 lightSpacePos = q.Inverse() * shadowCamPos;
        lightSpacePos.x = std::floor(lightSpacePos.x / worldTexelSize) * worldTexelSize;
        lightSpacePos.y = std::floor(lightSpacePos.y / worldTexelSize) * worldTexelSize;
        
        // Transform back to world space
        shadowCamPos = q * lightSpacePos;
        
        // Set camera position and orientation
        shadowCamera.GetParentNode()->SetPosition(shadowCamPos);
        shadowCamera.GetParentSceneNode()->LookAt(cascadeCenter, TransformSpace::World, Vector3::UnitZ);
        
        // Update matrices
        shadowCamera.InvalidateView();
        shadowCamera.InvalidateFrustum();
        
        // Store view-projection matrix for shaders
        m_cascades[cascadeIndex].viewProjectionMatrix = 
            shadowCamera.GetProjectionMatrix() * shadowCamera.GetViewMatrix();
    }

    void CascadedShadowCameraSetup::CalculateCascadeBounds(const Camera& camera, uint32 cascadeIndex, 
                                                           const Vector3& lightDirection, Vector3& center, float& radius)
    {
        // Get cascade distances
        float nearDist = m_cascadeSplits[cascadeIndex];
        float farDist = m_cascadeSplits[cascadeIndex + 1];
        
        // Calculate frustum corners for this cascade
        Vector3 corners[8];
        CalculateFrustumCorners(camera, nearDist, farDist, corners);
        
        // Find bounding sphere of frustum corners
        center = Vector3::Zero;
        for (int i = 0; i < 8; ++i)
        {
            center += corners[i];
        }
        center /= 8.0f;
        
        // Find radius
        radius = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            float dist = (corners[i] - center).GetLength();
            radius = std::max(radius, dist);
        }
        
        // Add some padding to avoid edge artifacts
        radius *= 1.1f;
    }

    void CascadedShadowCameraSetup::CalculateFrustumCorners(const Camera& camera, float nearDist, float farDist,
                                                            Vector3 corners[8]) const
    {
        // Get camera properties
        const Vector3 cameraPos = camera.GetDerivedPosition();
        const Vector3 cameraDir = camera.GetDerivedDirection();
        const Quaternion cameraRot = camera.GetDerivedOrientation();
        
        // Calculate up and right vectors
        Vector3 up = cameraRot * Vector3::UnitY;
        Vector3 right = cameraRot * Vector3::UnitX;
        
        float fov = camera.GetFOVy().GetValueRadians();
        float aspect = camera.GetAspectRatio();
        
        // Calculate frustum dimensions at near and far planes
        float nearHeight = 2.0f * std::tan(fov * 0.5f) * nearDist;
        float nearWidth = nearHeight * aspect;
        float farHeight = 2.0f * std::tan(fov * 0.5f) * farDist;
        float farWidth = farHeight * aspect;
        
        // Near plane center
        Vector3 nearCenter = cameraPos + cameraDir * nearDist;
        Vector3 farCenter = cameraPos + cameraDir * farDist;
        
        // Calculate 8 frustum corners
        // Near plane (0-3)
        corners[0] = nearCenter - right * (nearWidth * 0.5f) - up * (nearHeight * 0.5f); // bottom-left
        corners[1] = nearCenter + right * (nearWidth * 0.5f) - up * (nearHeight * 0.5f); // bottom-right
        corners[2] = nearCenter + right * (nearWidth * 0.5f) + up * (nearHeight * 0.5f); // top-right
        corners[3] = nearCenter - right * (nearWidth * 0.5f) + up * (nearHeight * 0.5f); // top-left
        
        // Far plane (4-7)
        corners[4] = farCenter - right * (farWidth * 0.5f) - up * (farHeight * 0.5f); // bottom-left
        corners[5] = farCenter + right * (farWidth * 0.5f) - up * (farHeight * 0.5f); // bottom-right
        corners[6] = farCenter + right * (farWidth * 0.5f) + up * (farHeight * 0.5f); // top-right
        corners[7] = farCenter - right * (farWidth * 0.5f) + up * (farHeight * 0.5f); // top-left
    }
}
