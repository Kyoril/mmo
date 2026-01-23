// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shadow_camera_setup.h"
#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/aabb.h"

#include <array>

namespace mmo
{
	/// @brief Number of shadow map cascades for CSM.
	static constexpr uint32 NUM_SHADOW_CASCADES = 4;

	/// @brief Contains the data for a single shadow cascade.
	struct ShadowCascade
	{
		/// @brief The view-projection matrix for this cascade.
		Matrix4 viewProjection;

		/// @brief The split distance for this cascade (in view space).
		float splitDistance = 0.0f;

		/// @brief The world space size of the cascade (for texel snapping).
		float worldTexelSize = 0.0f;
	};

	/// @brief Parameters for configuring the cascaded shadow map system.
	struct CascadedShadowConfig
	{
		/// @brief Split scheme lambda (0 = uniform, 1 = logarithmic).
		float splitLambda = 0.92f;

		/// @brief Maximum shadow distance.
		float maxShadowDistance = 300.0f;

		/// @brief Size of each cascade shadow map.
		uint16 shadowMapSize = 2048;

		/// @brief Whether to use stable cascades (prevents shadow swimming).
		bool stableCascades = true;

		/// @brief Cascade split distances (auto-calculated if empty, or can be manually set).
		std::array<float, NUM_SHADOW_CASCADES> cascadeSplits = { 0.0f, 0.0f, 0.0f, 0.0f };

		/// @brief Blend factor for cascade transitions (0 = hard transition, 1 = full blend).
		float cascadeBlendFactor = 0.1f;
	};

	/// @brief Advanced shadow camera setup implementing Cascaded Shadow Maps (CSM).
	/// 
	/// This provides UE-quality shadows by using multiple shadow maps at different
	/// resolutions to cover different distance ranges from the camera.
	class CascadedShadowCameraSetup : public ShadowCameraSetup
	{
	public:
		CascadedShadowCameraSetup();
		~CascadedShadowCameraSetup() override = default;

	public:
		/// @copydoc ShadowCameraSetup::SetupShadowCamera
		void SetupShadowCamera(Scene& scene, Camera& camera, Light& light, Camera& shadowCamera) override;

		/// @brief Sets up shadow cameras for all cascades.
		/// @param scene The scene.
		/// @param camera The main view camera.
		/// @param light The directional light casting shadows.
		/// @param shadowCameras Array of shadow cameras (one per cascade).
		void SetupCascades(Scene& scene, Camera& camera, Light& light, std::array<Camera*, NUM_SHADOW_CASCADES>& shadowCameras);

		/// @brief Gets the cascade data for shader consumption.
		[[nodiscard]] const std::array<ShadowCascade, NUM_SHADOW_CASCADES>& GetCascades() const { return m_cascades; }

		/// @brief Gets the configuration.
		[[nodiscard]] CascadedShadowConfig& GetConfig() { return m_config; }

		/// @brief Gets the configuration (const).
		[[nodiscard]] const CascadedShadowConfig& GetConfig() const { return m_config; }

		/// @brief Sets the configuration.
		void SetConfig(const CascadedShadowConfig& config) { m_config = config; }

	private:
		/// @brief Calculates the cascade split distances.
		void CalculateSplitDistances(float nearClip, float farClip);

		/// @brief Computes the frustum corners for a cascade in world space.
		/// @param camera The view camera.
		/// @param nearDist Near distance for this split.
		/// @param farDist Far distance for this split.
		/// @param outCorners Array of 8 corners (near plane 0-3, far plane 4-7).
		void GetFrustumCornersWorldSpace(
			const Camera& camera,
			float nearDist,
			float farDist,
			std::array<Vector3, 8>& outCorners) const;

		/// @brief Computes a tight orthographic projection for a cascade.
		/// @param lightDir The light direction.
		/// @param frustumCorners The frustum corners in world space.
		/// @param cascadeIndex The cascade index (for stable snapping).
		/// @param outViewProj The resulting view-projection matrix.
		/// @param outWorldTexelSize The world-space texel size for this cascade.
		void ComputeCascadeMatrix(
			const Vector3& lightDir,
			const std::array<Vector3, 8>& frustumCorners,
			uint32 cascadeIndex,
			Matrix4& outViewProj,
			float& outWorldTexelSize) const;

	private:
		CascadedShadowConfig m_config;
		std::array<ShadowCascade, NUM_SHADOW_CASCADES> m_cascades;
		std::array<float, NUM_SHADOW_CASCADES + 1> m_splitDistances;
	};
}
