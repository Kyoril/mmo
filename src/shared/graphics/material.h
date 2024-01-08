// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include "graphics/shader_base.h"
#include "graphics/texture.h"

#include <span>
#include <memory>

namespace mmo
{
	class ShaderCompiler;
	class GraphicsDevice;
	class MaterialCompiler;

	/// @brief Enumerates possible material types.
	enum class MaterialType : uint8_t
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

	enum class VertexShaderType : uint8_t
	{
		/// @brief Default vertex shader (no skinning).
		Default,

		/// @brief Skinning profile with low amount of bones (32).
		SkinnedLow,

		/// @brief Skinning profile with medium amount of bones (64).
		SkinnedMedium,

		/// @brief Skinning profile with high amount of bones (128)
		SkinnedHigh
	};

	/// @brief This class represents a material which describes how geometry in the scene
	///	       graph should be rendered.
	class Material : public std::enable_shared_from_this<Material>
	{
	public:
		/// @brief Creates a new instance of the Material class and initializes it.
		explicit Material(std::string_view name);
		
		virtual ~Material() = default;

	public:
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

		[[nodiscard]] std::string_view GetName() const noexcept { return m_name; }

		/// @brief Sets the name of the material.
		/// @param name The material name.
		void SetName(const std::string_view name) noexcept { m_name = name; }

		void ClearTextures();

		void AddTexture(std::string_view texture);

		void SetVertexShaderCode(VertexShaderType shaderType, std::span<uint8> code) noexcept;

		void SetPixelShaderCode(std::span<uint8> code) noexcept;

        [[nodiscard]] std::span<uint8 const> GetVertexShaderCode(VertexShaderType type) const noexcept { return { m_vertexShaderCode[static_cast<uint32_t>(type)]}; }
		
		[[nodiscard]] std::span<uint8 const> GetPixelShaderCode() const noexcept { return { m_pixelShaderCode }; }

		[[nodiscard]] bool IsDepthTestEnabled() const noexcept { return m_depthTest; }
		
		void SetDepthTestEnabled(const bool enable) noexcept { m_depthTest = enable; }

		[[nodiscard]] bool IsDepthWriteEnabled() const noexcept { return m_depthWrite; }
		
		void SetDepthWriteEnabled(const bool enable) noexcept { m_depthWrite = enable; }

	public:
		/// @brief Gets the texture files referenced by this material, in order.
		[[nodiscard]] const std::vector<String>& GetTextureFiles() const noexcept { return m_textureFiles; }

		/// @brief Ensures that the material is loaded.
		void Update();

		/// @brief Compiles the material.
		/// @param compiler The compiler to use for compiling the material.
		bool Compile(MaterialCompiler& compiler, ShaderCompiler& shaderCompiler);

		ShaderPtr& GetVertexShader(VertexShaderType type) noexcept { return m_vertexShader[static_cast<uint32_t>(type)]; }

		ShaderPtr& GetPixelShader() noexcept { return m_pixelShader; }

		void Apply(GraphicsDevice& device);

	private:
		void BindShaders(GraphicsDevice& device);

		void BindTextures(GraphicsDevice& device);
		
	private:
		String m_name;
		bool m_twoSided { false };
		bool m_castShadow { true };
		bool m_receiveShadows { true };
		MaterialType m_type { MaterialType::Opaque };
		ShaderPtr m_vertexShader[4];
		ShaderPtr m_pixelShader;
		std::vector<String> m_textureFiles;
		std::vector<TexturePtr> m_textures;
		bool m_texturesChanged { true };
		std::vector<uint8> m_vertexShaderCode[4];
		bool m_vertexShaderChanged { true };
		std::vector<uint8> m_pixelShaderCode;
		bool m_pixelShaderChanged { true };
		bool m_depthWrite { true };
		bool m_depthTest { true };
	};

	typedef std::shared_ptr<Material> MaterialPtr;
}
