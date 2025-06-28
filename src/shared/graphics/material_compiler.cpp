// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_compiler.h"
#include "material.h"
#include "graphics/shader_compiler.h"
#include "log/default_log_levels.h"

namespace mmo
{
	uint32 GetExpressionTypeComponentCount(const ExpressionType type)
	{
		switch(type)
		{
		case ExpressionType::Float_1: return 1;
		case ExpressionType::Float_2: return 2;
		case ExpressionType::Float_3: return 3;
		case ExpressionType::Float_4: return 4;
		default:
			return 0;
		}
	}

	void MaterialCompiler::Compile(Material& material, ShaderCompiler& shaderCompiler)
	{
		m_material = &material;
		m_material->ClearParameters();

		if (m_userInterface)
		{
			material.SetType(MaterialType::UserInterface);
		}
		else
		{
			if (m_lit)
			{
				material.SetType(m_translucent ? MaterialType::Translucent : MaterialType::Opaque);
			}
			else
			{
				material.SetType(MaterialType::Unlit);
			}
		}

		material.SetDepthWriteEnabled(m_depthWrite);
		material.SetDepthTestEnabled(m_depthTest);
		material.SetTwoSided(m_twoSided);

		for (uint32 i = 0; i < 5; ++i)
		{
			m_vertexShaderCode.clear();
			GenerateVertexShaderCode(static_cast<VertexShaderType>(i));

			ShaderCompileInput vertexInput;
			vertexInput.shaderCode = m_vertexShaderCode;
			vertexInput.shaderType = ShaderType::VertexShader;
			ShaderCompileResult vertexOutput;
			shaderCompiler.Compile(vertexInput, vertexOutput);

			if (!vertexOutput.succeeded)
			{
				ELOG("Error compiling vertex shader: " << vertexOutput.errorMessage);
			}
			else
			{
				DLOG("Successfully compiled vertex shader. Size: " << vertexOutput.code.data.size());
			}

			material.SetVertexShaderCode(static_cast<VertexShaderType>(i), { vertexOutput.code.data });
		}
		
		// Generate forward rendering pixel shader
		GeneratePixelShaderCode(PixelShaderType::Forward);

		ShaderCompileInput pixelInput;
		pixelInput.shaderCode = m_pixelShaderCode[(int)PixelShaderType::Forward];
		pixelInput.shaderType = ShaderType::PixelShader;
		ShaderCompileResult pixelOutput;
		shaderCompiler.Compile(pixelInput, pixelOutput);

		if (!pixelOutput.succeeded)
		{
			ELOG("Error compiling forward pixel shader: " << pixelOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled forward pixel shader. Size: " << pixelOutput.code.data.size());
			// Add shader code to the material
			material.SetPixelShaderCode(PixelShaderType::Forward, {pixelOutput.code.data });
		}

		// Generate GBuffer pixel shader
		GeneratePixelShaderCode(PixelShaderType::GBuffer);

		pixelInput.shaderCode = m_pixelShaderCode[(int)PixelShaderType::GBuffer];
		pixelInput.shaderType = ShaderType::PixelShader;
		shaderCompiler.Compile(pixelInput, pixelOutput);

		if (!pixelOutput.succeeded)
		{
			ELOG("Error compiling GBuffer pixel shader: " << pixelOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled GBuffer pixel shader. Size: " << pixelOutput.code.data.size());
			// Add shader code to the material
			material.SetPixelShaderCode(PixelShaderType::GBuffer, {pixelOutput.code.data });
		}

		// Generate Shadowmap pixel shader
		GeneratePixelShaderCode(PixelShaderType::ShadowMap);

		pixelInput.shaderCode = m_pixelShaderCode[(int)PixelShaderType::ShadowMap];
		pixelInput.shaderType = ShaderType::PixelShader;
		shaderCompiler.Compile(pixelInput, pixelOutput);

		if (!pixelOutput.succeeded)
		{
			ELOG("Error compiling ShadowMap pixel shader: " << pixelOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled ShadowMap pixel shader. Size: " << pixelOutput.code.data.size());
			// Add shader code to the material
			material.SetPixelShaderCode(PixelShaderType::ShadowMap, { pixelOutput.code.data });
		}

		// Generate UI pixel shader
		GeneratePixelShaderCode(PixelShaderType::UI);

		pixelInput.shaderCode = m_pixelShaderCode[(int)PixelShaderType::UI];
		pixelInput.shaderType = ShaderType::PixelShader;
		shaderCompiler.Compile(pixelInput, pixelOutput);

		if (!pixelOutput.succeeded)
		{
			ELOG("Error compiling UI pixel shader: " << pixelOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled UI pixel shader. Size: " << pixelOutput.code.data.size());
			// Add shader code to the material
			material.SetPixelShaderCode(PixelShaderType::UI, { pixelOutput.code.data });
		}

		// Set material textures
		material.ClearTextures();
		for (const auto& texture : m_textures)
		{
			material.AddTexture(texture);
		}

		m_material = nullptr;
	}
	
	ExpressionType MaterialCompiler::GetExpressionType(ExpressionIndex index)
	{
		if (index < 0 || index >= m_expressionTypes.size())
		{
			return ExpressionType::Unknown;
		}

		return m_expressionTypes[index];
	}

	void MaterialCompiler::SetBaseColorExpression(ExpressionIndex expression)
	{
		m_baseColorExpression = expression;
	}

	void MaterialCompiler::SetEmissiveExpression(ExpressionIndex expression)
	{
		m_emissiveExpression = expression;
	}

	void MaterialCompiler::SetMetallicExpression(ExpressionIndex expression)
	{
		m_metallicExpression = expression;
	}

	void MaterialCompiler::SetRoughnessExpression(ExpressionIndex expression)
	{
		m_roughnessExpression = expression;
	}

	void MaterialCompiler::SetSpecularExpression(ExpressionIndex expression)
	{
		m_specularExpression = expression;
	}

	void MaterialCompiler::SetNormalExpression(ExpressionIndex expression)
	{
		m_normalExpression = expression;
	}

	void MaterialCompiler::SetAmbientOcclusionExpression(ExpressionIndex expression)
	{
		m_ambientOcclusionExpression = expression;
	}

	void MaterialCompiler::SetOpacityExpression(ExpressionIndex expression)
	{
		m_opacityExpression = expression;
	}
}
