// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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

	void Material::SetVertexShaderCode(std::span<uint8> code) noexcept
	{
		m_vertexShaderCode.assign(code.begin(), code.end());
		m_vertexShaderChanged = true;
	}

	void Material::SetPixelShaderCode(std::span<uint8> code) noexcept
	{
		m_pixelShaderCode.assign(code.begin(), code.end());
		m_pixelShaderChanged = true;
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
					ELOG("Unable to load texture " << textureFile);
				}

				// Still add nullptr - texture won't be rendered
				m_textures.emplace_back(texture);
			}

			m_texturesChanged = false;
		}

		if (m_vertexShaderChanged)
		{
			if (!m_vertexShaderCode.empty())
			{
				m_vertexShader = GraphicsDevice::Get().CreateShader(ShaderType::VertexShader, m_vertexShaderCode.data(), m_vertexShaderCode.size());
			}
			else
			{
				m_vertexShader.reset();
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
		compiler.GenerateShaderCode(*this, shaderCompiler);

		// Compile vertex shader
		ShaderCompileResult vertexOutput;
		ShaderCompileInput vertexInput { compiler.GetVertexShaderCode(), ShaderType::VertexShader };
		shaderCompiler.Compile(vertexInput, vertexOutput);
		if (vertexOutput.succeeded)
		{
			m_vertexShaderCode = vertexOutput.code.data;
			m_vertexShader = std::move(GraphicsDevice::Get().CreateShader(ShaderType::VertexShader, &vertexOutput.code.data[0], vertexOutput.code.data.size()));
		}
		else
		{
			ELOG("Error compiling vertex shader: " << vertexOutput.errorMessage);
			return false;
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

	void Material::BindShaders(GraphicsDevice& device)
	{
		if (m_vertexShader) m_vertexShader->Set();
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
