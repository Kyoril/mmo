// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "cascaded_shadow_camera_setup.h"
#include "volumetric_fog_settings.h"

#include "base/non_copyable.h"
#include "graphics/constant_buffer.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/sampler_state.h"
#include "graphics/shader_base.h"
#include "graphics/vertex_buffer.h"
#include "graphics/volume_texture.h"
#include "math/matrix4.h"
#include "math/vector3.h"
#include "scene_graph/wind_state.h"

#include <array>

namespace mmo
{
	class Camera;

	/// @brief Froxel volumetric fog over the lit opaque scene.
	/// @remark Three compute steps fill a camera-aligned 3D grid (inject: height fog x wind noise and
	///         shadowed sun light; temporal: reprojected history blend; integrate: front-to-back
	///         accumulation, written back into the inject volume since its only reader - the temporal
	///         step - has already run by then), then a full-screen composite applies it and continues
	///         with the closed-form fog beyond the grid. With the volume disabled (quality 0), or if the
	///         grid volumes failed to allocate, only the closed form runs.
	/// @remark Uses only GraphicsDevice abstractions. The compute steps stay disabled on backends without
	///         volume textures or compute shaders, and the composite pixel shader bytecode currently exists
	///         only for D3D11 - the same seam as the other deferred passes.
	class VolumetricFogPass final : public NonCopyable
	{
	public:
		/// @brief Creates the pass and uploads the noise volume.
		VolumetricFogPass(GraphicsDevice& device, uint32 width, uint32 height);

		~VolumetricFogPass() override = default;

	public:
		/// @brief Records a new render size. Grid volumes are rebuilt lazily.
		void Resize(uint32 width, uint32 height);

		/// @brief Composites the fog onto sceneColor, writing the result into output.
		/// @param camera Camera the frame is rendered with.
		/// @param wind The frame's wind (noise scroll, size and amount).
		/// @param sceneColor The lit opaque scene (linear HDR). Read only.
		/// @param gbufferNormalRT G-Buffer normal target (a = radial depth).
		/// @param output Full-resolution target receiving the fogged scene. Must differ from sceneColor.
		/// @param cascadeShadowMaps The cascade depth maps.
		/// @param shadowBuffer The ShadowBuffer cbuffer already filled for this frame.
		/// @param cameraBuffer The scene camera cbuffer already refreshed for this camera.
		/// @param shadowSampler The cascade comparison sampler.
		/// @param quad The fullscreen quad vertex buffer.
		/// @param fullscreenVs The pass-through fullscreen vertex shader.
		void Render(Camera& camera, const WindState& wind, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output,
			const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>& cascadeShadowMaps, ConstantBuffer& shadowBuffer,
			ConstantBuffer& cameraBuffer, SamplerState& shadowSampler, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Discards the temporal history, e.g. after a frame in which the pass did not run.
		void InvalidateHistory() { m_historyValid = false; }

		/// @brief Gets the mutable settings.
		[[nodiscard]] VolumetricFogSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings.
		[[nodiscard]] const VolumetricFogSettings& GetSettings() const { return m_settings; }

	private:
		/// @brief (Re)creates the grid volumes for the current preset and render size.
		void EnsureVolumes();

		/// @brief Releases the grid volumes.
		void ReleaseVolumes();

		/// @brief Whether this backend can run the froxel volume at all.
		[[nodiscard]] bool SupportsVolume() const;

	private:
		GraphicsDevice& m_device;
		VolumetricFogSettings m_settings;

		uint32 m_width;
		uint32 m_height;

		uint32 m_gridWidth = 0;
		uint32 m_gridHeight = 0;
		uint32 m_gridDepth = 0;

		VolumeTexturePtr m_noiseVolume;

		/// @brief Inject step's output. The temporal step is its only reader, so once that step has run
		///        this frame the integrate step reuses it as its own output volume instead of a fourth
		///        allocation.
		VolumeTexturePtr m_injectVolume;
		std::array<VolumeTexturePtr, 2> m_historyVolumes;

		/// @brief Index into m_historyVolumes written this frame; the other one holds last frame.
		uint32 m_historyIndex = 0;
		bool m_historyValid = false;
		uint64 m_frameIndex = 0;

		/// @brief Set once EnsureVolumes has logged a grid-volume allocation failure, so it only warns once.
		bool m_volumeAllocationFailureLogged = false;

		Matrix4 m_prevViewProj;
		Vector3 m_prevCameraPosition;
		Vector3 m_prevCameraForward;
		float m_prevRange = 0.0f;

		ConstantBufferPtr m_fogBuffer;

		ShaderPtr m_injectCs;
		ShaderPtr m_temporalCs;
		ShaderPtr m_integrateCs;
		ShaderPtr m_compositePs;

		SamplerStatePtr m_linearClampSampler;
		SamplerStatePtr m_noiseSampler;
	};
}
