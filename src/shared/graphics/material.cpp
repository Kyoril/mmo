// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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

	void Material::SetPixelShaderCode(std::span<uint8> code) noexcept
	{
		m_pixelShaderCode.assign(code.begin(), code.end());
		m_pixelShaderChanged = true;
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

		const uint32 index = GetParameterIndex();
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
		return false;
	}

	void Material::AddTextureParameter(std::string_view name, TexturePtr defaultValue)
	{
		const auto it = std::find_if(m_textureParameters.begin(), m_textureParameters.end(), [&name](const TextureParameterValue& value) { return value.name == name; });
		if (it != m_textureParameters.end())
		{
			return;
		}

		const uint32 index = GetParameterIndex();
		m_textureParameters.emplace_back(String(name), defaultValue);
	}

	void Material::SetTextureParameter(std::string_view name, TexturePtr value)
	{
	}

	bool Material::GetTextureParameter(std::string_view name, TexturePtr& out_value)
	{
		return false;
	}

	uint32 Material::GetParameterIndex()
	{
		return m_scalarParameters.size() + m_vectorParameters.size() + m_textureParameters.size();
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

		if (m_pixelShaderChanged)
		{
			if (!m_pixelShaderCode.empty())
			{
				m_pixelShader = GraphicsDevice::Get().CreateShader(ShaderType::PixelShader, m_pixelShaderCode.data(), m_pixelShaderCode.size());
			}
			else
			{
				m_pixelShader.reset();
			}

			m_pixelShaderChanged = false;
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
		

		// Compile pixel shader
		ShaderCompileResult pixelOutput;
		ShaderCompileInput pixelInput { compiler.GetPixelShaderCode(), ShaderType::PixelShader };
		shaderCompiler.Compile(pixelInput, pixelOutput);
		if (pixelOutput.succeeded)
		{
			m_pixelShaderCode = pixelOutput.code.data;
		}
		else
		{
			ELOG("Error compiling pixel shader: " << pixelOutput.errorMessage);
			return false;
		}
		
		return true;
	}

	void Material::Apply(GraphicsDevice& device)
	{
		// TODO: Determine what vertex shader type we need to bind based on the rendering context or from outside

		BindShaders(device);
		BindTextures(device);

		device.SetDepthTestComparison(m_depthTest ? DepthTestMethod::Less : DepthTestMethod::Always);
		device.SetDepthWriteEnabled(m_depthWrite);

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
	}

	void Material::BindShaders(GraphicsDevice& device)
	{
		// Apply

		if (m_vertexShader[0]) m_vertexShader[0]->Set();
		if (m_pixelShader) m_pixelShader->Set();
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
