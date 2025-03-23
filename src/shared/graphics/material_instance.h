// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>

#include "base/typedefs.h"

#include "graphics/shader_base.h"
#include "graphics/texture.h"

#include <span>
#include <memory>

#include "material.h"

namespace mmo
{
	/// @brief This class represents a material which describes how geometry in the scene
	///	       graph should be rendered.
	class MaterialInstance : public MaterialInterface
	{
	public:
		/// @brief Creates a new instance of the Material class and initializes it.
		explicit MaterialInstance(std::string_view name, MaterialPtr parentMaterial);
		
		virtual ~MaterialInstance() override = default;

	public:
		virtual std::shared_ptr<Material> GetBaseMaterial() override { return m_parent->GetBaseMaterial(); }

		/// @brief Sets whether this material should render geometry without backface culling.
		/// @param value True if both sides of geometry should be rendered, false to cull the back face.
		virtual void SetTwoSided(const bool value) noexcept override { m_twoSided = value; }

		/// @brief Gets whether this material should render geometry without backface culling.
		[[nodiscard]] bool IsTwoSided() const noexcept override { return m_twoSided; }

		/// @brief Sets whether this material casts shadows.
		/// @param value True if this material should cast shadows.
		void SetCastShadows(const bool value) noexcept override { m_castShadows = value; }

		/// @brief Gets whether this material casts shadows.
		[[nodiscard]] bool IsCastingShadows() const noexcept override { return m_castShadows; }

		/// @brief Sets whether this material should receive shadows.
		/// @param receive True if the material should receive shadows.
		void SetReceivesShadows(const bool receive) noexcept override { m_receiveShadows = receive; }

		/// @brief Gets whether this material is receiving shadows.
		[[nodiscard]] bool IsReceivingShadows() const noexcept override { return m_receiveShadows; }

		/// @brief Sets the type of the material.
		/// @param value The new material type.
		void SetType(const MaterialType value) noexcept override { m_type = value; }

		/// @brief Gets the type of this material.
		[[nodiscard]] MaterialType GetType() const noexcept override { return m_type; }

		/// @brief Gets whether this material is translucent.
		[[nodiscard]] bool IsTranslucent() const noexcept override { ASSERT(m_parent); return m_parent->IsTranslucent(); }

		/// @brief Gets whether this material is receiving light.
		[[nodiscard]] bool IsLit() const noexcept override { ASSERT(m_parent); return m_parent->IsLit(); }

		[[nodiscard]] bool IsDepthTestEnabled() const noexcept override { return m_depthTest; }

		void SetDepthTestEnabled(const bool enable) noexcept override { m_depthTest = enable; }

		[[nodiscard]] bool IsDepthWriteEnabled() const noexcept override { return m_depthWrite; }

		void SetDepthWriteEnabled(const bool enable) noexcept override { m_depthWrite = enable; }

		[[nodiscard]] std::string_view GetName() const noexcept override { return m_name; }

		/// @brief Sets the name of the material.
		/// @param name The material name.
		void SetName(const std::string_view name) noexcept { m_name = name; }

		void SetParent(MaterialPtr parent) noexcept;

		[[nodiscard]] MaterialPtr GetParent() const noexcept { return m_parent; }

		std::shared_ptr<MaterialInstance> AsShared();

		void RefreshParametersFromBase();

	public:
		void DerivePropertiesFromParent();

	public:
		/// @brief Ensures that the material is loaded.
		void Update() override;

		ShaderPtr& GetVertexShader(VertexShaderType type) noexcept override { return m_parent->GetVertexShader(type); }

		ShaderPtr& GetPixelShader() noexcept override { return m_parent->GetPixelShader(); }

		void Apply(GraphicsDevice& device, MaterialDomain domain) override;

		ConstantBufferPtr GetParameterBuffer(MaterialParameterType type, GraphicsDevice& device) override;

		void ClearParameters() override;

		void AddScalarParameter(std::string_view name, float defaultValue) override;

		void SetScalarParameter(std::string_view name, float value) override;

		bool GetScalarParameter(std::string_view name, float& out_value) override;

		void AddVectorParameter(std::string_view name, const Vector4& defaultValue) override;

		void SetVectorParameter(std::string_view name, const Vector4& value) override;

		bool GetVectorParameter(std::string_view name, Vector4& out_value) override;

		void AddTextureParameter(std::string_view name, const String& defaultValue) override;

		void SetTextureParameter(std::string_view name, const String& value) override;

		void SetTextureParameter(std::string_view name, const TexturePtr& value) override;

		bool GetTextureParameter(std::string_view name, String& out_value) override;

		const std::vector<ScalarParameterValue>& GetScalarParameters() const override { return m_scalarParameters; }

		const std::vector<VectorParameterValue>& GetVectorParameters() const override { return m_vectorParameters; }

		const std::vector<TextureParameterValue>& GetTextureParameters() const override { return m_textureParameters; }

	private:
		String m_name;
		MaterialPtr m_parent;

		bool m_twoSided;
		bool m_castShadows;
		bool m_receiveShadows;
		MaterialType m_type;
		bool m_depthTest;
		bool m_depthWrite;

		std::vector<ScalarParameterValue> m_scalarParameters;
		std::vector<VectorParameterValue> m_vectorParameters;
		std::vector<TextureParameterValue> m_textureParameters;
		std::map<String, TexturePtr> m_textureParamTextures;

		bool m_bufferLayoutDirty[3]{ true, true, true };
		bool m_bufferDataDirty[3]{ true, true, true };
		ConstantBufferPtr m_parameterBuffers[3]{ nullptr, nullptr, nullptr };
	};

}
