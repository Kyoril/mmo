// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>

#include "base/typedefs.h"

#include "graphics/shader_base.h"
#include "graphics/texture.h"
#include "graphics/shader_types.h"

#include <span>
#include <memory>

#include "constant_buffer.h"
#include "math/vector3.h"
#include "math/vector4.h"

namespace mmo
{
	class Material;
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

		UserInterface,
	};

	enum class MaterialDomain : uint8_t
	{
		Surface,

		UserInterface,
	};

	struct ScalarParameterValue
	{
		String name;
		float value;
	};

	struct VectorParameterValue
	{
		String name;
		Vector4 value;
	};

	struct TextureParameterValue
	{
		String name;
		String texture;
	};

	enum class MaterialParameterType : uint8
	{
		Scalar,
		Vector,
		Texture
	};

	class MaterialInterface : public std::enable_shared_from_this<MaterialInterface>
	{
	public:
		virtual ~MaterialInterface() = default;

	public:

		/// @brief Ensures that the material is loaded.
		virtual void Update() = 0;

		virtual std::shared_ptr<Material> GetBaseMaterial() = 0;

		virtual ShaderPtr& GetVertexShader(VertexShaderType type) = 0;

		virtual ShaderPtr& GetPixelShader(PixelShaderType type = PixelShaderType::Forward) = 0;

		virtual void Apply(GraphicsDevice& device, MaterialDomain domain, PixelShaderType pixelShaderType = PixelShaderType::Forward) = 0;

		virtual ConstantBufferPtr GetParameterBuffer(MaterialParameterType type, GraphicsDevice& device) = 0;

		/// @brief Sets whether this material should render geometry without backface culling.
		/// @param value True if both sides of geometry should be rendered, false to cull the back face.
		virtual void SetTwoSided(const bool value) = 0;

		/// @brief Gets whether this material should render geometry without backface culling.
		[[nodiscard]] virtual bool IsTwoSided() const = 0;

		/// @brief Sets whether this material casts shadows.
		/// @param value True if this material should cast shadows.
		virtual void SetCastShadows(const bool value) = 0;

		/// @brief Gets whether this material casts shadows.
		[[nodiscard]] virtual bool IsCastingShadows() const = 0;

		/// @brief Sets whether this material should receive shadows.
		/// @param receive True if the material should receive shadows.
		virtual void SetReceivesShadows(const bool receive) = 0;

		/// @brief Gets whether this material is receiving shadows.
		[[nodiscard]] virtual bool IsReceivingShadows() const = 0;

		/// @brief Sets the type of the material.
		/// @param value The new material type.
		virtual void SetType(const MaterialType value) = 0;

		/// @brief Gets the type of this material.
		[[nodiscard]] virtual MaterialType GetType() const = 0;

		/// @brief Gets whether this material is translucent.
		[[nodiscard]] virtual bool IsTranslucent() const = 0;

		/// @brief Gets whether this material is receiving light.
		[[nodiscard]] virtual bool IsLit() const = 0;

		[[nodiscard]] virtual bool IsDepthTestEnabled() const = 0;

		void virtual SetDepthTestEnabled(const bool enable) = 0;

		[[nodiscard]] virtual bool IsDepthWriteEnabled() const = 0;

		virtual void SetDepthWriteEnabled(const bool enable) = 0;

		[[nodiscard]] virtual std::string_view GetName() const = 0;

		virtual void ClearParameters() = 0;

		virtual const std::vector<ScalarParameterValue>& GetScalarParameters() const = 0;

		virtual void AddScalarParameter(std::string_view name, float defaultValue) = 0;

		virtual void SetScalarParameter(std::string_view name, float value) = 0;

		virtual bool GetScalarParameter(std::string_view name, float& out_value) = 0;

		virtual const std::vector<VectorParameterValue>& GetVectorParameters() const = 0;

		virtual void AddVectorParameter(std::string_view name, const Vector4& defaultValue) = 0;

		virtual void SetVectorParameter(std::string_view name, const Vector4& value) = 0;

		virtual bool GetVectorParameter(std::string_view name, Vector4& out_value) = 0;

		virtual const std::vector<TextureParameterValue>& GetTextureParameters() const = 0;

		virtual void AddTextureParameter(std::string_view name, const String& defaultValue) = 0;

		virtual void SetTextureParameter(std::string_view name, const String& value) = 0;

		virtual void SetTextureParameter(std::string_view name, const TexturePtr& value) = 0;

		virtual bool GetTextureParameter(std::string_view name, String& out_value) = 0;

		virtual bool IsWireframe() const = 0;

		virtual void SetWireframe(bool value) = 0;
	};

	/// @brief This class represents a material which describes how geometry in the scene
	///	       graph should be rendered.
	class Material : public MaterialInterface
	{
	public:
		/// @brief Creates a new instance of the Material class and initializes it.
		explicit Material(std::string_view name);
		
		virtual ~Material() override = default;

	public:
		virtual std::shared_ptr<Material> GetBaseMaterial() override { return AsShared(); }

		/// @brief Sets whether this material should render geometry without backface culling.
		/// @param value True if both sides of geometry should be rendered, false to cull the back face.
		void SetTwoSided(const bool value) override { m_twoSided = value; }

		/// @brief Gets whether this material should render geometry without backface culling.
		[[nodiscard]] bool IsTwoSided() const override { return m_twoSided; }

		/// @brief Sets whether this material casts shadows.
		/// @param value True if this material should cast shadows.
		void SetCastShadows(const bool value) override { m_castShadow = value; }

		/// @brief Gets whether this material casts shadows.
		[[nodiscard]] bool IsCastingShadows() const override { return m_castShadow; }

		/// @brief Sets whether this material should receive shadows.
		/// @param receive True if the material should receive shadows.
		void SetReceivesShadows(const bool receive) override { m_receiveShadows = receive; }

		/// @brief Gets whether this material is receiving shadows.
		[[nodiscard]] bool IsReceivingShadows() const override { return m_receiveShadows; }

		/// @brief Sets the type of the material.
		/// @param value The new material type.
		void SetType(const MaterialType value) override { m_type = value; }

		/// @brief Gets the type of this material.
		[[nodiscard]] MaterialType GetType() const override { return m_type; }

		/// @brief Gets whether this material is translucent.
		[[nodiscard]] bool IsTranslucent() const override { return m_type == MaterialType::Translucent; }

		/// @brief Gets whether this material is receiving light.
		[[nodiscard]] bool IsLit() const override { return m_type == MaterialType::Masked || m_type == MaterialType::Translucent || m_type == MaterialType::Opaque; }

		[[nodiscard]] std::string_view GetName() const override { return m_name; }

		/// @brief Sets the name of the material.
		/// @param name The material name.
		void SetName(const std::string_view name) { m_name = name; }

		void ClearTextures();

		void AddTexture(std::string_view texture);

		void SetVertexShaderCode(VertexShaderType shaderType, std::span<uint8> code);

		void SetPixelShaderCode(PixelShaderType shaderType, std::span<uint8> code);

        [[nodiscard]] std::span<uint8 const> GetVertexShaderCode(VertexShaderType type) const { return { m_vertexShaderCode[static_cast<uint32_t>(type)]}; }
		
		[[nodiscard]] std::span<uint8 const> GetPixelShaderCode(PixelShaderType type = PixelShaderType::Forward) const { return { m_pixelShaderCode[static_cast<uint32_t>(type)] }; }

		[[nodiscard]] bool IsDepthTestEnabled() const override { return m_depthTest; }
		
		void SetDepthTestEnabled(const bool enable) override { m_depthTest = enable; }

		[[nodiscard]] bool IsDepthWriteEnabled() const override { return m_depthWrite; }
		
		void SetDepthWriteEnabled(const bool enable) override { m_depthWrite = enable; }

		virtual ConstantBufferPtr GetParameterBuffer(MaterialParameterType type, GraphicsDevice& device) override;

		virtual void ClearParameters() override;

		virtual void AddScalarParameter(std::string_view name, float defaultValue) override;

		virtual void SetScalarParameter(std::string_view name, float value) override;

		virtual bool GetScalarParameter(std::string_view name, float& out_value) override;

		virtual void AddVectorParameter(std::string_view name, const Vector4& defaultValue) override;

		virtual void SetVectorParameter(std::string_view name, const Vector4& value) override;

		virtual bool GetVectorParameter(std::string_view name, Vector4& out_value) override;

		virtual void AddTextureParameter(std::string_view name, const String& defaultValue) override;

		virtual void SetTextureParameter(std::string_view name, const String& value) override;

		virtual void SetTextureParameter(std::string_view name, const TexturePtr& value) override;

		virtual bool GetTextureParameter(std::string_view name, String& out_value) override;

		std::shared_ptr<Material> AsShared();

		virtual bool IsWireframe() const override { return m_wireframe; }

		virtual void SetWireframe(bool value) override { m_wireframe = value; }

	public:
		/// @brief Gets the texture files referenced by this material, in order.
		[[nodiscard]] const std::vector<String>& GetTextureFiles() const { return m_textureFiles; }

		/// @brief Ensures that the material is loaded.
		void Update() override;

		ShaderPtr& GetVertexShader(VertexShaderType type) override { return m_vertexShader[static_cast<uint32_t>(type)]; }

		ShaderPtr& GetPixelShader(PixelShaderType type = PixelShaderType::Forward) override { return m_pixelShader[static_cast<uint32_t>(type)]; }

		void Apply(GraphicsDevice& device, MaterialDomain domain, PixelShaderType pixelShaderType = PixelShaderType::Forward) override;

		/// @brief Compiles the material.
		/// @param compiler The compiler to use for compiling the material.
		bool Compile(MaterialCompiler& compiler, ShaderCompiler& shaderCompiler);

		void BindTextures(GraphicsDevice& device);

	public:
		const std::vector<ScalarParameterValue>& GetScalarParameters() const override { return m_scalarParameters; }

		const std::vector<VectorParameterValue>& GetVectorParameters() const override { return m_vectorParameters; }

		const std::vector<TextureParameterValue>& GetTextureParameters() const override { return m_textureParameters; }

	private:
		String m_name;
		bool m_twoSided { false };
		bool m_castShadow { true };
		bool m_receiveShadows { true };
		MaterialType m_type { MaterialType::Opaque };
		ShaderPtr m_vertexShader[5];	// Non-skinned, skinned low, skinned medium, skinned high and UI
		ShaderPtr m_pixelShader[4]; // Forward, GBuffer, ShadowMap and UI
		std::vector<String> m_textureFiles;
		std::vector<TexturePtr> m_textures;
		bool m_texturesChanged { true };
		std::vector<uint8> m_vertexShaderCode[5];
		bool m_vertexShaderChanged { true };
		std::vector<uint8> m_pixelShaderCode[4]; // Forward, GBuffer and ShadowMap
		bool m_pixelShaderChanged[3] { true, true, true };
		bool m_depthWrite { true };
		bool m_depthTest { true };

		bool m_wireframe{ false };

		std::vector<ScalarParameterValue> m_scalarParameters;
		std::vector<VectorParameterValue> m_vectorParameters;
		std::vector<TextureParameterValue> m_textureParameters;

		bool m_bufferLayoutDirty[3] { true, true, true };
		bool m_bufferDataDirty[3] { true, true, true };
		ConstantBufferPtr m_parameterBuffers[3]{ nullptr, nullptr, nullptr };
		std::map<String, TexturePtr> m_textureParamTextures;
	};

	typedef std::shared_ptr<MaterialInterface> MaterialPtr;
}
