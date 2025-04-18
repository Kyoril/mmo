// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_compiler_d3d11.h"

#include <fstream>

#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/text_writer.h"
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

	ExpressionIndex MaterialCompilerD3D11::AddTextureSample(std::string_view texture, const ExpressionIndex coordinates, bool srgb, SamplerType type)
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

		if (type == SamplerType::Normal)
		{
			outputStream << "(";
		}

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

		if (type == SamplerType::Normal)
		{
			outputStream << " * 2.0 - 1.0)";
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
		
		std::ostringstream outputStream;
		outputStream << "dot(expr_" << first << ", expr_" << second << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_1);
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
		outputStream << "N";
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
		const ExpressionType targetType = GetExpressionType(second);

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

	ExpressionIndex MaterialCompilerD3D11::AddTransform(const ExpressionIndex input, Space sourceSpace, Space targetSpace)
	{
		if (input == IndexNone)
		{
			WLOG("Missing input parameter of transform");
			return IndexNone;
		}

		const auto type = GetExpressionType(input);
		if (GetExpressionTypeComponentCount(type) < 3)
		{
			ELOG("Input of transform needs to be a vector!")
			return IndexNone;
		}

		auto outputType = ExpressionType::Float_3;

		std::ostringstream outputStream;
		if (sourceSpace == Space::World)
		{
			if (targetSpace == Space::Tangent)
			{
				outputStream << "mul(TBN, expr_" << input << ")";
			}
			else
			{
				WLOG("Transform between given two spaces not yet supported!");
				return IndexNone;
			}
		}
		else
		{
			WLOG("Transform between given two spaces not yet supported!");
			return IndexNone;
		}
		outputStream.flush();

		return AddExpression(outputStream.str(), outputType);
	}

	void MaterialCompilerD3D11::GeneratePixelShaderCode(PixelShaderType type)
	{
		m_pixelShaderStream.str("");
		m_pixelShaderStream.clear();

		m_pixelShaderStream
			<< "static const float PI = 3.14159265359;\n\n";

		m_pixelShaderStream
			<< "float select(bool expression, float whenTrue, float whenFalse) {\n"
			<< "\treturn expression ? whenTrue : whenFalse;\n"
			<< "}\n\n";

		if (type == PixelShaderType::GBuffer)
		{
			m_pixelShaderStream
				<< "float Dither8x8(int2 pos)\n"
				<< "{\n"
				<< "	static const float thresholdMatrix[64] = {\n"
				<< "		 0, 48, 12, 60,  3, 51, 15, 12,\n"
				<< "		32, 16, 44, 28, 35, 19, 47, 31,\n"
				<< "		 8, 44,  4, 30, 11, 22,  7, 55,\n"
				<< "		40, 24, 36, 20, 43, 27, 39, 23,\n"
				<< "		 2, 32, 14, 62,  1, 49, 13, 61,\n"
				<< "		34, 18, 46, 30, 33, 17, 45, 29,\n"
				<< "		10, 44,  6, 54,  9, 57,  5, 53,\n"
				<< "		42, 26, 38, 22, 41, 25, 37, 21\n"
				<< "	};\n"
				<< "	int x = pos.x % 8;\n"
				<< "	int y = pos.y % 8;\n"
				<< "	int index = y * 8 + x;\n"
				<< "	return thresholdMatrix[index] / 64.0;\n"
				<< "}\n\n";

			// For G-Buffer output
			m_pixelShaderStream
				<< "struct GBufferOutput\n"
				<< "{\n"
				<< "\tfloat4 albedo : SV_Target0;    // RGB: Albedo, A: Opacity\n"
				<< "\tfloat4 normal : SV_Target1;    // RGB: Normal, A: Depth\n"
				<< "\tfloat4 material : SV_Target2;  // R: Metallic, G: Roughness, B: Specular, A: Ambient Occlusion\n"
				<< "\tfloat4 emissive : SV_Target3;  // RGB: Emissive, A: Unused\n"
				<< "\tfloat4 viewRay : SV_Target4;  // RGB: ViewRay, A: Unused\n"
				<< "};\n\n";
		}

		size_t bufferRegister = 1;

		m_pixelShaderStream
			<< "cbuffer Matrices : register(b0)\n"
			<< "{\n"
			<< "\tcolumn_major matrix matWorld;\n"
			<< "\tcolumn_major matrix matView;\n"
			<< "\tcolumn_major matrix matProj;\n"
			<< "\tcolumn_major matrix matInvView;\n"
			<< "\tcolumn_major matrix matInvProj;\n"
			<< "};\n\n";

		m_pixelShaderStream
			<< "cbuffer CameraParameters : register(b" << bufferRegister++ << ")\n"
			<< "{\n"
			<< "\tfloat3 cameraPos;	// Camera position in world space\n"
			<< "\tfloat fogStart;	// Distance of fog start\n"
			<< "\tfloat fogEnd;		// Distance of fog end\n"
			<< "\tfloat3 fogColor;	// Fog color\n"
			<< "\trow_major matrix inverseCameraView;	// Inverse view matrix\n"
			<< "};\n\n";

		const auto& scalarParams = m_floatParameters;
		if (!scalarParams.empty())
		{
			m_pixelShaderStream
				<< "cbuffer ScalarParameters : register(b" << bufferRegister++ << ")\n"
				<< "{\n";

			for (const auto& param : scalarParams)
			{
				m_material->AddScalarParameter(param.name, param.value);
				m_pixelShaderStream << "\tfloat s" << param.name << ";\n";
			}

			m_pixelShaderStream
				<< "};\n\n";
		}

		const auto& vectorParams = m_vectorParameters;
		if (!vectorParams.empty())
		{
			m_pixelShaderStream
				<< "cbuffer VectorParameters : register(b" << bufferRegister++ << ")\n"
				<< "{\n";

			for (const auto& param : vectorParams)
			{
				m_material->AddVectorParameter(param.name, param.value);
				m_pixelShaderStream << "\tfloat4 v" << param.name << ";\n";
			}

			m_pixelShaderStream
				<< "};\n\n";
		}
		
		// VertexOut struct
		m_pixelShaderStream
			<< "struct VertexOut\n"
			<< "{\n"
			<< "\tfloat4 pos : SV_POSITION;\n"
			<< "\tfloat4 color : COLOR;\n";

		if (m_lit)
		{
			m_pixelShaderStream
				<< "\tfloat3 normal : NORMAL;\n"
				<< "\tfloat3 binormal : BINORMAL;\n"
				<< "\tfloat3 tangent : TANGENT;\n";
		}
		
		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			m_pixelShaderStream
				<< "\tfloat2 uv" << i << " : TEXCOORD" << i << ";\n";
		}
		m_pixelShaderStream << "\tfloat3 worldPos : TEXCOORD"<< m_numTexCoordinates << ";\n";
		m_pixelShaderStream << "\tfloat3 viewDir : TEXCOORD" << m_numTexCoordinates + 1 << ";\n";
		m_pixelShaderStream << "\tfloat3 viewPos : TEXCOORD" << m_numTexCoordinates + 2 << ";\n";
		m_pixelShaderStream << "};\n\n";

		// Add texture samplers
		for (size_t i = 0; i < m_textures.size(); ++i)
		{
			m_pixelShaderStream << "// " << m_textures[i] << "\n";
			m_pixelShaderStream << "Texture2D tex" << i << ";\n";
			m_pixelShaderStream << "SamplerState sampler" << i << ";\n\n";
		}

		// Add texture parameter samplers
		const auto& textureParams = m_textureParameters;
		if (!textureParams.empty())
		{
			for (size_t i = 0; i < textureParams.size(); ++i)
			{
				m_material->AddTextureParameter(textureParams[i].name, textureParams[i].texture);
				m_pixelShaderStream
					<< "// " << textureParams[i].name << "\n"
					<< "Texture2D texparam" << i << ";\n"
					<< "SamplerState paramsampler" << i << ";\n\n";
			}
		}

		if (m_lit)
		{
			m_pixelShaderStream
				<< "float3 GetWorldNormal(float3 tangentSpaceNormal, float3 N, float3 T, float3 B)\n"
				<< "{\n"
				<< "\t// tangentSpaceNormal is usually in range [0,1]. Convert to [-1,1]\n"
				<< "\tfloat3 n = tangentSpaceNormal /* * 2.0f - 1.0f*/;\n\n"
				<< "\t// Re-orient using T, B, N. (Assuming T,B,N are all normalized & orthonormal)\n"
				<< "\tfloat3 worldNormal = normalize(n.x * T + n.y * B + n.z * N);\n"
				<< "\treturn worldNormal;\n"
				<< "}\n\n";

			if (type != PixelShaderType::GBuffer)
			{
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
					<< "\tfloat denom = NdotV * (1.0 - k) + k;\n"
					<< "\treturn NdotV / denom;\n"
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
			}
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
		if (type == PixelShaderType::GBuffer)
		{
			m_pixelShaderStream
				<< "GBufferOutput main(VertexOut input)\n"
				<< "{\n"
				<< "\tfloat4 outputColor = float4(1, 1, 1, 1);\n\n";
		}
		else if (type == PixelShaderType::ShadowMap)
		{
			m_pixelShaderStream
				<< "void main(VertexOut input)\n"
				<< "{\n";
		}
		else
		{
			m_pixelShaderStream
				<< "float4 main(VertexOut input) : SV_Target\n"
				<< "{\n"
				<< "\tfloat4 outputColor = float4(1, 1, 1, 1);\n\n";
		}

		if (type != PixelShaderType::ShadowMap)
		{
			if (m_lit)
			{
				m_pixelShaderStream << "\tfloat3 N = normalize(input.normal);\n\n";
			}

			m_pixelShaderStream << "\tfloat3 V = normalize(input.viewDir);\n\n";

			if (m_lit)
			{
				m_pixelShaderStream
					<< "\tfloat3 B = normalize(input.binormal);\n"
					<< "\tfloat3 T = normalize(input.tangent);\n"
					<< "\tfloat3x3 TBN = float3x3(T, B, N);\n";
			}
		}

		for (const auto& code : m_expressions)
		{
			m_pixelShaderStream << "\t" << code;
		}

		if (type != PixelShaderType::ShadowMap)
		{
			if (m_lit)
			{
				// Normal
				if (m_normalExpression != IndexNone)
				{
					const auto expression = GetExpressionType(m_normalExpression);
					if (expression == ExpressionType::Float_4)
					{
						m_pixelShaderStream << "\tN = expr_" << m_normalExpression << ".rgb;\n\n";
					}
					else
					{
						m_pixelShaderStream << "\tN = expr_" << m_normalExpression << ";\n\n";
					}

					// We expect normal to be in tangent space: (0.5, 0.5, 1.0) = Z up normal
					m_pixelShaderStream
						<< "\tN = GetWorldNormal(N, input.normal, input.tangent, input.binormal);\n";
				}
			}
			else
			{
				m_pixelShaderStream << "\tfloat3 N = float3(0.0, 0.0, 1.0);\n\n";
			}

			// Specular
			if (m_specularExpression != IndexNone)
			{
				const auto expression = GetExpressionType(m_specularExpression);
				if (expression == ExpressionType::Float_1)
				{
					m_pixelShaderStream << "\tfloat specular = saturate(expr_" << m_specularExpression << ");\n\n";
				}
				else
				{
					m_pixelShaderStream << "\tfloat specular = saturate(expr_" << m_specularExpression << ".r);\n\n";
				}
			}
			else
			{
				m_pixelShaderStream << "\tfloat specular = 0.5;\n\n";
			}

			// Roughness
			if (m_roughnessExpression != IndexNone)
			{
				const auto expression = GetExpressionType(m_roughnessExpression);
				if (expression == ExpressionType::Float_1)
				{
					m_pixelShaderStream << "\tfloat roughness = saturate(expr_" << m_roughnessExpression << ");\n\n";
				}
				else
				{
					m_pixelShaderStream << "\tfloat roughness = saturate(expr_" << m_roughnessExpression << ".r);\n\n";
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
					m_pixelShaderStream << "\tfloat metallic = saturate(expr_" << m_metallicExpression << ");\n\n";
				}
				else
				{
					m_pixelShaderStream << "\tfloat metallic = saturate(expr_" << m_metallicExpression << ".r);\n\n";
				}
			}
			else
			{
				m_pixelShaderStream << "\tfloat metallic = 0.0;\n\n";
			}
		}

		// Opacity
		if (m_opacityExpression != IndexNone)
		{
			const auto expression = GetExpressionType(m_opacityExpression);
			if (expression == ExpressionType::Float_1)
			{
				m_pixelShaderStream << "\tfloat opacity = saturate(expr_" << m_opacityExpression << ");\n\n";
			}
			else
			{
				m_pixelShaderStream << "\tfloat opacity = saturate(expr_" << m_opacityExpression << ".r);\n\n";
			}
		}
		else
		{
			m_pixelShaderStream << "\tfloat opacity = 1.0;\n\n";
		}

		if (type != PixelShaderType::ShadowMap)
		{
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
		}

		if (type != PixelShaderType::GBuffer)
		{
			m_pixelShaderStream
				<< "\tif (opacity <= 0.333) discard;\n";
		}
		else
		{
			m_pixelShaderStream
				//<< "\tfloat threshold = Dither8x8(input.pos.xy + cameraPos.xy * cameraPos.z);\n"
				<< "\tif (opacity < 0.333) discard;\n";
		}

		if (type != PixelShaderType::ShadowMap)
		{
			if (type != PixelShaderType::GBuffer)
			{
				m_pixelShaderStream << "\tbaseColor = pow(baseColor, 2.2);\n";
			}

			m_pixelShaderStream
				<< "\tfloat3 ao = float3(1.0, 1.0, 1.0);\n\n";

			if (type != PixelShaderType::GBuffer)
			{
				if (m_lit)
				{
					m_pixelShaderStream
						<< "\tfloat3 F0 = 0.04;\n"
						<< "\tF0 = lerp(F0, baseColor, metallic);\n\n";

					m_pixelShaderStream
						<< "\tfloat3 L = normalize(-float3(0.5, -1.0, 0.5));\n"	// LightDir
						<< "\tfloat3 H = normalize(V + L);\n"
						<< "\tfloat3 radiance = float3(4.0, 4.0, 4.0);\n\n";		// Light color * attenuation

					m_pixelShaderStream
						<< "\tfloat NDF = DistributionGGX(N, H, roughness);\n"
						<< "\tfloat G   = GeometrySmith(N, V, L, roughness);\n"
						<< "\tfloat3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);\n";

					m_pixelShaderStream
						<< "\tfloat3 kS = F;\n"
						<< "\tfloat3 kD = 1.0f.xxx - kS;\n"
						<< "\tkD *= 1.0 - metallic;\n";

					m_pixelShaderStream
						<< "\tfloat3 numerator    = NDF * G * F;\n"
						<< "\tfloat denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0)  + 0.0001;\n"
						<< "\tfloat3 specularity     = numerator / denominator;\n";

					m_pixelShaderStream
						<< "\tfloat NdotL = max(dot(N, L), 0.0);\n"
						<< "\tfloat3 Lo = (kD * baseColor / PI + specularity) * radiance * NdotL;\n";

					m_pixelShaderStream
						<< "\tkS = fresnelSchlick(max(dot(N, V), 0.0), F0);\n"
						<< "\tkD = 1.0f.xxx - kS;\n"
						<< "\tkD *= 1.0 - metallic;\n"
						<< "\tfloat3 irradiance = float3(0.1f, 0.25f, 0.3f);\n"
						<< "\tfloat3 diffuse = irradiance * baseColor;\n"
						<< "\tfloat3 ambient = (kD * diffuse) * ao;\n";

					m_pixelShaderStream
						<< "\tfloat3 color = ambient + Lo;\n";

					m_pixelShaderStream
						<< "\tcolor = color / (color + 1.0f.xxx);\n"
						<< "\tcolor = pow(color, (1.0f/2.2f).xxx);\n";

					m_pixelShaderStream
						<< "\tfloat distance = length(input.worldPos - cameraPos);\n"
						<< "\tfloat fogFactor = saturate((distance - fogStart) / (fogEnd - fogStart));\n"
						<< "\tcolor = lerp(color, fogColor, fogFactor);\n";
				}
			}

			// Combining it
			if (type == PixelShaderType::GBuffer)
			{
				// G-Buffer output
				m_pixelShaderStream
					<< "\tGBufferOutput output;\n";

				if (m_depthWrite)
				{
					m_pixelShaderStream
						<< "\tfloat3 viewPos = mul(float4(input.worldPos, 1.0), matView).xyz;\n"
						<< "\tfloat linearDepth = length(viewPos);\n";
				}
				else
				{
					m_pixelShaderStream
						<< "\tfloat linearDepth = 0.0f;\n";	// TODO: Is that correct? Or maybe Z-Far?
				}

				m_pixelShaderStream
					<< "\toutput.viewRay = float4(normalize(input.viewPos), 1.0);\n";

				// For unlit materials, write base color to emissive instead of albedo
				if (!m_lit)
				{
					// Albedo - empty for unlit materials
					m_pixelShaderStream
						<< "\toutput.albedo = float4(0.0, 0.0, 0.0, 1.0);\n";

					// Normal - default up vector for unlit materials
					m_pixelShaderStream
						<< "\toutput.normal = float4(0.5, 0.5, 1.0, linearDepth);\n";

					// Material properties
					m_pixelShaderStream
						<< "\toutput.material = float4(metallic, roughness, specular, 1.0);\n";

					// Emissive - use base color for unlit materials
					m_pixelShaderStream
						<< "\toutput.emissive = float4(baseColor, 0.0);\n";
				}
				else
				{
					// Albedo
					m_pixelShaderStream
						<< "\toutput.albedo = float4(baseColor, 1.0);\n";

					// Normal
					m_pixelShaderStream
						<< "\toutput.normal = float4(N * 0.5 + 0.5, linearDepth);\n";

					// Material properties
					m_pixelShaderStream
						<< "\toutput.material = float4(metallic, roughness, specular, 1.0);\n";

					// Emissive - empty for lit materials
					m_pixelShaderStream
						<< "\toutput.emissive = float4(0.0, 0.0, 0.0, 0.0);\n";
				}

				// Return G-Buffer output
				m_pixelShaderStream
					<< "\treturn output;\n"
					<< "}"
					<< std::endl;
			}
			else
			{
				// Forward rendering output
				if (m_lit)
				{
					m_pixelShaderStream
						<< "\toutputColor = float4(color, opacity);\n";
				}
				else
				{
					m_pixelShaderStream
						<< "\toutputColor = float4(baseColor, opacity);\n";
				}

				// End of main function
				m_pixelShaderStream
					<< "\treturn outputColor;\n"
					<< "}"
					<< std::endl;
			}
		}
		else
		{
			// End of main function
			m_pixelShaderStream
				<< "\treturn;\n"
				<< "}"
				<< std::endl;
		}

		m_pixelShaderCode[(int)type] = m_pixelShaderStream.str();
		m_pixelShaderStream.clear();

#ifdef _DEBUG
		// Write shader output to asset registry for debug
		if (const auto filePtr = AssetRegistry::CreateNewFile(type == PixelShaderType::Forward ? "PSForward.hlsl" : type == PixelShaderType::GBuffer ? "PSDeferred.hlsl" : "PSShadow.hlsl"))
		{
			io::StreamSink sink(*filePtr);
			io::TextWriter<char> writer(sink);
			writer.write(m_pixelShaderCode[(int)type]);
		}
#endif
	}

	ExpressionIndex MaterialCompilerD3D11::AddTextureParameterSample(std::string_view name, std::string_view texture, ExpressionIndex coordinates, bool srgb, SamplerType type)
	{
		// Ensure parameter exists
		uint32 paramIndex = 0;
		auto it = m_textureParameters.begin();
		for (; it != m_textureParameters.end(); ++it, ++paramIndex)
		{
			if (it->name == name)
			{
				it->texture = texture;
				break;
			}
		}

		if (it == m_textureParameters.end())
		{
			m_textureParameters.emplace_back(String(name), String(texture));
			paramIndex = m_textureParameters.size() - 1;
		}

		std::ostringstream outputStream;

		if (type == SamplerType::Normal)
		{
			outputStream << "(";
		}

		if (srgb)
		{
			outputStream << "pow(";
		}

		outputStream << "texparam" << paramIndex << ".Sample(paramsampler" << paramIndex << ", ";
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

		if (type == SamplerType::Normal)
		{
			outputStream << " * 2.0 - 1.0)";
		}

		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_4);
	}

	ExpressionIndex MaterialCompilerD3D11::AddScalarParameterExpression(std::string_view name, float defaultValue)
	{
		// Ensure parameter exists
		if (const auto it = std::find_if(m_floatParameters.begin(), m_floatParameters.end(), [&name](const auto& param) { return param.name == name; }); it == m_floatParameters.end())
		{
			m_floatParameters.emplace_back(String(name), defaultValue);
		}
		else
		{
			it->value = defaultValue;
		}

		String copy(name);
		std::replace(copy.begin(), copy.end(), ' ', '_');

		std::ostringstream outputStream;
		outputStream << "s" << copy;
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_1);
	}

	ExpressionIndex MaterialCompilerD3D11::AddVectorParameterExpression(std::string_view name, const Vector4& defaultValue)
	{
		// Ensure parameter exists
		if (const auto it = std::find_if(m_vectorParameters.begin(), m_vectorParameters.end(), [&name](const auto& param) { return param.name == name; }); it == m_vectorParameters.end())
		{
			m_vectorParameters.emplace_back(String(name), defaultValue);
		}
		else
		{
			it->value = defaultValue;
		}

		String copy(name);
		std::replace(copy.begin(), copy.end(), ' ', '_');

		std::ostringstream outputStream;
		outputStream << "v" << name;
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_4);
	}

	void MaterialCompilerD3D11::GenerateVertexShaderCode(VertexShaderType type)
	{
		std::ostringstream vertexShaderStream;
		m_vertexShaderCode.clear();

		// VertexIn struct
		vertexShaderStream
			<< "struct VertexIn\n"
			<< "{\n"
			<< "\tfloat3 pos : POSITION;\n"
			<< "\tfloat4 color : COLOR;\n"
			<< "\tfloat3 normal : NORMAL;\n"
			<< "\tfloat3 binormal : BINORMAL;\n"
			<< "\tfloat3 tangent : TANGENT;\n";

		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			vertexShaderStream
				<< "\tfloat2 uv" << i << " : TEXCOORD" << i << ";\n";
		}

		if (type == VertexShaderType::SkinnedLow || type == VertexShaderType::SkinnedMedium || type == VertexShaderType::SkinnedHigh)
		{
			vertexShaderStream
				<< "\tuint4 boneIndices : BLENDINDICES;\n"
				<< "\tfloat4 boneWeights : BLENDWEIGHT;\n";
		}

		vertexShaderStream
			<< "};\n\n";

		// VertexOut struct
		vertexShaderStream
			<< "struct VertexOut\n"
			<< "{\n"
			<< "\tfloat4 pos : SV_POSITION;\n"
			<< "\tfloat4 color : COLOR;\n";

		if (m_lit)
		{
			vertexShaderStream
				<< "\tfloat3 normal : NORMAL;\n"
				<< "\tfloat3 binormal : BINORMAL;\n"
				<< "\tfloat3 tangent : TANGENT;\n";
		}

		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			vertexShaderStream
				<< "\tfloat2 uv" << i << " : TEXCOORD" << i << ";\n";
		}
		vertexShaderStream
			<< "\tfloat3 worldPos : TEXCOORD" << m_numTexCoordinates << ";\n"
			<< "\tfloat3 viewDir : TEXCOORD" << m_numTexCoordinates + 1 << ";\n"
			<< "\tfloat3 viewPos : TEXCOORD" << m_numTexCoordinates + 2 << ";\n";

		vertexShaderStream
			<< "};\n\n";

		uint32 numBones = 0;
		switch (type)
		{
		case VertexShaderType::SkinnedLow: numBones = 64; break;
		case VertexShaderType::SkinnedMedium: numBones = 128; break;
		case VertexShaderType::SkinnedHigh: numBones = 256; break;
		default:
			numBones = 0;
			break;
		}

		// Matrix constant buffer
		vertexShaderStream
			<< "cbuffer Matrices\n"
			<< "{\n"
			<< "\tcolumn_major matrix matWorld;\n"
			<< "\tcolumn_major matrix matView;\n"
			<< "\tcolumn_major matrix matProj;\n"
			<< "\tcolumn_major matrix matInvView;\n"
			<< "\tcolumn_major matrix InverseProjection;\n"
			<< "};\n\n";

		if (numBones > 0)
		{
			vertexShaderStream
				<< "cbuffer Bones\n"
				<< "{\n"
				<< "\tcolumn_major matrix matBone[" << numBones << "];\n"	// TODO: Dual quaternion support
				<< "};\n\n";
		}

		// Main procedure start
		vertexShaderStream
			<< "VertexOut main(VertexIn input)\n"
			<< "{\n"
			<< "\tVertexOut output;\n\n";

		const bool withSkinning = (type != VertexShaderType::Default);
		if (withSkinning)
		{
			vertexShaderStream
				<< "\tmatrix boneMatrix = matBone[input.boneIndices[0]-1] * input.boneWeights[0];\n"
				<< "\tif(input.boneIndices[1] != 0) { boneMatrix += matBone[input.boneIndices[1]-1] * input.boneWeights[1]; }\n"
				<< "\tif(input.boneIndices[2] != 0) { boneMatrix += matBone[input.boneIndices[2]-1] * input.boneWeights[2]; }\n"
				<< "\tif(input.boneIndices[3] != 0) { boneMatrix += matBone[input.boneIndices[3]-1] * input.boneWeights[3]; }\n\n";

			vertexShaderStream << "\tfloat4 transformedPos = mul(float4(input.pos.xyz, 1.0), boneMatrix);\n";
			if (m_lit)
			{
				vertexShaderStream
					<< "\tfloat3 transformedNormal = mul(input.normal, (float3x3)boneMatrix);\n"
					<< "\tfloat3 transformedBinormal = mul(input.binormal, (float3x3)boneMatrix);\n"
					<< "\tfloat3 transformedTangent = mul(input.tangent, (float3x3)boneMatrix);\n\n";
			}
		}
		else
		{
			vertexShaderStream
				<< "\tfloat4 transformedPos = float4(input.pos.xyz, 1.0);\n";

			if (m_lit)
			{
				vertexShaderStream
					<< "\tfloat3 transformedNormal = input.normal;\n"
					<< "\tfloat3 transformedBinormal = input.binormal;\n"
					<< "\tfloat3 transformedTangent = input.tangent;\n\n";
			}
		}

		// Basic transformations
		vertexShaderStream
			<< "\toutput.pos = mul(transformedPos, matWorld);\n"
			<< "\toutput.worldPos = output.pos.xyz;\n"
			<< "\toutput.viewDir = normalize(matInvView[3].xyz - output.worldPos);\n"
			<< "\toutput.pos = mul(output.pos, matView);\n"
			<< "\toutput.viewPos = output.pos;\n"
			<< "\toutput.pos = mul(output.pos, matProj);\n"
			<< "\toutput.color = input.color;\n";

		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			vertexShaderStream
				<< "\toutput.uv" << i << " = input.uv" << i << ";\n";
		}

		if (m_lit)
		{
			vertexShaderStream
				<< "\toutput.binormal = normalize(mul(normalize(transformedBinormal), (float3x3)matWorld));\n"
				<< "\toutput.tangent = normalize(mul(normalize(transformedTangent), (float3x3)matWorld));\n"
				<< "\toutput.normal = normalize(mul(normalize(transformedNormal), (float3x3)matWorld));\n";
		}

		// Main procedure end
		vertexShaderStream
			<< "\n\treturn output;\n"
			<< "}\n"
			<< std::endl;

		m_vertexShaderCode = vertexShaderStream.str();
		vertexShaderStream.clear();

#ifdef _DEBUG
		// Write shader output to asset registry for debug
		if (const auto filePtr = AssetRegistry::CreateNewFile("VS_" + std::to_string(static_cast<int>(type)) + ".hlsl"))
		{
			io::StreamSink sink(*filePtr);
			io::TextWriter<char> writer(sink);
			writer.write(m_vertexShaderCode);
		}
#endif
	}
}
