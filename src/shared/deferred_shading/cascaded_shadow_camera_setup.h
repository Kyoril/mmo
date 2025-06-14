#pragma once

#include "shadow_camera_setup.h"
#include "math/matrix4.h"
#include "base/typedefs.h"
#include <vector>

namespace mmo
{
    /// @brief Cascaded Shadow Maps camera setup for directional lights.
    /// This setup creates multiple shadow cameras at different distances to provide
    /// high-quality shadows both near and far from the camera.
    class CascadedShadowCameraSetup : public ShadowCameraSetup
    {
    public:
        static constexpr uint32 MAX_CASCADES = 4;

        struct CascadeInfo
        {
            float nearDistance;
            float farDistance;
            Matrix4 viewProjectionMatrix;
            Camera* shadowCamera;
            Vector3 center;
            float radius;
        };

    public:
        CascadedShadowCameraSetup(uint32 cascadeCount = 3);
        ~CascadedShadowCameraSetup() override = default;

        /// @brief Set up all cascade shadow cameras for a directional light
        void SetupShadowCamera(Scene& scene, Camera& camera, Light& light, Camera& shadowCamera) override;

        /// @brief Set up all cascade shadow cameras (new method for CSM)
        void SetupCascadedShadowCameras(Scene& scene, Camera& camera, Light& light, 
                                       std::vector<Camera*>& shadowCameras);

        /// @brief Get cascade information for shaders
        const std::vector<CascadeInfo>& GetCascades() const { return m_cascades; }

        /// @brief Get cascade split distances
        const std::vector<float>& GetCascadeSplits() const { return m_cascadeSplits; }

        /// @brief Set the number of cascades (1-4)
        void SetCascadeCount(uint32 count);

        /// @brief Set lambda for logarithmic/uniform split blending (0=uniform, 1=logarithmic)
        void SetSplitLambda(float lambda) { m_splitLambda = lambda; }

        /// @brief Set cascade overlap factor to reduce shadow popping
        void SetCascadeOverlap(float overlap) { m_cascadeOverlap = overlap; }

        /// @brief Set shadow distance multiplier for extending shadow range
        void SetShadowDistanceMultiplier(float multiplier) { m_shadowDistanceMultiplier = multiplier; }

        /// @brief Get split lambda value
        float GetSplitLambda() const { return m_splitLambda; }

        /// @brief Get extended shadow distance for CSM
        float GetExtendedShadowDistance(const Camera& camera, const Light& light) const;

    private:
        /// @brief Calculate cascade split distances using practical split scheme
        void CalculateCascadeSplits(float nearDistance, float farDistance);

        /// @brief Setup a single cascade shadow camera
        void SetupCascadeCamera(Scene& scene, Camera& camera, Light& light, 
                               Camera& shadowCamera, uint32 cascadeIndex);

        /// @brief Calculate tight AABB for cascade frustum
        void CalculateCascadeBounds(const Camera& camera, uint32 cascadeIndex, 
                                   const Vector3& lightDirection, Vector3& center, float& radius);

        /// @brief Calculate view frustum corners for a specific depth range
        void CalculateFrustumCorners(const Camera& camera, float nearDist, float farDist,
                                    Vector3 corners[8]) const;

    private:
        uint32 m_cascadeCount;
        float m_splitLambda;        // Blend between uniform and logarithmic splits
        float m_cascadeOverlap;     // Overlap between cascades to reduce popping
        float m_shadowDistanceMultiplier; // Multiplier for extending shadow distance

        std::vector<CascadeInfo> m_cascades;
        std::vector<float> m_cascadeSplits;
    };
}
