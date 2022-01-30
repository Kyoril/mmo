// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_compiler.h"
#include "material.h"
#include "graphics/shader_compiler.h"
#include "log/default_log_levels.h"

namespace mmo
{
	void MaterialCompiler::GenerateShaderCode(Material& material, ShaderCompiler& compiler)
	{
		GenerateVertexShaderCode();
		GeneratePixelShaderCode();

		ShaderCompileInput vertexInput;
		vertexInput.shaderCode = m_vertexShaderCode;
		vertexInput.shaderType = ShaderType::VertexShader;
		ShaderCompileResult vertexOutput;
		compiler.Compile(vertexInput, vertexOutput);

		if (!vertexOutput.succeeded)
		{
			ELOG("Error compiling vertex shader: " << vertexOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled vertex shader. Size: " << vertexOutput.code.data.size());
		}
		
		ShaderCompileInput pixelInput;
		pixelInput.shaderCode = m_pixelShaderCode;
		pixelInput.shaderType = ShaderType::PixelShader;
		ShaderCompileResult pixelOutput;
		compiler.Compile(pixelInput, pixelOutput);

		if (!pixelOutput.succeeded)
		{
			ELOG("Error compiling pixel shader: " << pixelOutput.errorMessage);
		}
		else
		{
			DLOG("Successfully compiled pixel shader. Size: " << pixelOutput.code.data.size());
		}

		// Set material textures
		material.ClearTextures();
		for (const auto& texture : m_textures)
		{
			material.AddTexture(texture);
		}

		// Add shader code to the material
		material.SetVertexShaderCode({vertexOutput.code.data.begin(), vertexOutput.code.data.end() });
		material.SetPixelShaderCode({pixelOutput.code.data.begin(), pixelOutput.code.data.end() });
	}
	
	void MaterialCompiler::AddGlobalFunction(std::string_view name, std::string_view code)
	{
		m_globalFunctions.emplace(name, code);
	}

	int32 MaterialCompiler::AddExpression(std::string_view code)
	{
		const int32 id = m_expressions.size();

		std::ostringstream strm;
		strm << "float4 expr_" << id << " = " << code << ";\n\n";
		strm.flush();

		m_expressions.emplace_back(strm.str());

		return id;
	}
	
	void MaterialCompiler::NotifyTextureCoordinateIndex(const uint32 texCoordIndex)
	{
		m_numTexCoordinates = std::max(texCoordIndex + 1, m_numTexCoordinates);
	}

	void MaterialCompiler::SetBaseColorExpression(int32 expression)
	{
		m_baseColorExpression = expression;
	}

	int32 MaterialCompiler::AddTextureCoordinate(int32 coordinateIndex)
	{
		if (coordinateIndex >= 8)
		{
			return IndexNone;
		}

		NotifyTextureCoordinateIndex(coordinateIndex);

		std::ostringstream strm;
		strm << "float4(input.uv" << coordinateIndex << ", 0.0, 0.0)";
		strm.flush();

		return AddExpression(strm.str());
	}

	int32 MaterialCompiler::AddTextureSample(std::string_view texture, const int32 coordinates)
	{
		if (texture.empty())
		{
			WLOG("Trying to sample empty texture");
			return IndexNone;
		}

		int32 textureIndex;
		for (textureIndex = 0; textureIndex < m_textures.size(); ++textureIndex)
		{
			if (m_textures[textureIndex] == texture)
			{
				break;
			}
		}

		if (textureIndex == m_textures.size())
		{
			m_textures.emplace_back(texture);
		}
		
		std::ostringstream strm;
		strm << "tex" << textureIndex << ".Sample(sampler" << textureIndex << ", ";
		if (coordinates == IndexNone)
		{
			strm << "input.uv0";
		}
		else
		{
			strm << "expr_" << coordinates << ".xy";
		}
		strm << ")";
		strm.flush();

		return AddExpression(strm.str());
	}

	auto MaterialCompiler::AddMultiply(int32 first, const int32 second) -> int32
	{
		if (first == IndexNone)
		{
			WLOG("Missing first parameter for multiplication");
			return IndexNone;
		}

		if (second == IndexNone)
		{
			WLOG("Missing second parameter for multiplication");
			return IndexNone;
		}

		std::ostringstream strm;
		strm << "expr_" << first << " * expr_" << second;
		strm.flush();

		return AddExpression(strm.str());
	}

	int32 MaterialCompiler::AddAddition(int32 first, int32 second)
	{
		if (first == IndexNone)
		{
			WLOG("Missing first parameter for addition");
			return IndexNone;
		}

		if (second == IndexNone)
		{
			WLOG("Missing second parameter for addition");
			return IndexNone;
		}

		std::ostringstream strm;
		strm << "expr_" << first << " + expr_" << second;
		strm.flush();

		return AddExpression(strm.str());
	}

	int32 MaterialCompiler::AddLerp(int32 first, int32 second, int32 alpha)
	{
		if (first == IndexNone)
		{
			WLOG("Missing first parameter for lerp");
			return IndexNone;
		}

		if (second == IndexNone)
		{
			WLOG("Missing second parameter for lerp");
			return IndexNone;
		}

		if (alpha == IndexNone)
		{
			WLOG("Missing alpha parameter for lerp");
			return IndexNone;
		}

		std::ostringstream strm;
		strm << "lerp(expr_" << first << ", expr_" << second << ", expr_" << alpha << ")";
		strm.flush();

		return AddExpression(strm.str());
	}

	void MaterialCompiler::GenerateVertexShaderCode()
	{
		m_vertexShaderStream.clear();
		
		// VertexIn struct
		m_vertexShaderStream
			<< "struct VertexIn\n"
			<< "{\n"
			<< "\tfloat4 pos : SV_POSITION;\n"
			<< "\tfloat4 color : COLOR;\n"
			<< "\tfloat3 normal : NORMAL;\n";

		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_vertexShaderStream
				<< "\tfloat2 uv" << i << " : TEXCOORD" << i << ";\n";
		}

		m_vertexShaderStream
			<< "};\n\n";
		
		// VertexOut struct
		m_vertexShaderStream
			<< "struct VertexOut\n"
			<< "{\n"
			<< "\tfloat4 pos : SV_POSITION;\n"
			<< "\tfloat4 color : COLOR;\n"
			<< "\tfloat3 normal : NORMAL;\n";
		
		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_vertexShaderStream
				<< "\tfloat2 uv" << i << " : TEXCOORD" << i << ";\n";
		}

		m_vertexShaderStream
			<< "};\n\n";

		// Matrix constant buffer
		m_vertexShaderStream
			<< "cbuffer Matrices\n"
			<< "{\n"
			<< "\tcolumn_major matrix matWorld;\n"
			<< "\tcolumn_major matrix matView;\n"
			<< "\tcolumn_major matrix matProj;\n"
			<< "};\n\n";

		// Main procedure start
		m_vertexShaderStream
			<< "VertexOut main(VertexIn input)\n"
			<< "{\n"
			<< "\tVertexOut output;\n\n";

		// TODO: Extend with custom code

		// Basic transformations
		m_vertexShaderStream
			<< "\tinput.pos.w = 1.0;\n"
			<< "\toutput.pos = mul(input.pos, matWorld);\n"
			<< "\toutput.pos = mul(output.pos, matView);\n"
			<< "\toutput.pos = mul(output.pos, matProj);\n"
			<< "\toutput.color = input.color;\n";
		
		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_vertexShaderStream
				<< "\toutput.uv" << i << " = input.uv" << i << ";\n";
		}

		m_vertexShaderStream
			<< "\toutput.normal = mul(input.normal, (float3x3)matWorld);\n"
			<< "\toutput.normal = normalize(output.normal);\n";

		// Main procedure end
		m_vertexShaderStream
			<< "\n\treturn output;\n"
			<< "}\n"
			<< std::endl;
		
		m_vertexShaderCode = m_vertexShaderStream.str();
		m_vertexShaderStream.clear();
	}

	void MaterialCompiler::GeneratePixelShaderCode()
	{
		m_pixelShaderStream.clear();
		
		// VertexOut struct
		m_pixelShaderStream
			<< "struct VertexOut\n"
			<< "{\n"
			<< "\tfloat4 pos : SV_POSITION;\n"
			<< "\tfloat4 color : COLOR;\n"
			<< "\tfloat3 normal : NORMAL;\n";
		
		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_pixelShaderStream
				<< "\tfloat2 uv" << i << " : TEXCOORD" << i << ";\n";
		}

		m_pixelShaderStream << "};\n\n";

		// Add texture samplers
		for (size_t i = 0; i < m_textures.size(); ++i)
		{
			m_pixelShaderStream << "// " << m_textures[i] << "\n";
			m_pixelShaderStream << "Texture2D tex" << i << ";\n";
			m_pixelShaderStream << "SamplerState sampler" << i << ";\n\n";
		}

		for (const auto& [name, code] : m_globalFunctions)
		{
			m_pixelShaderStream
			<< "float4 " << name << "(VertexOut input)\n"
			<< "{\n"
			<< code << "\n"
			<< "}\n\n";
		}
		
		// Start of main function
		m_pixelShaderStream
			<< "float4 main(VertexOut input) : SV_Target\n"
			<< "{\n"
			<< "\tfloat4 outputColor = float4(1, 1, 1, 1);\n\n";

		// Lighting base
		m_pixelShaderStream
			<< "\tfloat3 lightDir = normalize(-float3(1.0, -0.5, 1.0));\n"
			<< "\tfloat4 ambient = float4(0.05, 0.15, 0.25, 1.0);\n\n";

		// Light intensity expression
		m_pixelShaderStream
		<< "\tfloat4 lightIntensity = saturate(dot(input.normal, lightDir));\n\n";

		// BaseColor base
		m_pixelShaderStream
		<< "\tfloat4 baseColor = float4(1.0, 1.0, 1.0, 1.0);\n\n";
		
		// BaseColor expression?
		if (m_baseColorExpression != IndexNone)
		{
			for (const auto& code : m_expressions)
			{
				m_pixelShaderStream << "\t" << code;
			}

			m_pixelShaderStream << "\tbaseColor = expr_" << m_baseColorExpression << ";\n\n";
		}
		
		// Combining it
		m_pixelShaderStream
		<< "\toutputColor = (ambient + float4(saturate(input.color * lightIntensity).xyz, 1.0)) * baseColor;\n";

		// End of main function
		m_pixelShaderStream
			<< "\treturn outputColor;\n"
			<< "}"
			<< std::endl;
		
		m_pixelShaderCode = m_pixelShaderStream.str();
		m_pixelShaderStream.clear();
	}
}
