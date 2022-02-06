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

	ExpressionIndex MaterialCompilerD3D11::AddExpression(const std::string_view code, ExpressionType type)
	{
		if (type == ExpressionType::Unknown)
		{
			ELOG("Unknown expression type!");
			return IndexNone;
		}

		const int32 id = m_expressions.size();

		std::ostringstream outputStream;
		switch(type)
		{
		case ExpressionType::Float_1:
			outputStream << "float";
			break;
		case ExpressionType::Float_2:
			outputStream << "float2";
			break;
		case ExpressionType::Float_3:
			outputStream << "float3";
			break;
		case ExpressionType::Float_4:
			outputStream << "float4";
			break;
		}

		outputStream << " expr_" << id << " = " << code << ";\n\n";
		outputStream.flush();

		m_expressions.emplace_back(outputStream.str());
		m_expressionTypes.emplace_back(type);

		return id;
	}
	
	void MaterialCompilerD3D11::NotifyTextureCoordinateIndex(const uint32 textureCoordinateIndex)
	{
		m_numTexCoordinates = std::max(textureCoordinateIndex + 1, m_numTexCoordinates);
	}
	
	ExpressionIndex MaterialCompilerD3D11::AddTextureCoordinate(const int32 coordinateIndex)
	{
		if (coordinateIndex >= 8)
		{
			return IndexNone;
		}

		NotifyTextureCoordinateIndex(coordinateIndex);

		std::ostringstream outputStream;
		outputStream << "input.uv" << coordinateIndex;
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_2);
	}

	ExpressionIndex MaterialCompilerD3D11::AddTextureSample(std::string_view texture, const ExpressionIndex coordinates, bool srgb)
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
		if (srgb)
		{
			outputStream << "pow(";
		}

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

		if (srgb)
		{
			outputStream << ", 2.2)";
		}

		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_4);
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
		
		const ExpressionType firstType = GetExpressionType(first);
		const ExpressionType secondType = GetExpressionType(second);

		std::ostringstream outputStream;
		outputStream << "expr_" << first << " * expr_" << second;
		outputStream.flush();

		return AddExpression(outputStream.str(), std::max(firstType, secondType));
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
		
		const ExpressionType targetType = GetExpressionType(first);

		std::ostringstream outputStream;
		outputStream << "expr_" << first << " + expr_" << second;
		outputStream.flush();

		return AddExpression(outputStream.str(), targetType);
	}

	ExpressionIndex MaterialCompilerD3D11::AddSubtract(const ExpressionIndex first, const ExpressionIndex second)
	{
		if (first == IndexNone)
		{
			WLOG("Missing first parameter for subtract");
			return IndexNone;
		}

		if (second == IndexNone)
		{
			WLOG("Missing second parameter for subtract");
			return IndexNone;
		}
		
		const ExpressionType targetType = GetExpressionType(first);

		std::ostringstream outputStream;
		outputStream << "expr_" << first << " - expr_" << second;
		outputStream.flush();

		return AddExpression(outputStream.str(), targetType);
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
		
		const ExpressionType targetType = GetExpressionType(first);

		std::ostringstream outputStream;
		outputStream << "lerp(expr_" << first << ", expr_" << second << ", expr_" << alpha << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), targetType);
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
		
		const ExpressionType targetType = GetExpressionType(first);

		std::ostringstream outputStream;
		outputStream << "dot(expr_" << first << ", expr_" << second << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), targetType);
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
		
		const ExpressionType valueType = GetExpressionType(value);

		std::ostringstream outputStream;
		outputStream << "clamp(expr_" << value << ", expr_" << min << ", expr_" << max << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), valueType);
	}

	ExpressionIndex MaterialCompilerD3D11::AddOneMinus(const ExpressionIndex input)
	{
		if (input == IndexNone)
		{
			WLOG("Missing input parameter for one minus");
			return IndexNone;
		}
		
		const ExpressionType valueType = GetExpressionType(input);

		std::ostringstream outputStream;
		outputStream << "1.0 - expr_" << input;
		outputStream.flush();

		return AddExpression(outputStream.str(), valueType);
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
		
		const ExpressionType valueType = GetExpressionType(base);

		std::ostringstream outputStream;
		outputStream << "pow(expr_" << base << ", expr_" << exponent << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), valueType);
	}

	ExpressionIndex MaterialCompilerD3D11::AddWorldPosition()
	{
		std::ostringstream outputStream;
		outputStream << "input.worldPos.xyz";
		outputStream.flush();
		
		return AddExpression(outputStream.str(), ExpressionType::Float_3);
	}
	
	ExpressionIndex MaterialCompilerD3D11::AddCameraVector()
	{
		std::ostringstream outputStream;
		outputStream << "V";
		outputStream.flush();
		
		return AddExpression(outputStream.str(), ExpressionType::Float_3);
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

		auto type = ExpressionType::Unknown;
		switch(channelCount)
		{
		case 1:
			type = ExpressionType::Float_1;
			break;
		case 2:
			type = ExpressionType::Float_2;
			break;
		case 3:
			type = ExpressionType::Float_3;
			break;
		case 4:
			type = ExpressionType::Float_4;
			break;
		}

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
		
		return AddExpression(outputStream.str(), type);
	}

	ExpressionIndex MaterialCompilerD3D11::AddVertexNormal()
	{
		std::ostringstream outputStream;
		outputStream << "input.normal";
		outputStream.flush();
		
		return AddExpression(outputStream.str(), ExpressionType::Float_3);
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
		
		const ExpressionType valueType = GetExpressionType(first);

		std::ostringstream outputStream;
		outputStream << "expr_" << first << " / expr_" << second;
		outputStream.flush();

		return AddExpression(outputStream.str(), valueType);
	}

	ExpressionIndex MaterialCompilerD3D11::AddAbs(const ExpressionIndex input)
	{
		if (input == IndexNone)
		{
			WLOG("Missing input parameter for abs");
			return IndexNone;
		}
		
		const ExpressionType valueType = GetExpressionType(input);

		std::ostringstream outputStream;
		outputStream << "abs(expr_" << input << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), valueType);
	}

	ExpressionIndex MaterialCompilerD3D11::AddNormalize(const ExpressionIndex input)
	{
		if (input == IndexNone)
		{
			WLOG("Missing input parameter for normalize");
			return IndexNone;
		}
		
		const ExpressionType valueType = GetExpressionType(input);
		if (valueType == ExpressionType::Float_1)
		{
			ELOG("Input of normalize must be a vector, but scalar was provided");
			return IndexNone;
		}

		std::ostringstream outputStream;
		outputStream << "normalize(expr_" << input << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), valueType);
	}

	ExpressionIndex MaterialCompilerD3D11::AddVertexColor()
	{
		std::ostringstream outputStream;
		outputStream << "input.color";
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_4);
	}

	ExpressionIndex MaterialCompilerD3D11::AddAppend(ExpressionIndex first, ExpressionIndex second)
	{
		if (first == IndexNone)
		{
			WLOG("Missing first input parameter of append");
			return IndexNone;
		}

		if (second == IndexNone)
		{
			WLOG("Missing second input parameter of append");
			return IndexNone;
		}
		
		const ExpressionType sourceType = GetExpressionType(first);
		const ExpressionType targetType = GetExpressionType(first);

		const uint32 sourceComponentCount = GetExpressionTypeComponentCount(sourceType);
		const uint32 targetComponentCount = GetExpressionTypeComponentCount(targetType);
		const uint32 totalComponentCount = sourceComponentCount + targetComponentCount;

		auto outputType = ExpressionType::Unknown;
		switch(totalComponentCount)
		{
		case 1:
			outputType = ExpressionType::Float_1;
			break;
		case 2:
			outputType = ExpressionType::Float_2;
			break;
		case 3:
			outputType = ExpressionType::Float_3;
			break;
		case 4:
			outputType = ExpressionType::Float_4;
			break;
		default:
			ELOG("Unsupported amount of output components (" << totalComponentCount << ")!");
			return IndexNone;
		}

		std::ostringstream outputStream;
		outputStream << "float" << totalComponentCount << "(expr_" << first << ", expr_" << second << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), outputType);
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
			<< "\tfloat3 worldPos : TEXCOORD" << m_numTexCoordinates << ";\n"
			<< "\tfloat3 viewDir : TEXCOORD" << m_numTexCoordinates + 1 << ";\n";

		m_vertexShaderStream
			<< "};\n\n";

		// Matrix constant buffer
		m_vertexShaderStream
			<< "cbuffer Matrices\n"
			<< "{\n"
			<< "\tcolumn_major matrix matWorld;\n"
			<< "\tcolumn_major matrix matView;\n"
			<< "\tcolumn_major matrix matProj;\n"
			<< "\tcolumn_major matrix matInvView;\n"
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
			<< "\toutput.worldPos = output.pos.xyz;\n"
			<< "\toutput.viewDir = normalize(matInvView[3].xyz - output.worldPos);\n"
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

		m_pixelShaderStream
			<< "static const float PI = 3.14159265359;\n\n";

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
		m_pixelShaderStream << "\tfloat3 worldPos : TEXCOORD"<< m_numTexCoordinates << ";\n";
		m_pixelShaderStream << "\tfloat3 viewDir : TEXCOORD"<< m_numTexCoordinates + 1 << ";\n";
		m_pixelShaderStream << "};\n\n";

		// Add texture samplers
		for (size_t i = 0; i < m_textures.size(); ++i)
		{
			m_pixelShaderStream << "// " << m_textures[i] << "\n";
			m_pixelShaderStream << "Texture2D tex" << i << ";\n";
			m_pixelShaderStream << "SamplerState sampler" << i << ";\n\n";
		}

		// tangent normal to world space normal
		m_pixelShaderStream
			<< "float3 getNormalFromMap(float3 WorldPos, float2 TexCoords, float3 Normal, float3 inTangentNormal)\n"
			<< "{\n"
			<< "\tfloat3 tangentNormal = inTangentNormal * 2.0 - 1.0;\n"
			<< "\tfloat3 Q1  = ddx(WorldPos);\n"
			<< "\tfloat3 Q2  = ddy(WorldPos);\n"
			<< "\tfloat2 st1 = ddx(TexCoords);\n"
			<< "\tfloat2 st2 = ddy(TexCoords);\n\n"
			<< "\tfloat3 N   = normalize(Normal);\n"
			<< "\tfloat3 T  = normalize(Q1*st2.y - Q2*st1.y);\n"
			<< "\tfloat3 B  = -normalize(cross(N, T));\n"
			<< "\tfloat3x3 TBN = float3x3(T, B, N);\n\n"
			<< "\treturn normalize(mul(tangentNormal, TBN));\n"
			<< "}\n\n";
		
		// fresnelSchlick
		m_pixelShaderStream
			<< "float3 fresnelSchlick(float cosTheta, float3 F0)\n"
			<< "{\n"
			<< "\treturn F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n\n"
			<< "}\n\n";

		// DistributionGGX
		m_pixelShaderStream
			<< "float DistributionGGX(float3 N, float3 H, float roughness)\n"
			<< "{\n"
			<< "\tfloat a      = roughness*roughness;\n"
			<< "\tfloat a2     = a*a;\n"
			<< "\tfloat NdotH  = max(dot(N, H), 0.0);\n"
			<< "\tfloat NdotH2 = NdotH*NdotH;\n\n"
			<< "\tfloat num   = a2;\n"
			<< "\tfloat denom = (NdotH2 * (a2 - 1.0) + 1.0);\n"
			<< "\tdenom = PI * denom * denom;\n"
			<< "\treturn num / denom;\n"
			<< "}\n\n";

		// GeometrySchlickGGX
		m_pixelShaderStream
			<< "float GeometrySchlickGGX(float NdotV, float roughness)\n"
			<< "{\n"
			<< "\tfloat r = (roughness + 1.0);\n"
			<< "\tfloat k = (r*r) / 8.0;\n"
			<< "\tfloat num   = NdotV;\n"
			<< "\tfloat denom = NdotV * (1.0 - k) + k;\n"
			<< "\treturn num / denom;\n"
			<< "}\n\n";

		// GeometrySmith
		m_pixelShaderStream
			<< "float GeometrySmith(float3 N, float3 V, float3 L, float roughness)\n"
			<< "{\n"
			<< "\tfloat NdotV = max(dot(N, V), 0.0);\n"
			<< "\tfloat NdotL = max(dot(N, L), 0.0);\n"
			<< "\tfloat ggx2  = GeometrySchlickGGX(NdotV, roughness);\n"
			<< "\tfloat ggx1  = GeometrySchlickGGX(NdotL, roughness);\n"
			<< "\treturn ggx1 * ggx2;\n"
			<< "}\n\n";

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

		m_pixelShaderStream << "\tfloat3 V = normalize(input.viewDir);\n\n";

		for (const auto& code : m_expressions)
		{
			m_pixelShaderStream << "\t" << code;
		}

		// Normal
		if (m_normalExpression != IndexNone)
		{
			const auto expression = GetExpressionType(m_normalExpression);
			if (expression == ExpressionType::Float_4)
			{
				m_pixelShaderStream << "\tfloat3 N = getNormalFromMap(input.worldPos, input.uv0, normalize(input.normal), expr_" << m_normalExpression << ".rgb);\n\n";
			}
			else
			{
				m_pixelShaderStream << "\tfloat3 N = getNormalFromMap(input.worldPos, input.uv0, normalize(input.normal), float3(expr_" << m_normalExpression << "));\n\n";
			}
		}
		else
		{
			m_pixelShaderStream << "\tfloat3 N = normalize(input.normal);\n\n";
		}

		// Roughness
		if (m_roughnessExpression != IndexNone)
		{
			const auto expression = GetExpressionType(m_roughnessExpression);
			if (expression == ExpressionType::Float_1)
			{
				m_pixelShaderStream << "\tfloat roughness = expr_" << m_roughnessExpression << ";\n\n";
			}
			else
			{
				m_pixelShaderStream << "\tfloat roughness = expr_" << m_roughnessExpression << ".r;\n\n";
			}
		}
		else
		{
			m_pixelShaderStream << "\tfloat roughness = 1.0;\n\n";
		}
		
		// Metallic
		if (m_metallicExpression != IndexNone)
		{
			const auto expression = GetExpressionType(m_metallicExpression);
			if (expression == ExpressionType::Float_1)
			{
				m_pixelShaderStream << "\tfloat metallic = expr_" << m_metallicExpression << ";\n\n";
			}
			else
			{
				m_pixelShaderStream << "\tfloat metallic = expr_" << m_metallicExpression << ".r;\n\n";
			}
		}
		else
		{
			m_pixelShaderStream << "\tfloat metallic = 1.0;\n\n";
		}
		
		// BaseColor base
		m_pixelShaderStream << "\tfloat3 baseColor = float3(1.0, 1.0, 1.0);\n\n";
		if (m_baseColorExpression != IndexNone)
		{
			const auto expressionType = GetExpressionType(m_baseColorExpression);
			if (expressionType == ExpressionType::Float_1 || expressionType == ExpressionType::Float_3)
			{
				m_pixelShaderStream << "\tbaseColor = expr_" << m_baseColorExpression << ";\n\n";
			}
			else
			{
				if (expressionType == ExpressionType::Float_2)
				{
					m_pixelShaderStream << "\tbaseColor = float3(expr_" << m_baseColorExpression << ", 1.0);\n\n";
				}
				else if (expressionType == ExpressionType::Float_4)
				{
					m_pixelShaderStream << "\tbaseColor = expr_" << m_baseColorExpression << ".rgb;\n\n";
				}
			}
		}

		m_pixelShaderStream << "\tbaseColor = pow(baseColor, 2.2);\n";
		
		m_pixelShaderStream
			<< "\tfloat3 ao = float3(1.0, 1.0, 1.0);\n\n";

		if (m_lit)
		{
			m_pixelShaderStream
				<< "\tfloat3 F0 = 0.04;\n"
				<< "\tF0 = lerp(F0, baseColor, metallic);\n\n";

			m_pixelShaderStream
				<< "\tfloat3 L = normalize(-float3(1.0, -0.5, 1.0));\n"	// LightDir
				<< "\tfloat3 H = normalize(V + L);\n"				
				<< "\tfloat3 radiance = float3(4.0, 4.0, 4.0);\n\n";		// Light color * attenuation
			
			m_pixelShaderStream
				<< "\tfloat NDF = DistributionGGX(N, H, roughness);\n"
				<< "\tfloat G   = GeometrySmith(N, V, L, roughness);\n"
				<< "\tfloat3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);\n";
			
			m_pixelShaderStream
				<< "\tfloat3 kS = F;\n"
				<< "\tfloat3 kD = float3(1.0, 1.0, 1.0) - kS;\n"
				<< "\tkD *= 1.0 - metallic;\n";
			
			m_pixelShaderStream
				<< "\tfloat3 numerator    = NDF * G * F;\n"
				<< "\tfloat denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0)  + 0.0001;\n"
				<< "\tfloat3 specular     = numerator / denominator;\n";

			m_pixelShaderStream
				<< "\tfloat NdotL = max(dot(N, L), 0.0);\n"
				<< "\tfloat3 Lo = (kD * baseColor / PI + specular) * radiance * NdotL;\n";

			m_pixelShaderStream
				<< "\tfloat3 ambient = float3(0.03, 0.03, 0.03) * baseColor * ao;\n"
				<< "\tfloat3 color   = ambient + Lo;\n";

			m_pixelShaderStream
				<< "\tcolor = color / (color + float3(1.0, 1.0, 1.0));\n"
				<< "\tcolor = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));\n";
		}
		
		// Combining it
		if (m_lit)
		{
			m_pixelShaderStream
				<< "\toutputColor = float4(color, 1.0);\n";
		}
		else
		{
			m_pixelShaderStream
				<< "\toutputColor = input.color * baseColor;\n";
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
