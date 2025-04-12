// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "render_texture.h"
#include "math/vector2.h"
#include "graphics_device.h"

namespace mmo
{
	/// @brief Class that represents a G-Buffer for deferred rendering.
	class GBuffer final : public NonCopyable
	{
	public:
		/// @brief Creates a new instance of the GBuffer class.
		/// @param device The graphics device to use.
		/// @param width The width of the G-Buffer.
		/// @param height The height of the G-Buffer.
		GBuffer(GraphicsDevice& device, uint32 width, uint32 height);

		/// @brief Destructor.
		~GBuffer() override = default;

	public:
		/// @brief Gets the albedo render texture.
		/// @return The albedo render texture.
		[[nodiscard]] RenderTexture& GetAlbedoRT() { return *m_albedoRT; }

		/// @brief Gets the normal render texture.
		/// @return The normal render texture.
		[[nodiscard]] RenderTexture& GetNormalRT() { return *m_normalRT; }

		/// @brief Gets the material render texture.
		/// @return The material render texture.
		[[nodiscard]] RenderTexture& GetMaterialRT() { return *m_materialRT; }

		/// @brief Gets the emissive render texture.
		/// @return The emissive render texture.
		[[nodiscard]] RenderTexture& GetEmissiveRT() { return *m_emissiveRT; }

		[[nodiscard]] RenderTexture& GetViewRayRT() { return *m_viewRayRT; }

		/// @brief Gets the depth render texture.
		/// @return The depth render texture.
		[[nodiscard]] RenderTexture& GetDepthRT() { return *m_depthRT; }

		/// @brief Gets the size of the G-Buffer.
		/// @return The size of the G-Buffer.
		[[nodiscard]] Vector2 GetSize() const { return Vector2(static_cast<float>(m_width), static_cast<float>(m_height)); }

		/// @brief Gets the width of the G-Buffer.
		/// @return The width of the G-Buffer.
		[[nodiscard]] uint32 GetWidth() const { return m_width; }

		/// @brief Gets the height of the G-Buffer.
		/// @return The height of the G-Buffer.
		[[nodiscard]] uint32 GetHeight() const { return m_height; }

		/// @brief Resizes the G-Buffer.
		/// @param width The new width of the G-Buffer.
		/// @param height The new height of the G-Buffer.
		void Resize(uint32 width, uint32 height);

		/// @brief Binds the G-Buffer for rendering.
		void Bind();

		/// @brief Unbinds the G-Buffer.
		void Unbind();

	private:
		/// @brief The graphics device.
		GraphicsDevice& m_device;

		/// @brief The width of the G-Buffer.
		uint32 m_width;

		/// @brief The height of the G-Buffer.
		uint32 m_height;

		/// @brief The albedo render texture (RGB: Albedo, A: Opacity).
		RenderTexturePtr m_albedoRT;

		/// @brief The normal render texture (RGB: Normal, A: Unused).
		RenderTexturePtr m_normalRT;

		/// @brief The material render texture (R: Metallic, G: Roughness, B: Specular, A: Ambient Occlusion).
		RenderTexturePtr m_materialRT;

		/// @brief The emissive render texture (RGB: Emissive, A: Unused).
		RenderTexturePtr m_emissiveRT;

		RenderTexturePtr m_viewRayRT;

		/// @brief The depth render texture.
		RenderTexturePtr m_depthRT;
	};
}
