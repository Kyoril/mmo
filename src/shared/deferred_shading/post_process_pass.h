// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "underwater_settings.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"
#include "graphics/texture.h"

namespace mmo
{
	class Camera;

	/// @brief Screen-space post-processing applied after the forward/translucent pass.
	///
	/// @remark Currently this is the underwater treatment and nothing else: fog and depth tint,
	///			screen distortion, projected caustics, sun shafts, and the surface crossing.
	///
	/// @remark The pass is skipped entirely whenever ShouldRunUnderwaterPass is false, and
	///			DeferredRenderer then returns the raw scene texture from GetFinalRenderTarget().
	///			Out of water this costs nothing at all - no draw, and no render target allocated
	///			until the first time the camera actually goes under. That matters because five
	///			separate tools consume GetFinalRenderTarget(): the client world frame, the world
	///			editor viewport, the world model editor, the material instance editor and the
	///			spell visualization preview.
	///
	/// @remark Backend-neutral in the same way as SsaoPass: only GraphicsDevice abstractions
	///			appear here, and the one backend-bound fact (which shader blob) is isolated in
	///			post_process_pass.cpp.
	class PostProcessPass final : public NonCopyable
	{
	public:
		/// @brief Creates the post-process pass.
		/// @param device The graphics device to use.
		/// @param width Width of the scene colour target.
		/// @param height Height of the scene colour target.
		PostProcessPass(GraphicsDevice& device, uint32 width, uint32 height);

		~PostProcessPass() override = default;

	public:
		/// @brief Records a new scene target size. The output target is rebuilt lazily on the next
		///			Render, so resizing while dry allocates nothing.
		void Resize(uint32 width, uint32 height);

		/// @brief Whether the pass would do any work for the given state.
		/// @remark DeferredRenderer calls this to decide both whether to draw and which texture to
		///			hand out, so the two decisions can never disagree.
		[[nodiscard]] bool WouldRun(const UnderwaterState& state) const { return ShouldRunUnderwaterPass(state); }

		/// @brief Applies the post-process to the scene colour.
		/// @param state Underwater state for this frame.
		/// @param camera Camera the frame was rendered with.
		/// @param sceneColor The finished scene colour from the forward pass.
		/// @param gbufferNormalRT The G-Buffer normal target (rgb = normal, a = radial depth).
		/// @param quad The fullscreen quad vertex buffer.
		/// @param fullscreenVs The pass-through fullscreen vertex shader.
		/// @param sunScreenU Sun position in screen UV space, X.
		/// @param sunScreenV Sun position in screen UV space, Y.
		/// @param elapsedSeconds Scene time, driving the distortion and caustic scroll.
		void Render(const UnderwaterState& state, Camera& camera, RenderTexture& sceneColor,
			RenderTexture& gbufferNormalRT, VertexBuffer& quad, ShaderBase& fullscreenVs,
			float sunScreenU, float sunScreenV, float elapsedSeconds);

		/// @brief Gets the processed texture. Only meaningful after a Render for which WouldRun
		///			was true.
		[[nodiscard]] TexturePtr GetResult() const { return m_outputRT; }

		/// @brief Sets the caustics texture, resolved from the water profile.
		void SetCausticsTexture(TexturePtr texture) { m_causticsTexture = std::move(texture); }

		/// @brief Gets the mutable transition settings.
		[[nodiscard]] UnderwaterSettings& GetSettings() { return m_settings; }

		/// @brief Gets the transition settings.
		[[nodiscard]] const UnderwaterSettings& GetSettings() const { return m_settings; }

	private:
		/// @brief Creates the output target if it is missing or stale.
		void EnsureTargets();

	private:
		GraphicsDevice& m_device;

		UnderwaterSettings m_settings;

		uint32 m_sceneWidth;
		uint32 m_sceneHeight;

		uint32 m_targetWidth{ 0 };
		uint32 m_targetHeight{ 0 };

		RenderTexturePtr m_outputRT;

		/// @brief 1x1 black stand-in bound when no caustics texture is set, so the shader can
		///			sample unconditionally with no permutation.
		TexturePtr m_blackTexture;

		TexturePtr m_causticsTexture;

		ConstantBufferPtr m_underwaterBuffer;

		ShaderPtr m_underwaterPs;
	};
}
