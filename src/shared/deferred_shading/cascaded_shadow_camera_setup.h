// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shadow_camera_setup.h"
#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/aabb.h"

#include <algorithm>
#include <array>
#include <cmath>

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

		/// @brief Quantization step for the shadow light direction, in degrees. A continuously moving
		///        sun (day/night cycle) re-rasterizes every shadow map each frame, which texel snapping
		///        cannot stabilize (snapping fixes translation, not rotation) — distant cascades shimmer
		///        badly. With quantization the shadow system keeps using the last direction until the
		///        real light has moved more than this angle, so cascade matrices are bit-identical
		///        between steps and static geometry is perfectly stable. Set to 0 to disable and track
		///        the light continuously. Lighting itself is unaffected — only shadows step.
		float lightStepDegrees = 0.35f;

		/// @brief Number of active cascades to render (1..NUM_SHADOW_CASCADES). Rendering fewer
		///        cascades means fewer full-scene shadow passes — a large saving on lower-end GPUs —
		///        at the cost of near-camera shadow resolution. The split scheme is computed over
		///        exactly this many cascades, so the total shadow distance is preserved.
		uint32 activeCascadeCount = NUM_SHADOW_CASCADES;

		/// @brief Returns the active cascade count clamped to the valid [1, NUM_SHADOW_CASCADES] range.
		[[nodiscard]] uint32 GetActiveCascadeCount() const
		{
			return activeCascadeCount < 1u ? 1u :
				(activeCascadeCount > NUM_SHADOW_CASCADES ? NUM_SHADOW_CASCADES : activeCascadeCount);
		}

		/// @brief Number of near cascades rendered at the full shadowMapSize; the rest use half of it
		///        (shadow quality setting).
		uint32 fullResolutionCascades = 2;

		/// @brief Height of the tallest shadow caster that must still shade a cascade from outside it
		///        (trees, buildings), in world units.
		float casterHeight = 50.0f;

		/// @brief Upper bound for GetLightwardReach, in world units.
		float maxLightwardReach = 800.0f;

		/// @brief How far toward the light a cascade must reach beyond its bounding sphere so a caster
		///        casterHeight tall can still shade it: casterHeight / tan(sun elevation), clamped to
		///        [0, maxLightwardReach]. A low sun throws a tree's shadow hundreds of metres.
		/// @param lightDir Normalised direction the light travels (y < 0 for a sun above the horizon).
		[[nodiscard]] float GetLightwardReach(const Vector3& lightDir) const
		{
			const float sinElevation = -lightDir.y;
			if (sinElevation <= 0.0f)
			{
				return maxLightwardReach;
			}

			const float cosElevation = std::sqrt(std::max(0.0f, 1.0f - sinElevation * sinElevation));
			return std::min(casterHeight * cosElevation / sinElevation, maxLightwardReach);
		}
		/// @brief Edge length in texels of the depth map a cascade is rendered to. Cascades past
		///        fullResolutionCascades use half of shadowMapSize (at least 256).
		[[nodiscard]] uint16 GetCascadeMapSize(const uint32 cascadeIndex) const
		{
			if (cascadeIndex < fullResolutionCascades)
			{
				return shadowMapSize;
			}

			const uint16 halfSize = shadowMapSize / 2;
			return halfSize < 256 ? 256 : halfSize;
		}

		/// @brief World size of one texel of a cascade rendered with the given ortho window size. The
		///        texel snap must use exactly this: snapping a half-resolution cascade on the full-size
		///        grid moves its window by half a texel as the camera moves, which re-rasterizes every
		///        shadow edge at a new offset (thin light gaps open and close; god rays flicker).
		[[nodiscard]] float GetCascadeTexelSize(const float windowSize, const uint32 cascadeIndex) const
		{
			return windowSize / static_cast<float>(GetCascadeMapSize(cascadeIndex));
		}
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

		/// @brief Feeds the current world light direction into the quantizer and returns whether the
		///        quantized direction the cascades use actually changed. Callers that stagger cascade
		///        updates should force a full refresh on frames where this returns true, so the whole
		///        shadow picture steps coherently in one frame instead of cascade-by-cascade.
		/// @param worldLightDir The light's current (continuous) world-space direction.
		/// @return true if the quantized direction stepped this call. Idempotent within a frame: a
		///         second call with the same input returns false.
		bool UpdateLightDirection(const Vector3& worldLightDir);

		/// @brief Sets up shadow cameras for all cascades.
		/// @param scene The scene.
		/// @param camera The main view camera.
		/// @param light The directional light casting shadows.
		/// @param shadowCameras Array of shadow cameras (one per cascade).
		/// @param updateMask Bit i set => (re)fit cascade i this frame. Cascades whose bit is clear keep
		///        their previous camera matrix untouched so the renderer can reuse last frame's shadow
		///        map for them (temporal cascade staggering). Split distances are always refreshed.
		void SetupCascades(Scene& scene, Camera& camera, Light& light, std::array<Camera*, NUM_SHADOW_CASCADES>& shadowCameras, uint32 updateMask = 0xFFFFFFFFu);

		/// @brief Fits the volumetric fog's own shadow camera around the view frustum from the near plane to
		///        range, snapped and extended toward the light exactly like a cascade. Call after SetupCascades
		///        (it uses the light direction quantized there).
		/// @return World size of one texel of the fog shadow map.
		float SetupFogShadowCamera(Scene& scene, Camera& camera, float range, uint16 mapSize, Camera& fogShadowCamera);

		/// @brief Gets the cascade data for shader consumption.
		[[nodiscard]] const std::array<ShadowCascade, NUM_SHADOW_CASCADES>& GetCascades() const { return m_cascades; }

		/// @brief Gets the configuration.
		[[nodiscard]] CascadedShadowConfig& GetConfig() { return m_config; }

		/// @brief Gets the configuration (const).
		[[nodiscard]] const CascadedShadowConfig& GetConfig() const { return m_config; }

		/// @brief Sets the configuration.
		void SetConfig(const CascadedShadowConfig& config) { m_config = config; }

	private:
		/// @brief Places an orthographic shadow camera around a frustum section: bounding sphere with a
		///        quantized radius, texel snap on the grid of a mapSize map, near plane GetLightwardReach toward
		///        the light. Returns the world texel size.
		float PlaceShadowCamera(Scene& scene, const std::array<Vector3, 8>& frustumCorners, const Vector3& lightDir, uint16 mapSize, Camera& shadowCam) const;

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

		/// @brief The quantized light direction the cascades are currently built from.
		Vector3 m_quantizedLightDir = Vector3::Zero;

		/// @brief Whether m_quantizedLightDir holds a valid direction yet.
		bool m_hasQuantizedLightDir = false;
	};
}
