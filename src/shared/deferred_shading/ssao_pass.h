// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "ssao_settings.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"

namespace mmo
{
	class Camera;

	/// @brief Screen-space ambient occlusion pass using the visibility-bitmask occlusion core of
	///        SSILVB (Therrien, Levesque & Gilet, 2023).
	/// @remark Runs between the geometry and lighting passes. Reads only the G-Buffer normal
	///         target, which packs the world normal in rgb and linear radial depth in a.
	/// @remark Deliberately backend-neutral: this class talks only to GraphicsDevice abstractions
	///         and contains no #ifdef and no D3D11 references. The one backend-bound fact — which
	///         shader bytecode blob to use — is isolated in ssao_pass.cpp.
	class SsaoPass final : public NonCopyable
	{
	public:
		/// @brief Creates the SSAO pass.
		/// @param device The graphics device to use.
		/// @param width Width of the G-Buffer.
		/// @param height Height of the G-Buffer.
		SsaoPass(GraphicsDevice& device, uint32 width, uint32 height);

		~SsaoPass() override = default;

	public:
		/// @brief Resizes the AO targets to match a new G-Buffer size.
		void Resize(uint32 width, uint32 height);

		/// @brief Computes the AO term for the current frame.
		/// @param camera Camera the frame is rendered with.
		/// @param gbufferNormalRT The G-Buffer normal target (rgb = normal, a = radial depth).
		/// @param quad The fullscreen quad vertex buffer.
		/// @param fullscreenVs The pass-through fullscreen vertex shader.
		/// @remark Does nothing when settings.enabled is false; GetResult() then yields white.
		void Render(Camera& camera, RenderTexture& gbufferNormalRT, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Gets the AO texture for the lighting pass to sample.
		/// @return The blurred AO texture, or a 1x1 white texture when SSAO is disabled.
		[[nodiscard]] TexturePtr GetResult() const;

		/// @brief Gets the mutable settings for this pass.
		[[nodiscard]] SsaoSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings for this pass.
		[[nodiscard]] const SsaoSettings& GetSettings() const { return m_settings; }

	private:
		/// @brief (Re)creates the AO render targets at the resolution implied by the current
		///        G-Buffer size and the halfResolution setting.
		void EnsureTargets();

		/// @brief Releases the AO render targets. Called when SSAO is disabled so a disabled
		///        pass costs no video memory.
		void ReleaseTargets();

	private:
		GraphicsDevice& m_device;

		SsaoSettings m_settings;

		/// @brief Current G-Buffer dimensions. AO targets derive from these.
		uint32 m_gbufferWidth;
		uint32 m_gbufferHeight;

		/// @brief Resolution the current targets were built for. Used to detect when a resize or
		///        a halfResolution toggle invalidates them.
		uint32 m_targetWidth = 0;
		uint32 m_targetHeight = 0;

		/// @brief Raw AO output, single channel.
		RenderTexturePtr m_aoRT;

		/// @brief Ping target for the separable blur (horizontal result).
		RenderTexturePtr m_blurRT;

		/// @brief 1x1 white texture handed out while SSAO is disabled, so the lighting shader can
		///        sample unconditionally with no permutation and no branch.
		TexturePtr m_whiteTexture;

		ConstantBufferPtr m_ssaoBuffer;

		/// @brief Holds the blur direction for the separable blur (b3). Updated between the
		///        horizontal and vertical passes.
		ConstantBufferPtr m_ssaoBlurBuffer;

		ShaderPtr m_ssaoPs;

		ShaderPtr m_ssaoBlurPs;
	};
}
