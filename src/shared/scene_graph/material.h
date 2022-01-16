// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "graphics/pixel_shader.h"
#include "graphics/vertex_shader.h"

#include <memory>

namespace mmo
{
	/// @brief Enumerates possible material types.
	enum class MaterialType
	{
		/// @brief The material is completely opaque and lit.
		Opaque,

		/// @brief The material is unlit.
		Unlit,

		/// @brief The material is lit and masked (binary alpha).
		Masked,

		/// @brief The material is lit and translucent (support for non-binary alpha channel).
		Translucent,
	};

	/// @brief This class represents a material which describes how geometry in the scene
	///	       graph should be rendered.
	class Material : public std::enable_shared_from_this<Material>
	{
	public:
		/// @brief Creates a new instance of the Material class and initializes it.
		/// @param name Name of the material.
		explicit Material(String name);
		
		virtual ~Material() = default;

	public:
		/// @brief Gets the name of this material.
		[[nodiscard]] const String& GetName() const noexcept { return m_name; }

		/// @brief Sets whether this material should render geometry without backface culling.
		/// @param value True if both sides of geometry should be rendered, false to cull the back face.
		void SetTwoSided(const bool value) noexcept { m_twoSided = value; }

		/// @brief Gets whether this material should render geometry without backface culling.
		[[nodiscard]] bool IsTwoSided() const noexcept { return m_twoSided; }

		/// @brief Sets whether this material casts shadows.
		/// @param value True if this material should cast shadows.
		void SetCastShadows(const bool value) noexcept { m_castShadow = value; }

		/// @brief Gets whether this material casts shadows.
		[[nodiscard]] bool IsCastingShadows() const noexcept { return m_castShadow; }

		/// @brief Sets whether this material should receive shadows.
		/// @param receive True if the material should receive shadows.
		void SetReceivesShadows(const bool receive) noexcept { m_receiveShadows = receive; }

		/// @brief Gets whether this material is receiving shadows.
		[[nodiscard]] bool IsReceivingShadows() const noexcept { return m_receiveShadows; }

		/// @brief Sets the type of the material.
		/// @param value The new material type.
		void SetType(const MaterialType value) noexcept { m_type = value; }

		/// @brief Gets the type of this material.
		[[nodiscard]] MaterialType GetType() const noexcept { return m_type; }

		/// @brief Gets whether this material is translucent.
		[[nodiscard]] bool IsTranslucent() const noexcept { return m_type == MaterialType::Translucent; }

		/// @brief Gets whether this material is receiving light.
		[[nodiscard]] bool IsLit() const noexcept { return m_type == MaterialType::Masked || m_type == MaterialType::Translucent || m_type == MaterialType::Opaque; }


		void SetVertexShader(const std::shared_ptr<VertexShader>& vertexShader) { m_vertexShader = vertexShader; }

		/// @brief Gets the vertex shader that is being used.
		///	@return The vertex shader that is being used.
		[[nodiscard]] const std::shared_ptr<VertexShader>& GetVertexShader() const noexcept { return m_vertexShader; }
		
		void SetPixelShader(const std::shared_ptr<PixelShader>& pixelShader) { m_pixelShader = pixelShader; }

		/// @brief Gets the pixel shader that is currently being used.
		/// @return The pixel shader to use when rendering something using this material.
		[[nodiscard]] const std::shared_ptr<PixelShader>& GetPixelShader() const noexcept { return m_pixelShader; }

	private:
		String m_name;
		bool m_twoSided { false };
		bool m_castShadow { true };
		bool m_receiveShadows { true };
		MaterialType m_type { MaterialType::Opaque };
		std::shared_ptr<VertexShader> m_vertexShader;
		std::shared_ptr<PixelShader> m_pixelShader;
	};
}
