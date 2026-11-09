// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"
#include "graphics/texture.h"
#include "upscale_filter.h"

namespace mmo
{
	/// @brief Spatial upscaling of the finished, tonemapped frame from the internal render
	///			resolution to the output resolution.
	///
	/// @remark Runs after everything else in the frame, on display-encoded colour, which is where
	///			FSR 1 expects to sit. The UI is not part of the input; it stays at native resolution.
	///
	/// @remark Backend-neutral like the other passes: the shader blobs are the only backend-bound
	///			fact and live in upscale_pass.cpp. Targets are created lazily and released while
	///			nothing needs upscaling, so a native-resolution frame costs no memory and no draw.
	class UpscalePass final : public NonCopyable
	{
	public:
		/// @brief Creates the pass and its shaders. Allocates no render targets yet.
		explicit UpscalePass(GraphicsDevice& device);

		~UpscalePass() override = default;

	public:
		/// @brief Upscales the source to the output size.
		/// @param source The finished frame at internal resolution.
		/// @param sourceWidth Width of the source texture in pixels.
		/// @param sourceHeight Height of the source texture in pixels.
		/// @param outputWidth Width to upscale to.
		/// @param outputHeight Height to upscale to.
		/// @param filter The upscale filter.
		/// @param sharpness RCAS factor from RcasSharpnessFromStops, only used by FSR.
		/// @return The upscaled texture, or the source itself if UpscaleNeedsPass is false.
		TexturePtr Render(const TexturePtr& source, uint32 sourceWidth, uint32 sourceHeight,
			uint32 outputWidth, uint32 outputHeight, upscale_filter::Type filter, float sharpness);

	private:
		/// @brief Creates the targets the filter needs at the given size, and releases the rest.
		void EnsureTargets(upscale_filter::Type filter, uint32 width, uint32 height);

		/// @brief Releases every render target.
		void ReleaseTargets();

		/// @brief Draws one fullscreen pass from source into target.
		void DrawPass(const TexturePtr& source, RenderTexture& target, ShaderBase& pixelShader);

	private:
		GraphicsDevice& m_device;

		VertexBufferPtr m_quad;
		ShaderPtr m_vertexShader;
		ShaderPtr m_upscalePs;
		ShaderPtr m_easuPs;
		ShaderPtr m_rcasPs;
		ConstantBufferPtr m_constants;

		/// Output of the single-pass filters, and of RCAS for FSR.
		RenderTexturePtr m_outputRT;

		/// EASU output and RCAS input; only exists while FSR is in use.
		RenderTexturePtr m_easuRT;

		uint32 m_targetWidth = 0;
		uint32 m_targetHeight = 0;
	};
}
