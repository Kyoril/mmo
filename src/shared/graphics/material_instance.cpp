// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_instance.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	MaterialInstance::MaterialInstance(const std::string_view name, MaterialPtr parent)
		: m_name(name)
		, m_parent(nullptr)
	{
		ASSERT(parent);
		SetParent(parent);
	}

	void MaterialInstance::SetParent(MaterialPtr parent) noexcept
	{
		ASSERT(parent);

		// Parent must be valid
		if (!parent)
		{
			return;
		}

		// Anything new at all?
		if (m_parent == parent)
		{
			return;
		}

		// Can't set ourself as parent
		if (parent.get() == this)
		{
			return;
		}

		// TODO: We also need to prevent a parent loop. A material instance parent could be a material instance as well, whose parent could again be us.

		const bool hadParent = m_parent != nullptr;

		// Update parent
		m_parent = parent;

		// Take parent parameter default values
		m_scalarParameters.clear();
		m_scalarParameters.reserve(m_parent->GetScalarParameters().size());
		m_vectorParameters.clear();
		m_vectorParameters.reserve(m_parent->GetVectorParameters().size());
		m_textureParameters.clear();
		m_textureParameters.reserve(m_parent->GetTextureParameters().size());
		m_textureParamTextures.clear();

		for (auto& param : m_parent->GetScalarParameters())
		{
			m_scalarParameters.push_back(param);
		}
		for (auto& param : m_parent->GetVectorParameters())
		{
			m_vectorParameters.push_back(param);
		}
		for (auto& param : m_parent->GetTextureParameters())
		{
			m_textureParameters.push_back(param);
			m_textureParamTextures[param.name] = TextureManager::Get().CreateOrRetrieve(param.texture);
		}

		// Refresh base values from parent material if this is the first reference to a parent material
		if (!hadParent)
		{
			DerivePropertiesFromParent();
		}
	}

	std::shared_ptr<MaterialInstance> MaterialInstance::AsShared()
	{
		return std::static_pointer_cast<MaterialInstance>(shared_from_this());
	}

	void MaterialInstance::RefreshParametersFromBase()
	{
		MaterialPtr baseMaterial = GetBaseMaterial();
		if (!baseMaterial)
		{
			return;
		}

		// Iterate through base material parameters
		auto scalarParamsCopy = m_scalarParameters;
		m_scalarParameters.clear();

		for (auto& param : m_parent->GetScalarParameters())
		{
			m_scalarParameters.push_back(param);
			if (auto it = std::find_if(scalarParamsCopy.begin(), scalarParamsCopy.end(), [&param](const ScalarParameterValue& value) { return value.name == param.name; }); it != scalarParamsCopy.end())
			{
				m_scalarParameters.back().value = it->value;
			}
		}

		auto vectorParamsCopy = m_vectorParameters;
		m_vectorParameters.clear();

		for (auto& param : m_parent->GetVectorParameters())
		{
			m_vectorParameters.push_back(param);
			if (auto it = std::find_if(vectorParamsCopy.begin(), vectorParamsCopy.end(), [&param](const VectorParameterValue& value) { return value.name == param.name; }); it != vectorParamsCopy.end())
			{
				m_vectorParameters.back().value = it->value;
			}
		}

		auto textureParamsCopy = m_textureParameters;
		m_textureParameters.clear();
		m_textureParamTextures.clear();

		for (auto& param : m_parent->GetTextureParameters())
		{
			m_textureParameters.push_back(param);
			if (auto it = std::find_if(textureParamsCopy.begin(), textureParamsCopy.end(), [&param](const TextureParameterValue& value) { return value.name == param.name; }); it != textureParamsCopy.end())
			{
				m_textureParameters.back().texture = it->texture;
			}

			m_textureParamTextures[param.name] = TextureManager::Get().CreateOrRetrieve(m_textureParameters.back().texture);
		}
	}

	void MaterialInstance::DerivePropertiesFromParent()
	{
		m_type = m_parent->GetType();
		m_receiveShadows = m_parent->IsReceivingShadows();
		m_castShadows = m_parent->IsCastingShadows();
		m_twoSided = m_parent->IsTwoSided();
		m_depthTest = m_parent->IsDepthTestEnabled();
		m_depthWrite = m_parent->IsDepthWriteEnabled();
		m_wireframe = m_parent->IsWireframe();
	}

	void MaterialInstance::Update()
	{
		m_parent->Update();
	}

	void MaterialInstance::Apply(GraphicsDevice& device, MaterialDomain domain, PixelShaderType pixelShaderType)
	{
		// Bind shaders from the material
		if (m_parent->GetVertexShader(VertexShaderType::Default)) m_parent->GetVertexShader(VertexShaderType::Default)->Set();
		if (m_parent->GetPixelShader(pixelShaderType)) m_parent->GetPixelShader(pixelShaderType)->Set();

		// Bind base material static textures
		auto baseMaterial = GetBaseMaterial();

		if (pixelShaderType != PixelShaderType::ShadowMap)
		{
			baseMaterial->BindTextures(device);
			// Bind texture parameter override textures
			uint32 shaderSlot = baseMaterial->GetTextureFiles().size();
			for (auto& param : m_textureParameters)
			{
				device.BindTexture(m_textureParamTextures[param.name], ShaderType::PixelShader, shaderSlot++);
			}

			device.SetDepthTestComparison(m_depthTest ? DepthTestMethod::Less : DepthTestMethod::Always);
			device.SetDepthWriteEnabled(m_depthWrite);
		}
		else
		{
			device.SetDepthEnabled(true);
			device.SetDepthWriteEnabled(true);
			device.SetDepthTestComparison(DepthTestMethod::LessEqual);
		}

		if (m_type == MaterialType::Translucent || m_type == MaterialType::Masked)
		{
			device.SetBlendMode(BlendMode::Alpha);
		}
		else
		{
			device.SetBlendMode(BlendMode::Opaque);
		}

		if (m_twoSided)
		{
			device.SetFaceCullMode(FaceCullMode::None);
		}
		else
		{
			device.SetFaceCullMode(FaceCullMode::Back);
		}

		device.SetFillMode(m_wireframe ? FillMode::Wireframe : FillMode::Solid);
	}

	ConstantBufferPtr MaterialInstance::GetParameterBuffer(MaterialParameterType type, GraphicsDevice& device)
	{
		if (m_bufferLayoutDirty[0])
		{
			m_parameterBuffers[0].reset();

			if (!m_scalarParameters.empty())
			{
				std::vector<float> initialData;
				for (auto& param : m_scalarParameters)
				{
					initialData.push_back(param.value);
				}

				m_parameterBuffers[0] = device.CreateConstantBuffer(sizeof(float) * m_scalarParameters.size(), initialData.data());
			}

			m_bufferLayoutDirty[0] = false;
			m_bufferDataDirty[0] = false;
		}

		if (m_bufferDataDirty[0])
		{
			std::vector<float> initialData;
			for (auto& param : m_scalarParameters)
			{
				initialData.push_back(param.value);
			}
			m_parameterBuffers[0]->Update(initialData.data());
			m_bufferDataDirty[0] = false;
		}

		if (m_bufferLayoutDirty[1])
		{
			m_parameterBuffers[1].reset();

			if (!m_vectorParameters.empty())
			{
				std::vector<Vector4> initialData;
				for (auto& param : m_vectorParameters)
				{
					initialData.push_back(param.value);
				}

				m_parameterBuffers[1] = device.CreateConstantBuffer(sizeof(Vector4) * m_vectorParameters.size(), initialData.data());
			}

			m_bufferLayoutDirty[1] = false;
			m_bufferDataDirty[1] = false;
		}

		if (m_bufferDataDirty[1])
		{
			std::vector<Vector4> initialData;
			for (auto& param : m_vectorParameters)
			{
				initialData.push_back(param.value);
			}
			m_parameterBuffers[1]->Update(initialData.data());
			m_bufferDataDirty[1] = false;
		}

		return m_parameterBuffers[(uint8)type];
	}

	void MaterialInstance::ClearParameters()
	{
		m_scalarParameters.clear();
		m_vectorParameters.clear();
		m_textureParameters.clear();
		m_textureParamTextures.clear();
	}

	void MaterialInstance::AddScalarParameter(std::string_view name, float defaultValue)
	{
	}

	void MaterialInstance::SetScalarParameter(std::string_view name, float value)
	{
		const auto it = std::find_if(m_scalarParameters.begin(), m_scalarParameters.end(), [&name](const ScalarParameterValue& value) { return value.name == name; });
		if (it == m_scalarParameters.end())
		{
			return;
		}

		if (it->value != value)
		{
			m_bufferDataDirty[(uint8)MaterialParameterType::Scalar] = true;
			it->value = value;
		}
	}

	bool MaterialInstance::GetScalarParameter(std::string_view name, float& out_value)
	{
		const auto it = std::find_if(m_scalarParameters.begin(), m_scalarParameters.end(), [&name](const ScalarParameterValue& value) { return value.name == name; });
		if (it == m_scalarParameters.end())
		{
			return false;
		}

		out_value = it->value;
		return true;
	}

	void MaterialInstance::AddVectorParameter(std::string_view name, const Vector4& defaultValue)
	{
	}

	void MaterialInstance::SetVectorParameter(std::string_view name, const Vector4& value)
	{
		const auto it = std::find_if(m_vectorParameters.begin(), m_vectorParameters.end(), [&name](const VectorParameterValue& value) { return value.name == name; });
		if (it == m_vectorParameters.end())
		{
			return;
		}

		if (it->value != value)
		{
			m_bufferDataDirty[(uint8)MaterialParameterType::Vector] = true;
			it->value = value;
		}
	}

	bool MaterialInstance::GetVectorParameter(std::string_view name, Vector4& out_value)
	{
		const auto it = std::find_if(m_vectorParameters.begin(), m_vectorParameters.end(), [&name](const VectorParameterValue& value) { return value.name == name; });
		if (it == m_vectorParameters.end())
		{
			return false;
		}

		out_value = it->value;
		return true;
	}

	void MaterialInstance::AddTextureParameter(std::string_view name, const String& defaultValue)
	{
	}

	void MaterialInstance::SetTextureParameter(std::string_view name, const String& value)
	{
		const auto it = std::find_if(m_textureParameters.begin(), m_textureParameters.end(), [&name](const auto& value) { return value.name == name; });
		if (it == m_textureParameters.end())
		{
			return;
		}

		it->texture = value;
		m_textureParamTextures[String(name)] = TextureManager::Get().CreateOrRetrieve(value);
	}

	void MaterialInstance::SetTextureParameter(std::string_view name, const TexturePtr& value)
	{
		const auto it = std::find_if(m_textureParameters.begin(), m_textureParameters.end(), [&name](const auto& value) { return value.name == name; });
		if (it == m_textureParameters.end())
		{
			return;
		}

		m_textureParamTextures[String(name)] = value;
	}

	bool MaterialInstance::GetTextureParameter(std::string_view name, String& out_value)
	{
		const auto it = std::find_if(m_textureParameters.begin(), m_textureParameters.end(), [&name](const TextureParameterValue& value) { return value.name == name; });
		if (it == m_textureParameters.end())
		{
			return false;
		}

		out_value = it->texture;
		return true;
	}
}
