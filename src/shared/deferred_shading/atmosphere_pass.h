// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "atmosphere_pass_settings.h"
#include "cascaded_shadow_camera_setup.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"
#include "graphics/texture.h"

#include <array>
#include <functional>

namespace mmo
{
	class Camera;

	/// @brief Height fog and shadowed light shafts over the lit opaque scene.
	/// @remark Marches the cascaded shadow maps at reduced resolution, blurs the result with a
	///         depth-aware filter, and composites it at full resolution together with the closed-form
	///         fog for the rest of each view ray. With the march disabled only the closed form runs.
	/// @remark Backend-neutral like SsaoPass; the one D3D11 dependency, the comparison sampler, is
	///         bound by the caller through a callback.
	class AtmospherePass final : public NonCopyable
	{
	public:
		/// @brief Creates the pass.
		AtmospherePass(GraphicsDevice& device, uint32 width, uint32 height);

		~AtmospherePass() override = default;

	public:
		/// @brief Records a new G-Buffer size. March targets are rebuilt lazily.
		void Resize(uint32 width, uint32 height);

		/// @brief Composites the atmosphere onto sceneColor, writing the result into output.
		/// @param camera Camera the frame is rendered with.
		/// @param sceneColor The lit opaque scene (linear HDR). Read only.
		/// @param gbufferNormalRT G-Buffer normal target (a = radial depth).
		/// @param output Full-resolution target receiving the fogged scene. Must differ from sceneColor.
		/// @param cascadeShadowMaps The cascade depth maps.
		/// @param shadowBuffer The ShadowBuffer cbuffer already filled for this frame.
		/// @param cameraBuffer The scene camera cbuffer already refreshed for this camera.
		/// @param bindShadowSampler Binds the comparison sampler at s1; called right before the march draw.
		/// @param quad The fullscreen quad vertex buffer.
		/// @param fullscreenVs The pass-through fullscreen vertex shader.
		void Render(Camera& camera, RenderTexture& sceneColor, RenderTexture& gbufferNormalRT, RenderTexture& output,
			const std::array<RenderTexturePtr, NUM_SHADOW_CASCADES>& cascadeShadowMaps, ConstantBuffer& shadowBuffer,
			ConstantBuffer& cameraBuffer, const std::function<void()>& bindShadowSampler, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Gets the mutable settings.
		[[nodiscard]] AtmospherePassSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings.
		[[nodiscard]] const AtmospherePassSettings& GetSettings() const { return m_settings; }

	private:
		/// @brief (Re)creates the march and blur targets for the current divisor and G-Buffer size.
		void EnsureTargets();

		/// @brief Releases the march and blur targets.
		void ReleaseTargets();

	private:
		GraphicsDevice& m_device;

		AtmospherePassSettings m_settings;

		uint32 m_gbufferWidth;
		uint32 m_gbufferHeight;

		uint32 m_targetWidth = 0;
		uint32 m_targetHeight = 0;
		uint32 m_targetDivisor = 0;

		RenderTexturePtr m_marchRT;
		RenderTexturePtr m_blurRT;

		/// @brief 1x1 (0, 0, 0, 1) stand-in for the march result: no in-scatter, full transmittance.
		TexturePtr m_neutralTexture;

		ConstantBufferPtr m_marchBuffer;
		ConstantBufferPtr m_blurBuffer;
		ConstantBufferPtr m_compositeBuffer;

		ShaderPtr m_marchPs;
		ShaderPtr m_blurPs;
		ShaderPtr m_compositePs;
	};
}
