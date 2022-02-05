// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_compiler_d3d11.h"
#include "graphics/material.h"
#include "graphics/shader_compiler.h"
#include "log/default_log_levels.h"

namespace mmo
{
	void MaterialCompilerD3D11::AddGlobalFunction(const std::string_view name, const std::string_view code)
	{
		m_globalFunctions.emplace(name, code);
	}

	ExpressionIndex MaterialCompilerD3D11::AddExpression(const std::string_view code)
	{
		const int32 id = m_expressions.size();

		std::ostringstream outputStream;
		outputStream << "float4 expr_" << id << " = " << code << ";\n\n";
		outputStream.flush();

		m_expressions.emplace_back(outputStream.str());

		return id;
	}
	
	void MaterialCompilerD3D11::NotifyTextureCoordinateIndex(const uint32 textureCoordinateIndex)
	{
		m_numTexCoordinates = std::max(textureCoordinateIndex + 1, m_numTexCoordinates);
	}

	void MaterialCompilerD3D11::SetBaseColorExpression(const ExpressionIndex expression)
	{
		m_baseColorExpression = expression;
	}

	ExpressionIndex MaterialCompilerD3D11::AddTextureCoordinate(const int32 coordinateIndex)
	{
		if (coordinateIndex >= 8)
		{
			return IndexNone;
		}

		NotifyTextureCoordinateIndex(coordinateIndex);

		std::ostringstream outputStream;
		outputStream << "float4(input.uv" << coordinateIndex << ", 0.0, 0.0)";
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddTextureSample(std::string_view texture, const ExpressionIndex coordinates)
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
		
		std::ostringstream outputStream;
		outputStream << "tex" << textureIndex << ".Sample(sampler" << textureIndex << ", ";
		if (coordinates == IndexNone)
		{
			outputStream << "input.uv0";
		}
		else
		{
			outputStream << "expr_" << coordinates << ".xy";
		}
		outputStream << ")";
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddMultiply(const ExpressionIndex first, const ExpressionIndex second)
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

		std::ostringstream outputStream;
		outputStream << "expr_" << first << " * expr_" << second;
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddAddition(const ExpressionIndex first, const ExpressionIndex second)
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

		std::ostringstream outputStream;
		outputStream << "expr_" << first << " + expr_" << second;
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddLerp(const ExpressionIndex first, const ExpressionIndex second, const ExpressionIndex alpha)
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

		std::ostringstream outputStream;
		outputStream << "lerp(expr_" << first << ", expr_" << second << ", expr_" << alpha << ")";
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddDot(const ExpressionIndex first, const ExpressionIndex second)
	{
		if (first == IndexNone)
		{
			WLOG("Missing first parameter for dot");
			return IndexNone;
		}

		if (second == IndexNone)
		{
			WLOG("Missing second parameter for dot");
			return IndexNone;
		}

		std::ostringstream outputStream;
		outputStream << "dot(expr_" << first << ", expr_" << second << ")";
		outputStream.flush();

		return AddExpression(outputStream.str());
	}
		
	ExpressionIndex MaterialCompilerD3D11::AddClamp(const ExpressionIndex value, const ExpressionIndex min, const ExpressionIndex max)
	{
		if (value == IndexNone)
		{
			WLOG("Missing value parameter for clamp");
			return IndexNone;
		}

		if (min == IndexNone)
		{
			WLOG("Missing min parameter for clamp");
			return IndexNone;
		}

		if (max == IndexNone)
		{
			WLOG("Missing max parameter for clamp");
			return IndexNone;
		}

		std::ostringstream outputStream;
		outputStream << "clamp(expr_" << value << ", expr_" << min << ", expr_" << max << ")";
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddOneMinus(const ExpressionIndex input)
	{
		if (input == IndexNone)
		{
			WLOG("Missing input parameter for one minus");
			return IndexNone;
		}
		
		std::ostringstream outputStream;
		outputStream << "1.0 - expr_" << input;
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddPower(const ExpressionIndex base, const ExpressionIndex exponent)
	{
		if (base == IndexNone)
		{
			WLOG("Missing base parameter for power");
			return IndexNone;
		}

		if (exponent == IndexNone)
		{
			WLOG("Missing exponent parameter for power");
			return IndexNone;
		}

		std::ostringstream outputStream;
		outputStream << "pow(expr_" << base << ", expr_" << exponent << ")";
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddWorldPosition()
	{
		std::ostringstream outputStream;
		outputStream << "input.worldPos";
		outputStream.flush();
		
		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddMask(const ExpressionIndex input, const bool r, const bool g, const bool b, const bool a)
	{
		if (input == IndexNone)
		{
			WLOG("Missing base parameter for power");
			return IndexNone;
		}

		uint32 channelCount = 0;
		char channels[4] = { 'x', 'x', 'x', 'x' };
		if (r) channels[channelCount++] = 'r';
		if (g) channels[channelCount++] = 'g';
		if (b) channels[channelCount++] = 'b';
		if (a) channels[channelCount++] = 'a';

		if (channelCount == 0)
		{
			WLOG("No channel enabled in mask expression, invalid");
			return IndexNone;
		}
		
		std::ostringstream outputStream;
		outputStream << "expr_" << input << ".";

		for (int i = 0; i < 4; ++i)
		{
			if (channels[i] != 'x')
			{
				outputStream << channels[i];
			}
		}
		
		outputStream.flush();
		
		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddVertexNormal()
	{
		std::ostringstream outputStream;
		outputStream << "float4(input.normal, 0.0)";
		outputStream.flush();
		
		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddDivide(const ExpressionIndex first, const ExpressionIndex second)
	{
		if (first == IndexNone)
		{
			WLOG("Missing first parameter for divide");
			return IndexNone;
		}

		if (second == IndexNone)
		{
			WLOG("Missing second parameter for divide");
			return IndexNone;
		}

		std::ostringstream outputStream;
		outputStream << "expr_" << first << " / expr_" << second;
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	ExpressionIndex MaterialCompilerD3D11::AddAbs(const ExpressionIndex input)
	{
		if (input == IndexNone)
		{
			WLOG("Missing input parameter for abs");
			return IndexNone;
		}
		
		std::ostringstream outputStream;
		outputStream << "abs(expr_" << input << ")";
		outputStream.flush();

		return AddExpression(outputStream.str());
	}

	void MaterialCompilerD3D11::GenerateVertexShaderCode()
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
			<< "\tfloat4 worldPos : TEXCOORD" << m_numTexCoordinates << ";\n";

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
		
		// Basic transformations
		m_vertexShaderStream
			<< "\tinput.pos.w = 1.0;\n"
			<< "\toutput.pos = mul(input.pos, matWorld);\n"
			<< "\toutput.worldPos = output.pos;\n"
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

	void MaterialCompilerD3D11::GeneratePixelShaderCode()
	{
		m_pixelShaderStream.clear();
		
		// VertexOut struct
		m_pixelShaderStream
			<< "struct VertexOut\n"
			<< "{\n"
			<< "\tfloat4 pos : SV_POSITION0;\n"
			<< "\tfloat4 color : COLOR;\n"
			<< "\tfloat3 normal : NORMAL;\n";
		
		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_pixelShaderStream
				<< "\tfloat2 uv" << i << " : TEXCOORD" << i << ";\n";
		}
		m_pixelShaderStream << "\tfloat4 worldPos : TEXCOORD"<< m_numTexCoordinates << ";\n";
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

		if (m_lit)
		{
			// Lighting base
			m_pixelShaderStream
				<< "\tfloat3 lightDir = normalize(-float3(1.0, -0.5, 1.0));\n"
				<< "\tfloat4 ambient = float4(0.05, 0.15, 0.25, 1.0);\n\n";

			// Light intensity expression
			m_pixelShaderStream
				<< "\tfloat4 lightIntensity = saturate(dot(input.normal, lightDir));\n\n";
		}
		
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
		if (m_lit)
		{
			m_pixelShaderStream
				<< "\toutputColor = (ambient + float4(saturate(input.color * lightIntensity).xyz, 1.0)) * baseColor;\n";
		}
		else
		{
			m_pixelShaderStream
				<< "\toutputColor = saturate(input.color * baseColor);\n";
		}

		// End of main function
		m_pixelShaderStream
			<< "\treturn outputColor;\n"
			<< "}"
			<< std::endl;
		
		m_pixelShaderCode = m_pixelShaderStream.str();
		m_pixelShaderStream.clear();
	}
}
