// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "contact_shadow_settings.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"
#include "math/vector3.h"

namespace mmo
{
	class Camera;

	/// @brief Screen-space contact shadow pass for the sun.
	/// @remark Runs between the geometry and lighting passes and produces a single-channel term the
	///         lighting pass multiplies into the directional light's shadow. It fills in the few
	///         centimetres at contact that a cascaded shadow map cannot resolve, because its depth
	///         bias and finite resolution erase the shadow exactly there.
	/// @remark Always full resolution. Unlike SSAO there is deliberately no half-resolution mode:
	///         this term IS the fine detail, so halving the resolution would erase what it exists to
	///         draw.
	/// @remark Deliberately backend-neutral: this class talks only to GraphicsDevice abstractions and
	///         contains no #ifdef and no D3D11 references. The one backend-bound fact — which shader
	///         bytecode blob to use — is isolated in contact_shadow_pass.cpp.
	class ContactShadowPass final : public NonCopyable
	{
	public:
		/// @brief Creates the contact shadow pass.
		/// @param device The graphics device to use.
		/// @param width Width of the G-Buffer.
		/// @param height Height of the G-Buffer.
		ContactShadowPass(GraphicsDevice& device, uint32 width, uint32 height);

		~ContactShadowPass() override = default;

	public:
		/// @brief Resizes the targets to match a new G-Buffer size.
		void Resize(uint32 width, uint32 height);

		/// @brief Computes the contact shadow term for the current frame.
		/// @param camera Camera the frame is rendered with.
		/// @param gbufferNormalRT The G-Buffer normal target (rgb = normal, a = radial depth).
		/// @param gbufferDepthRT The G-Buffer depth target, read for its STENCIL plane: non-zero
		///        tags geometry that casts no shadows and so must not occlude the march.
		/// @param sunDirection World-space direction TOWARD the sun, normalised.
		/// @param quad The fullscreen quad vertex buffer.
		/// @param fullscreenVs The pass-through fullscreen vertex shader.
		/// @remark Does nothing when settings.enabled is false; GetResult() then yields white.
		void Render(Camera& camera, RenderTexture& gbufferNormalRT, RenderTexture& gbufferDepthRT,
			const Vector3& sunDirection, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Gets the contact shadow texture for the lighting pass to sample.
		/// @return The blurred term, or a 1x1 white texture when contact shadows are disabled.
		[[nodiscard]] TexturePtr GetResult() const;

		/// @brief Gets the mutable settings for this pass.
		[[nodiscard]] ContactShadowSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings for this pass.
		[[nodiscard]] const ContactShadowSettings& GetSettings() const { return m_settings; }

	private:
		/// @brief (Re)creates the render targets at the current G-Buffer size.
		void EnsureTargets();

		/// @brief Releases the render targets. Called when the effect is disabled so a disabled pass
		///        costs no video memory.
		void ReleaseTargets();

	private:
		GraphicsDevice& m_device;

		ContactShadowSettings m_settings;

		/// @brief Current G-Buffer dimensions. Targets match these exactly (no half-res mode).
		uint32 m_gbufferWidth;
		uint32 m_gbufferHeight;

		/// @brief Resolution the current targets were built for, to detect an invalidating resize.
		uint32 m_targetWidth = 0;
		uint32 m_targetHeight = 0;

		/// @brief Raw contact shadow output, single channel.
		RenderTexturePtr m_termRT;

		/// @brief Ping target for the separable blur (horizontal result).
		RenderTexturePtr m_blurRT;

		/// @brief 1x1 white texture handed out while the effect is disabled, so the lighting shader
		///        can sample unconditionally with no permutation and no branch.
		TexturePtr m_whiteTexture;

		ConstantBufferPtr m_contactShadowBuffer;

		/// @brief Holds the blur direction for the separable blur (b3). Updated between the
		///        horizontal and vertical passes.
		ConstantBufferPtr m_contactShadowBlurBuffer;

		ShaderPtr m_contactShadowPs;

		ShaderPtr m_contactShadowBlurPs;
	};
}
