// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cascaded_shadow_camera_setup.h"

#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "scene_graph/light.h"
#include "graphics/graphics_device.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	CascadedShadowCameraSetup::CascadedShadowCameraSetup()
	{
		m_splitDistances.fill(0.0f);
		for (auto& cascade : m_cascades)
		{
			cascade.viewProjection = Matrix4::Identity;
			cascade.splitDistance = 0.0f;
			cascade.worldTexelSize = 0.0f;
		}
	}

	void CascadedShadowCameraSetup::SetupShadowCamera(Scene& scene, Camera& camera, Light& light, Camera& shadowCamera)
	{
		// This is called for single shadow camera setup - we'll use the first cascade
		std::array<Camera*, NUM_SHADOW_CASCADES> cameras = { &shadowCamera, nullptr, nullptr, nullptr };
		SetupCascades(scene, camera, light, cameras);
	}

	void CascadedShadowCameraSetup::SetupCascades(
		Scene& scene,
		Camera& camera,
		Light& light,
		std::array<Camera*, NUM_SHADOW_CASCADES>& shadowCameras)
	{
		if (light.GetType() != LightType::Directional)
		{
			return;
		}

		const float nearClip = camera.GetNearClipDistance();
		const float farClip = std::min(camera.GetFarClipDistance(), m_config.maxShadowDistance);

		// Calculate cascade split distances
		CalculateSplitDistances(nearClip, farClip);

		// Get light direction
		Vector3 lightDir = light.GetDerivedDirection();
		lightDir.Normalize();

		// Setup each cascade
		for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
		{
			if (!shadowCameras[i])
			{
				continue;
			}

			Camera& shadowCam = *shadowCameras[i];

			// Reset custom matrices
			shadowCam.SetCustomViewMatrix(false);
			shadowCam.SetCustomProjMatrix(false);
			shadowCam.SetProjectionType(ProjectionType::Orthographic);

			// Get frustum corners for this cascade
			std::array<Vector3, 8> frustumCorners;
			GetFrustumCornersWorldSpace(camera, m_splitDistances[i], m_splitDistances[i + 1], frustumCorners);

			// Compute the cascade view-projection matrix
			Matrix4 viewProj;
			float worldTexelSize;
			ComputeCascadeMatrix(lightDir, frustumCorners, i, viewProj, worldTexelSize);

			// Store cascade data
			m_cascades[i].viewProjection = viewProj;
			m_cascades[i].splitDistance = m_splitDistances[i + 1];
			m_cascades[i].worldTexelSize = worldTexelSize;

			// Set up the shadow camera
			// Extract the view matrix from the combined view-projection (we need separate matrices)
			// For shadow rendering, we'll use a custom view-projection matrix

			// Calculate frustum center for camera position
			Vector3 frustumCenter(0.0f, 0.0f, 0.0f);
			for (const auto& corner : frustumCorners)
			{
				frustumCenter += corner;
			}
			frustumCenter /= 8.0f;

			// Build look-at vectors
			Vector3 up = Vector3::UnitY;
			if (std::abs(up.Dot(lightDir)) >= 0.99f)
			{
				up = Vector3::UnitZ;
			}

			Vector3 right = up.Cross(lightDir);
			right.Normalize();
			up = lightDir.Cross(right);
			up.Normalize();

			// Calculate the bounding sphere radius for the cascade
			float radius = 0.0f;
			for (const auto& corner : frustumCorners)
			{
				float distance = (corner - frustumCenter).GetLength();
				radius = std::max(radius, distance);
			}

			// Round radius to prevent shadow flickering
			if (m_config.stableCascades)
			{
				// Round up to the nearest texel
				const float texelSize = (radius * 2.0f) / static_cast<float>(m_config.shadowMapSize);
				radius = std::ceil(radius / texelSize) * texelSize;
			}

			// Position the shadow camera
			// Use a larger extrusion distance based on the cascade size to capture shadow casters
			const float baseExtrusionDistance = scene.GetShadowDirectionalLightExtrusionDistance();
			const float extrusionDistance = std::max(baseExtrusionDistance, radius * 2.0f + 100.0f);
			Vector3 shadowCamPos = frustumCenter - lightDir * extrusionDistance;

			// Snap to texel grid to prevent shadow swimming
			if (m_config.stableCascades)
			{
				// Create a light-space transform
				Quaternion lightRotation;
				lightRotation.FromAxes(right, up, lightDir);

				// Transform position to light space
				Vector3 lightSpacePos = lightRotation.Inverse() * shadowCamPos;

				// Snap to texel grid
				lightSpacePos.x = std::floor(lightSpacePos.x / worldTexelSize) * worldTexelSize;
				lightSpacePos.y = std::floor(lightSpacePos.y / worldTexelSize) * worldTexelSize;

				// Transform back to world space
				shadowCamPos = lightRotation * lightSpacePos;
			}

			// Set camera parameters
			shadowCam.GetParentNode()->SetPosition(shadowCamPos);
			shadowCam.GetParentSceneNode()->LookAt(frustumCenter, TransformSpace::World, Vector3::UnitZ);

			// Set orthographic projection to exactly fit the cascade
			shadowCam.SetOrthoWindow(radius * 2.0f, radius * 2.0f);
			shadowCam.SetNearClipDistance(0.1f);
			shadowCam.SetFarClipDistance(extrusionDistance * 2.0f + radius);

			shadowCam.InvalidateView();
			shadowCam.InvalidateFrustum();
		}
	}

	void CascadedShadowCameraSetup::CalculateSplitDistances(float nearClip, float farClip)
	{
		// Check if manual splits are provided
		bool useManualSplits = false;
		for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
		{
			if (m_config.cascadeSplits[i] > 0.0f)
			{
				useManualSplits = true;
				break;
			}
		}

		m_splitDistances[0] = nearClip;

		if (useManualSplits)
		{
			// Use manual split distances
			for (uint32 i = 0; i < NUM_SHADOW_CASCADES; ++i)
			{
				m_splitDistances[i + 1] = m_config.cascadeSplits[i];
			}
		}
		else
		{
			// Calculate split distances using practical split scheme (mix of uniform and logarithmic)
			for (uint32 i = 1; i <= NUM_SHADOW_CASCADES; ++i)
			{
				const float p = static_cast<float>(i) / static_cast<float>(NUM_SHADOW_CASCADES);

				// Logarithmic split
				const float logSplit = nearClip * std::pow(farClip / nearClip, p);

				// Uniform split
				const float uniformSplit = nearClip + (farClip - nearClip) * p;

				// Blend between logarithmic and uniform using lambda
				m_splitDistances[i] = m_config.splitLambda * logSplit + (1.0f - m_config.splitLambda) * uniformSplit;
			}
		}

		// Ensure the last split exactly matches the far clip
		m_splitDistances[NUM_SHADOW_CASCADES] = farClip;
	}

	void CascadedShadowCameraSetup::GetFrustumCornersWorldSpace(
		const Camera& camera,
		float nearDist,
		float farDist,
		std::array<Vector3, 8>& outCorners) const
	{
		const Matrix4& viewMatrix = camera.GetViewMatrix();
		const float fov = camera.GetFOVy().GetValueRadians();
		const float aspectRatio = camera.GetAspectRatio();

		const float tanHalfFOV = std::tan(fov * 0.5f);

		// Calculate near plane dimensions
		const float nearHeight = 2.0f * tanHalfFOV * nearDist;
		const float nearWidth = nearHeight * aspectRatio;

		// Calculate far plane dimensions
		const float farHeight = 2.0f * tanHalfFOV * farDist;
		const float farWidth = farHeight * aspectRatio;

		// Get camera transform
		const Vector3 camPos = camera.GetDerivedPosition();
		const Vector3 camDir = camera.GetDerivedDirection();
		const Quaternion camOrientation = camera.GetDerivedOrientation();
		const Vector3 camUp = camOrientation * Vector3::UnitY;
		const Vector3 camRight = camOrientation * Vector3::UnitX;

		// Calculate center points of near and far planes
		const Vector3 nearCenter = camPos + camDir * nearDist;
		const Vector3 farCenter = camPos + camDir * farDist;

		// Calculate frustum corners
		// Near plane corners (0-3)
		outCorners[0] = nearCenter - camUp * (nearHeight * 0.5f) - camRight * (nearWidth * 0.5f); // Bottom-left
		outCorners[1] = nearCenter - camUp * (nearHeight * 0.5f) + camRight * (nearWidth * 0.5f); // Bottom-right
		outCorners[2] = nearCenter + camUp * (nearHeight * 0.5f) + camRight * (nearWidth * 0.5f); // Top-right
		outCorners[3] = nearCenter + camUp * (nearHeight * 0.5f) - camRight * (nearWidth * 0.5f); // Top-left

		// Far plane corners (4-7)
		outCorners[4] = farCenter - camUp * (farHeight * 0.5f) - camRight * (farWidth * 0.5f); // Bottom-left
		outCorners[5] = farCenter - camUp * (farHeight * 0.5f) + camRight * (farWidth * 0.5f); // Bottom-right
		outCorners[6] = farCenter + camUp * (farHeight * 0.5f) + camRight * (farWidth * 0.5f); // Top-right
		outCorners[7] = farCenter + camUp * (farHeight * 0.5f) - camRight * (farWidth * 0.5f); // Top-left
	}

	void CascadedShadowCameraSetup::ComputeCascadeMatrix(
		const Vector3& lightDir,
		const std::array<Vector3, 8>& frustumCorners,
		uint32 cascadeIndex,
		Matrix4& outViewProj,
		float& outWorldTexelSize) const
	{
		// Calculate frustum center
		Vector3 frustumCenter(0.0f, 0.0f, 0.0f);
		for (const auto& corner : frustumCorners)
		{
			frustumCenter += corner;
		}
		frustumCenter /= 8.0f;

		// Build light-space basis vectors
		Vector3 up = Vector3::UnitY;
		if (std::abs(up.Dot(lightDir)) >= 0.99f)
		{
			up = Vector3::UnitZ;
		}

		Vector3 right = up.Cross(lightDir);
		right.Normalize();
		up = lightDir.Cross(right);
		up.Normalize();

		// Create light view matrix
		// The light looks along its direction (into the scene)
		const Vector3 lightPos = frustumCenter - lightDir * 100.0f; // Back the light up

		Matrix4 lightView = Matrix4::Identity;
		lightView[0][0] = right.x;
		lightView[1][0] = right.y;
		lightView[2][0] = right.z;
		lightView[3][0] = -right.Dot(lightPos);

		lightView[0][1] = up.x;
		lightView[1][1] = up.y;
		lightView[2][1] = up.z;
		lightView[3][1] = -up.Dot(lightPos);

		lightView[0][2] = lightDir.x;
		lightView[1][2] = lightDir.y;
		lightView[2][2] = lightDir.z;
		lightView[3][2] = -lightDir.Dot(lightPos);

		// Transform frustum corners to light space and find bounds
		float minX = std::numeric_limits<float>::max();
		float maxX = std::numeric_limits<float>::lowest();
		float minY = std::numeric_limits<float>::max();
		float maxY = std::numeric_limits<float>::lowest();
		float minZ = std::numeric_limits<float>::max();
		float maxZ = std::numeric_limits<float>::lowest();

		for (const auto& corner : frustumCorners)
		{
			Vector4 lightSpaceCorner = lightView * Vector4(corner, 1.0f);

			minX = std::min(minX, lightSpaceCorner.x);
			maxX = std::max(maxX, lightSpaceCorner.x);
			minY = std::min(minY, lightSpaceCorner.y);
			maxY = std::max(maxY, lightSpaceCorner.y);
			minZ = std::min(minZ, lightSpaceCorner.z);
			maxZ = std::max(maxZ, lightSpaceCorner.z);
		}

		// Expand Z range to capture shadow casters behind the camera
		const float zExpansion = 100.0f;
		minZ -= zExpansion;

		// Calculate world texel size for stable cascades
		const float cascadeWidth = maxX - minX;
		const float cascadeHeight = maxY - minY;
		outWorldTexelSize = std::max(cascadeWidth, cascadeHeight) / static_cast<float>(m_config.shadowMapSize);

		// Snap to texel boundaries to prevent shadow swimming
		if (m_config.stableCascades)
		{
			// Use bounding sphere approach for more stable shadows
			float radius = 0.0f;
			for (const auto& corner : frustumCorners)
			{
				float distance = (corner - frustumCenter).GetLength();
				radius = std::max(radius, distance);
			}

			// Round up to prevent clipping
			radius = std::ceil(radius);

			// Use sphere-based bounds for stability
			minX = -radius;
			maxX = radius;
			minY = -radius;
			maxY = radius;

			// Recalculate texel size with sphere bounds
			outWorldTexelSize = (radius * 2.0f) / static_cast<float>(m_config.shadowMapSize);

			// Snap bounds to texel grid
			minX = std::floor(minX / outWorldTexelSize) * outWorldTexelSize;
			maxX = std::floor(maxX / outWorldTexelSize) * outWorldTexelSize;
			minY = std::floor(minY / outWorldTexelSize) * outWorldTexelSize;
			maxY = std::floor(maxY / outWorldTexelSize) * outWorldTexelSize;
		}

		// Create orthographic projection matrix (DirectX-style, Z in [0,1])
		Matrix4 lightProj = Matrix4::Identity;

		const float width = maxX - minX;
		const float height = maxY - minY;
		const float depth = maxZ - minZ;

		lightProj[0][0] = 2.0f / width;
		lightProj[1][1] = 2.0f / height;
		lightProj[2][2] = 1.0f / depth;
		lightProj[3][0] = -(maxX + minX) / width;
		lightProj[3][1] = -(maxY + minY) / height;
		lightProj[3][2] = -minZ / depth;

		outViewProj = lightProj * lightView;
	}
}
