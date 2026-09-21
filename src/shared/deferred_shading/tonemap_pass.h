// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "tonemap_settings.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"
#include "graphics/texture.h"
#include "graphics/sampler_state.h"

#include <map>

namespace mmo
{
	/// @brief The last pass of the deferred frame: bloom composite, exposure, ACES, gamma, dither.
	/// @remark Backend-neutral like SsaoPass; the shader blob choice is isolated in tonemap_pass.cpp.
	class TonemapPass final : public NonCopyable
	{
	public:
		/// @brief Creates the pass and its full-resolution output target.
		TonemapPass(GraphicsDevice& device, uint32 width, uint32 height);

		~TonemapPass() override = default;

	public:
		/// @brief Resizes the output target.
		void Resize(uint32 width, uint32 height);

		/// @brief Tone maps the linear HDR scene into the output target.
		/// @param hdrScene The finished linear HDR scene (after the forward pass).
		/// @param bloom The bloom result, or nullptr for none.
		/// @param bloomScale Weight of the bloom texture; ignored when bloom is nullptr.
		/// @param quad The fullscreen quad vertex buffer.
		/// @param fullscreenVs The pass-through fullscreen vertex shader.
		void Render(RenderTexture& hdrScene, const TexturePtr& bloom, float bloomScale, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Sets the zone LUTs for the next frames. Empty paths mean no LUT.
		/// @param lut Target LUT texture path.
		/// @param lutFrom LUT being faded out.
		/// @param blend Weight of lut (lutFrom gets 1 - blend).
		void SetLuts(const String& lut, const String& lutFrom, float blend);

		/// @brief Gets the display-referred output.
		[[nodiscard]] RenderTexturePtr GetResult() const { return m_outputRT; }

		/// @brief Gets the mutable settings.
		[[nodiscard]] TonemapSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings.
		[[nodiscard]] const TonemapSettings& GetSettings() const { return m_settings; }

	private:
		/// @brief Loads a strip LUT once; empty, missing or wrongly sized textures resolve to nullptr (warned once).
		TexturePtr ResolveLut(const String& path);

		GraphicsDevice& m_device;

		TonemapSettings m_settings;

		uint32 m_width;
		uint32 m_height;

		RenderTexturePtr m_outputRT;

		/// @brief 1x1 black stand-in bound when there is no bloom, and also when a LUT slot (t2/t3) has no LUT to bind.
		TexturePtr m_blackTexture;

		ConstantBufferPtr m_tonemapBuffer;

		ShaderPtr m_tonemapPs;

		/// @brief Cache of loaded strip LUT textures, keyed by path, so repeated zone transitions don't reload.
		std::map<String, TexturePtr> m_lutCache;
		/// @brief Target LUT texture for the current blend (nullptr means no LUT).
		TexturePtr m_lut;
		/// @brief LUT being faded out during a zone cross-fade (nullptr means no LUT).
		TexturePtr m_lutFrom;
		/// @brief Weight of m_lut in the blend; m_lutFrom gets 1 - m_lutBlend.
		float m_lutBlend = 1.0f;
		/// @brief Sampler used to fetch the LUT textures; may be nullptr on backends where CreateSamplerState fails.
		SamplerStatePtr m_lutSampler;
	};
}
