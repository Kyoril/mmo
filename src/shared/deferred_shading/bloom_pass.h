// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bloom_settings.h"

#include "base/non_copyable.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "graphics/constant_buffer.h"
#include "graphics/vertex_buffer.h"
#include "graphics/shader_base.h"
#include "graphics/texture.h"

#include <vector>

namespace mmo
{
	/// @brief Physically-inspired bloom: a 13-tap downsample chain and a tent-filter upsample chain
	///        over the linear HDR scene. The TonemapPass adds the result.
	/// @remark Backend-neutral like SsaoPass; the shader blob choice is isolated in bloom_pass.cpp.
	class BloomPass final : public NonCopyable
	{
	public:
		/// @brief Creates the pass. Targets are allocated on the first Render.
		BloomPass(GraphicsDevice& device, uint32 width, uint32 height);

		~BloomPass() override = default;

	public:
		/// @brief Records a new scene size. Targets are rebuilt lazily.
		void Resize(uint32 width, uint32 height);

		/// @brief Builds the bloom texture from the linear HDR scene.
		/// @remark Does nothing (and releases its targets) when the settings disable bloom.
		void Render(RenderTexture& sceneColor, VertexBuffer& quad, ShaderBase& fullscreenVs);

		/// @brief Gets the bloom texture, or nullptr when bloom is disabled.
		[[nodiscard]] TexturePtr GetResult() const;

		/// @brief Gets the weight the TonemapPass applies: intensity divided by the level count,
		///        because every level adds its own copy of the light.
		[[nodiscard]] float GetResultScale() const;

		/// @brief Gets the mutable settings.
		[[nodiscard]] BloomSettings& GetSettings() { return m_settings; }

		/// @brief Gets the settings.
		[[nodiscard]] const BloomSettings& GetSettings() const { return m_settings; }

	private:
		void EnsureTargets();
		void ReleaseTargets();

	private:
		GraphicsDevice& m_device;

		BloomSettings m_settings;

		uint32 m_sceneWidth;
		uint32 m_sceneHeight;

		/// @brief Configuration the current targets were built for.
		uint32 m_builtDivisor = 0;
		uint32 m_builtLevels = 0;
		uint32 m_builtWidth = 0;
		uint32 m_builtHeight = 0;

		std::vector<RenderTexturePtr> m_downTargets;
		std::vector<RenderTexturePtr> m_upTargets;
		std::vector<uint32> m_levelWidths;
		std::vector<uint32> m_levelHeights;

		ConstantBufferPtr m_downsampleBuffer;
		ConstantBufferPtr m_upsampleBuffer;

		ShaderPtr m_downsamplePs;
		ShaderPtr m_upsamplePs;
	};
}
