// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material.h"
#include "material_compiler.h"
#include "graphics/graphics_device.h"
#include "graphics/shader_compiler.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	Material::Material(const std::string_view name)
		: m_name(name)
	{
	}

	void Material::ClearTextures()
	{
		m_textures.clear();
		m_textureFiles.clear();
		m_texturesChanged = true;
	}

	void Material::AddTexture(std::string_view texture)
	{
		m_textureFiles.emplace_back(texture);
		m_texturesChanged = true;
	}

	void Material::SetVertexShaderCode(VertexShaderType vertexShaderType, std::span<uint8> code) noexcept
	{
		m_vertexShaderCode[static_cast<uint32_t>(vertexShaderType)].assign(code.begin(), code.end());
		m_vertexShaderChanged = true;
	}

	void Material::SetPixelShaderCode(PixelShaderType shaderType, std::span<uint8> code) noexcept
	{
		m_pixelShaderCode[static_cast<uint32_t>(shaderType)].assign(code.begin(), code.end());
		m_pixelShaderChanged[static_cast<uint32_t>(shaderType)] = true;
	}

	ConstantBufferPtr Material::GetParameterBuffer(MaterialParameterType type, GraphicsDevice& device)
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

	void Material::ClearParameters()
	{
		m_scalarParameters.clear();
		m_vectorParameters.clear();
		m_textureParameters.clear();
		m_textureParamTextures.clear();
	}

	void Material::AddScalarParameter(std::string_view name, float defaultValue)
	{
		const auto it = std::find_if(m_scalarParameters.begin(), m_scalarParameters.end(), [&name](const ScalarParameterValue& value) { return value.name == name; });
		if (it != m_scalarParameters.end())
		{
			return;
		}

		m_scalarParameters.emplace_back(String(name), defaultValue);
		m_bufferLayoutDirty[(uint8)MaterialParameterType::Scalar] = true;
	}

	void Material::SetScalarParameter(std::string_view name, float value)
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

	bool Material::GetScalarParameter(std::string_view name, float& out_value)
	{
		const auto it = std::find_if(m_scalarParameters.begin(), m_scalarParameters.end(), [&name](const ScalarParameterValue& value) { return value.name == name; });
		if (it == m_scalarParameters.end())
		{
			return false;
		}

		out_value = it->value;
		return true;
	}

	void Material::AddVectorParameter(std::string_view name, const Vector4& defaultValue)
	{
		const auto it = std::find_if(m_vectorParameters.begin(), m_vectorParameters.end(), [&name](const VectorParameterValue& value) { return value.name == name; });
		if (it != m_vectorParameters.end())
		{
			return;
		}

		m_vectorParameters.emplace_back(String(name), defaultValue);
		m_bufferLayoutDirty[(uint8)MaterialParameterType::Vector] = true;
	}

	void Material::SetVectorParameter(std::string_view name, const Vector4& value)
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

	bool Material::GetVectorParameter(std::string_view name, Vector4& out_value)
	{
		const auto it = std::find_if(m_vectorParameters.begin(), m_vectorParameters.end(), [&name](const auto& value) { return value.name == name; });
		if (it == m_vectorParameters.end())
		{
			return false;
		}

		out_value = it->value;
		return true;
	}

	void Material::AddTextureParameter(std::string_view name, const String& defaultValue)
	{
		const auto it = std::find_if(m_textureParameters.begin(), m_textureParameters.end(), [&name](const TextureParameterValue& value) { return value.name == name; });
		if (it != m_textureParameters.end())
		{
			return;
		}

		m_textureParamTextures[String(name)] = TextureManager::Get().CreateOrRetrieve(defaultValue);
		m_textureParameters.emplace_back(String(name), defaultValue);

		m_bufferLayoutDirty[(uint8)MaterialParameterType::Texture] = true;
	}

	void Material::SetTextureParameter(std::string_view name, const String& value)
	{
		const auto it = std::find_if(m_textureParameters.begin(), m_textureParameters.end(), [&name](const TextureParameterValue& value) { return value.name == name; });
		if (it == m_textureParameters.end())
		{
			return;
		}

		if (it->texture != value)
		{
			m_bufferDataDirty[(uint8)MaterialParameterType::Texture] = true;
			it->texture = value;
			m_textureParamTextures[String(name)] = TextureManager::Get().CreateOrRetrieve(value);
		}
	}

	void Material::SetTextureParameter(std::string_view name, const TexturePtr& value)
	{
		const auto it = std::find_if(m_textureParameters.begin(), m_textureParameters.end(), [&name](const TextureParameterValue& value) { return value.name == name; });
		if (it == m_textureParameters.end())
		{
			return;
		}

		if (m_textureParamTextures[String(name)] != value)
		{
			m_bufferDataDirty[(uint8)MaterialParameterType::Texture] = true;
			m_textureParamTextures[String(name)] = value;
		}
	}

	bool Material::GetTextureParameter(std::string_view name, String& out_value)
	{
		const auto it = std::find_if(m_textureParameters.begin(), m_textureParameters.end(), [&name](const auto& value) { return value.name == name; });
		if (it == m_textureParameters.end())
		{
			return false;
		}

		out_value = it->texture;
		return true;
	}

	std::shared_ptr<Material> Material::AsShared()
	{
		return std::static_pointer_cast<Material>(shared_from_this());
	}

	void Material::Update()
	{
		if (m_texturesChanged)
		{
			m_textures.reserve(m_textureFiles.size());
			for (const auto& textureFile : m_textureFiles)
			{
				auto texture = TextureManager::Get().CreateOrRetrieve(textureFile);
				if (!texture)
				{
					ELOG("Unable to load texture " << textureFile << " (referenced by Material '" << m_name << "')");
					texture = TextureManager::Get().CreateOrRetrieve("Textures/Engine/DefaultGrid/T_Default_Material_Grid_M.htex");
				}

				// Still add nullptr - texture won't be rendered
				m_textures.emplace_back(texture);
			}

			m_texturesChanged = false;
		}

		if (m_vertexShaderChanged)
		{
			uint32 index = 0;
			for (const auto& code : m_vertexShaderCode)
			{
				if (!code.empty())
				{
					m_vertexShader[index] = GraphicsDevice::Get().CreateShader(ShaderType::VertexShader, code.data(), code.size());
				}
				else
				{
					m_vertexShader[index].reset();
				}

				index++;
			}

			m_vertexShaderChanged = false;
		}

		for (uint32 i = 0; i < 3; ++i)
		{
			if (m_pixelShaderChanged[i])
			{
				if (!m_pixelShaderCode[i].empty())
				{
					m_pixelShader[i] = GraphicsDevice::Get().CreateShader(ShaderType::PixelShader, m_pixelShaderCode[i].data(), m_pixelShaderCode[i].size());
				}
				else
				{
					m_pixelShader[i].reset();
				}

				m_pixelShaderChanged[i] = false;
			}
		}
	}

	bool Material::Compile(MaterialCompiler& compiler, ShaderCompiler& shaderCompiler)
	{
		compiler.Compile(*this, shaderCompiler);

		// Compile vertex shader
		for (uint32 i = 0; i < 4; ++i)
		{
			ShaderCompileResult vertexOutput;
			ShaderCompileInput vertexInput{ compiler.GetVertexShaderCode(), ShaderType::VertexShader };
			shaderCompiler.Compile(vertexInput, vertexOutput);
			if (vertexOutput.succeeded)
			{
				m_vertexShaderCode[i] = vertexOutput.code.data;
				m_vertexShader[i] = std::move(GraphicsDevice::Get().CreateShader(ShaderType::VertexShader, vertexOutput.code.data.data(), vertexOutput.code.data.size()));
			}
			else
			{
				ELOG("Error compiling vertex shader: " << vertexOutput.errorMessage);
				return false;
			}
		}
		

		// Compile pixel shader (forward rendering)
		ShaderCompileResult pixelOutput;
		ShaderCompileInput pixelInput { compiler.GetPixelShaderCode(PixelShaderType::Forward), ShaderType::PixelShader };
		shaderCompiler.Compile(pixelInput, pixelOutput);
		if (pixelOutput.succeeded)
		{
			m_pixelShaderCode[static_cast<uint32_t>(PixelShaderType::Forward)] = pixelOutput.code.data;
			m_pixelShader[static_cast<uint32_t>(PixelShaderType::Forward)] = std::move(GraphicsDevice::Get().CreateShader(ShaderType::PixelShader, pixelOutput.code.data.data(), pixelOutput.code.data.size()));
		}
		else
		{
			ELOG("Error compiling pixel shader: " << pixelOutput.errorMessage);
			return false;
		}
		
		return true;
	}

	void Material::Apply(GraphicsDevice& device, MaterialDomain domain, PixelShaderType pixelShaderType)
	{
		// TODO: Determine what vertex shader type we need to bind based on the rendering context or from outside
		// Apply
		if (m_vertexShader[0]) m_vertexShader[0]->Set();
		if (m_pixelShader[static_cast<uint32_t>(pixelShaderType)]) m_pixelShader[static_cast<uint32_t>(pixelShaderType)]->Set();

		if (pixelShaderType != PixelShaderType::ShadowMap)
		{
			BindTextures(device);
			
			// Bind texture parameter textures
			uint32 shaderSlot = m_textures.size();
			for (auto& param : m_textureParameters)
			{
				device.BindTexture(m_textureParamTextures[param.name], ShaderType::PixelShader, shaderSlot++);
			}
		}

		if (pixelShaderType != PixelShaderType::ShadowMap)
		{
			device.SetDepthTestComparison(m_depthTest ? DepthTestMethod::Less : DepthTestMethod::Always);
			device.SetDepthWriteEnabled(m_depthWrite);
		}
		else
		{
			device.SetDepthEnabled(true);
			device.SetDepthWriteEnabled(true);
		}

		if (m_type == MaterialType::Translucent || m_type == MaterialType::Masked)
		{
			device.SetBlendMode(BlendMode::Alpha);
		}
		else
		{
			device.SetBlendMode(BlendMode::Opaque);
		}

		if (m_twoSided || pixelShaderType == PixelShaderType::ShadowMap)
		{
			device.SetFaceCullMode(FaceCullMode::None);
		}
		else
		{
			device.SetFaceCullMode(FaceCullMode::Back);
		}

		device.SetFillMode(m_wireframe ? FillMode::Wireframe : FillMode::Solid);
	}

	void Material::BindTextures(GraphicsDevice& device)
	{
		int shaderSlot = 0;
		for (const auto& texture : m_textures)
		{
			device.BindTexture(texture, ShaderType::PixelShader, shaderSlot++);
		}
	}
}
