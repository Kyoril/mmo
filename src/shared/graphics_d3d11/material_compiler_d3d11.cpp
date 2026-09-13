// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_compiler_d3d11.h"

#include <fstream>

#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/text_writer.h"
#include "graphics/material.h"
#include "graphics/global_shader_parameters.h"
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

		if (type == SamplerType::Normal)
		{
			// First, add the raw sample as a temporary expression
			const ExpressionIndex rawSample = AddExpression(outputStream.str(), ExpressionType::Float_4);

			// Reconstruct the normal: read RG, remap to [-1,1], derive Z = sqrt(1 - x^2 - y^2)
			// This works correctly for both BC5/RG8 (where B=0) and standard RGBA normal maps
			std::ostringstream normalStream;
			normalStream << "float4(expr_" << rawSample << ".rg * 2.0 - 1.0, "
				<< "sqrt(saturate(1.0 - dot(expr_" << rawSample << ".rg * 2.0 - 1.0, expr_" << rawSample << ".rg * 2.0 - 1.0))), 0.0)";
			normalStream.flush();

			return AddExpression(normalStream.str(), ExpressionType::Float_4);
		}

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

	ExpressionIndex MaterialCompilerD3D11::AddMax(const ExpressionIndex first, const ExpressionIndex second)
	{
		if (first == IndexNone)
		{
			WLOG("Missing first parameter for maximum");
			return IndexNone;
		}

		if (second == IndexNone)
		{
			WLOG("Missing second parameter for maximum");
			return IndexNone;
		}

		const ExpressionType firstType = GetExpressionType(first);
		const ExpressionType secondType = GetExpressionType(second);

		std::ostringstream outputStream;
		outputStream << "max(expr_" << first << ", expr_" << second << ")";
		outputStream.flush();

		// Like multiplication, the widest operand wins: max(float, float4) is a float4.
		return AddExpression(outputStream.str(), std::max(firstType, secondType));
	}

	ExpressionIndex MaterialCompilerD3D11::AddMin(const ExpressionIndex first, const ExpressionIndex second)
	{
		if (first == IndexNone)
		{
			WLOG("Missing first parameter for minimum");
			return IndexNone;
		}

		if (second == IndexNone)
		{
			WLOG("Missing second parameter for minimum");
			return IndexNone;
		}

		const ExpressionType firstType = GetExpressionType(first);
		const ExpressionType secondType = GetExpressionType(second);

		std::ostringstream outputStream;
		outputStream << "min(expr_" << first << ", expr_" << second << ")";
		outputStream.flush();

		// Like multiplication, the widest operand wins: max(float, float4) is a float4.
		return AddExpression(outputStream.str(), std::max(firstType, secondType));
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

	ExpressionIndex MaterialCompilerD3D11::AddCameraPosition()
	{
		std::ostringstream outputStream;
		outputStream << "cameraPos";
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

		const auto outputType = ExpressionType::Float_3;

		// Transforming a vector into the same space is a no-op; just reuse the input expression.
		if (sourceSpace == targetSpace)
		{
			return input;
		}

		// We only support direction-vector transforms between the spaces exposed by the editor's
		// Transform Vector node (Local, World, View, Tangent). All transforms are expressed by first
		// bringing the vector into world space and then into the target space, which keeps the matrix
		// usage consistent and lets us cover every combination with the matrices available in the
		// pixel shader (matWorld, matView, matInvView) plus the per-pixel TBN basis.
		//
		// The engine uses the row-vector convention (v' = mul(v, M)) everywhere except for the
		// world->tangent projection, which uses mul(TBN, v) so that each component becomes a dot
		// product against the tangent basis. World<->Local and World<->Tangent assume an orthonormal
		// basis (no non-uniform scale), which is the same limitation as the existing normal handling.

		// Builds the HLSL expression that brings 'inner' from the given space into world space.
		const auto toWorld = [](Space space, const std::string& inner) -> std::string
		{
			switch (space)
			{
			case Space::World:   return inner;
			case Space::Local:   return "mul(" + inner + ", (float3x3)matWorld)";
			case Space::View:    return "mul(" + inner + ", (float3x3)matInvView)";
			case Space::Tangent: return "mul(" + inner + ", TBN)";
			default:             return std::string();
			}
		};

		// Builds the HLSL expression that brings 'inner' (already in world space) into the given space.
		const auto fromWorld = [](Space space, const std::string& inner) -> std::string
		{
			switch (space)
			{
			case Space::World:   return inner;
			// matWorld is assumed orthonormal, so its inverse equals its transpose; mul(M, v) applies
			// the transpose under the row-vector convention used elsewhere.
			case Space::Local:   return "mul((float3x3)matWorld, " + inner + ")";
			case Space::View:    return "mul(" + inner + ", (float3x3)matView)";
			case Space::Tangent: return "mul(TBN, " + inner + ")";
			default:             return std::string();
			}
		};

		const std::string worldExpr = toWorld(sourceSpace, "expr_" + std::to_string(input));
		if (worldExpr.empty())
		{
			WLOG("Transform from given source space not yet supported!");
			return IndexNone;
		}

		const std::string result = fromWorld(targetSpace, worldExpr);
		if (result.empty())
		{
			WLOG("Transform to given target space not yet supported!");
			return IndexNone;
		}

		return AddExpression(result, outputType);
	}

	ExpressionIndex MaterialCompilerD3D11::AddTime()
	{
		return AddExpression("time", ExpressionType::Float_1);
	}

	ExpressionIndex MaterialCompilerD3D11::AddRotator(ExpressionIndex coordinates, ExpressionIndex center, ExpressionIndex rotation)
	{
		if (coordinates == IndexNone)
		{
			WLOG("Missing coordinates input for Rotator");
			return IndexNone;
		}

		if (center == IndexNone)
		{
			WLOG("Missing center input for Rotator");
			return IndexNone;
		}

		if (rotation == IndexNone)
		{
			WLOG("Missing rotation input for Rotator");
			return IndexNone;
		}

		std::ostringstream outputStream;
		outputStream << "(float2("
			<< "cos(expr_" << rotation << ") * (expr_" << coordinates << ".x - expr_" << center << ".x) - sin(expr_" << rotation << ") * (expr_" << coordinates << ".y - expr_" << center << ".y) + expr_" << center << ".x, "
			<< "sin(expr_" << rotation << ") * (expr_" << coordinates << ".x - expr_" << center << ".x) + cos(expr_" << rotation << ") * (expr_" << coordinates << ".y - expr_" << center << ".y) + expr_" << center << ".y"
			<< "))";
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_2);
	}

	ExpressionIndex MaterialCompilerD3D11::AddFresnel(ExpressionIndex exponent, ExpressionIndex baseReflectFraction, ExpressionIndex normal)
	{
		if (exponent == IndexNone)
		{
			WLOG("Missing exponent input for Fresnel");
			return IndexNone;
		}

		if (baseReflectFraction == IndexNone)
		{
			WLOG("Missing base reflect fraction input for Fresnel");
			return IndexNone;
		}

		// Fresnel = base + (1 - base) * pow(1 - saturate(dot(N, V)), exponent)
		// V is the normalized view direction, available in the shader as 'V'
		std::ostringstream outputStream;
		if (normal != IndexNone)
		{
			outputStream << "expr_" << baseReflectFraction << " + (1.0 - expr_" << baseReflectFraction
				<< ") * pow(1.0 - saturate(dot(normalize(expr_" << normal << "), V)), expr_" << exponent << ")";
		}
		else
		{
			outputStream << "expr_" << baseReflectFraction << " + (1.0 - expr_" << baseReflectFraction
				<< ") * pow(1.0 - saturate(dot(normalize(input.normal), V)), expr_" << exponent << ")";
		}
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_1);
	}

	ExpressionIndex MaterialCompilerD3D11::AddPixelDepth()
	{
		return AddExpression("length(input.viewPos.xyz)", ExpressionType::Float_1);
	}

	ExpressionIndex MaterialCompilerD3D11::AddSceneDepth()
	{
		m_needsSceneDepth = true;
		return AddExpression("sceneDepthTex.Load(int3((int2)input.pos.xy, 0)).a", ExpressionType::Float_1);
	}

	ExpressionIndex MaterialCompilerD3D11::AddScreenPosition()
	{
		return AddExpression("float2(input.pos.xy)", ExpressionType::Float_2);
	}

	ExpressionIndex MaterialCompilerD3D11::AddSaturate(const ExpressionIndex input)
	{
		if (input == IndexNone)
		{
			WLOG("Missing input parameter for saturate");
			return IndexNone;
		}

		const ExpressionType valueType = GetExpressionType(input);

		std::ostringstream outputStream;
		outputStream << "saturate(expr_" << input << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), valueType);
	}

	ExpressionIndex MaterialCompilerD3D11::AddSceneColor(const ExpressionIndex screenOffset)
	{
		m_needsSceneColor = true;

		// Sample the captured opaque scene color at this pixel, optionally offset in screen pixels
		// (used for refraction). Uses Load() so no sampler binding is required; the offset is added
		// in pixel space before the integer cast.
		std::ostringstream outputStream;
		outputStream << "sceneColorTex.Load(int3((int2)(input.pos.xy";
		if (screenOffset != IndexNone)
		{
			outputStream << " + expr_" << screenOffset << ".xy";
		}
		outputStream << "), 0)).rgb";
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_3);
	}

	ExpressionIndex MaterialCompilerD3D11::AddScreenSpaceReflection(const ExpressionIndex worldNormal,
		const ExpressionIndex maxDistance, const ExpressionIndex stepCount)
	{
		m_needsScreenSpaceReflection = true;
		m_needsSceneColor = true;
		m_needsSceneDepth = true;

		std::ostringstream outputStream;
		outputStream << "ComputeSSR(input.worldPos, ";

		if (worldNormal != IndexNone)
		{
			outputStream << "normalize(expr_" << worldNormal << ".xyz)";
		}
		else
		{
			// Not input.normal: expressions are shared by every pixel variant, and the unlit and UI
			// variants carry no normal in VertexOut, so referencing it fails their compile. World up
			// is the right default for what this node is for - reflections on flat water.
			outputStream << "float3(0.0, 1.0, 0.0)";
		}

		outputStream << ", ";
		if (maxDistance != IndexNone)
		{
			outputStream << "expr_" << maxDistance;
		}
		else
		{
			outputStream << "256.0f";
		}

		outputStream << ", ";
		if (stepCount != IndexNone)
		{
			outputStream << "expr_" << stepCount;
		}
		else
		{
			outputStream << "24.0f";
		}

		outputStream << ")";
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_4);
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

		// ACES Film tone mapping — matches the deferred lighting pass so forward-rendered
		// objects (translucent water, particles …) have a consistent look.
		m_pixelShaderStream
			<< "float3 ACESFilm(float3 x) {\n"
			<< "\tfloat a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;\n"
			<< "\treturn saturate((x*(a*x+b))/(x*(c*x+d)+e));\n"
			<< "}\n\n";

		// Exact inverse of ACESFilm followed by gamma. Unlit forward materials author display-referred
		// colour; inside DeferredRenderer's forward pass they convert it to the linear value the
		// TonemapPass will map back onto that same display colour.
		m_pixelShaderStream
			<< "float3 InverseACESFilm(float3 y) {\n"
			<< "\tfloat a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;\n"
			<< "\tfloat3 qa = y * c - a; float3 qb = y * d - b; float3 qc = y * e;\n"
			<< "\treturn (-qb - sqrt(max(qb * qb - 4.0 * qa * qc, 0.0))) / (2.0 * qa);\n"
			<< "}\n\n"
			<< "float3 InverseTonemap(float3 displayColor) {\n"
			<< "\treturn InverseACESFilm(pow(clamp(displayColor, 0.0, 0.999), 2.2));\n"
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
				<< "};\n\n";
		}

		size_t bufferRegister = 1;

		// Split matrix layout: per-object world matrix at b0 (re-uploaded per draw), per-view
		// matrices at b12 (only re-uploaded when the camera changes). Keep the register assignments
		// in sync with GraphicsDeviceD3D11 (kPerViewMatrixBufferSlot).
		m_pixelShaderStream
			<< "cbuffer ObjectMatrices : register(b0)\n"
			<< "{\n"
			<< "\tcolumn_major matrix matWorld;\n"
			<< "};\n\n"
			<< "cbuffer ViewMatrices : register(b12)\n"
			<< "{\n"
			<< "\tcolumn_major matrix matView;\n"
			<< "\tcolumn_major matrix matProj;\n"
			<< "\tcolumn_major matrix matInvView;\n"
			<< "\tcolumn_major matrix matInvProj;\n"
			<< "};\n\n";

		m_pixelShaderStream
			<< "cbuffer CameraParameters : register(b" << bufferRegister++ << ")\n"
			<< "{\n"
			// MUST stay field-for-field in sync with PsCameraConstantBuffer in scene.cpp.
			<< "\tfloat3 cameraPos;	// Camera position in world space\n"
			<< "\tfloat fogDensity;	// Height fog extinction per metre at fogBaseHeight; 0 = off\n"
			<< "\tfloat fogHeightFalloff;\n"
			<< "\tfloat3 fogTint;	// Ambient radiance of the fog\n"
			<< "\trow_major matrix inverseCameraView;	// Inverse view matrix\n"
			<< "\tfloat time;		// Time in seconds since game start\n"
			<< "\tfloat fogBaseHeight;\n"
			<< "\tfloat fogAnisotropy;\n"
			<< "\tfloat _cameraPadding0;\n"
			<< "\tfloat3 sunDirection;	// World-space direction toward the sun (normalised)\n"
			<< "\tfloat sunIntensity;\n"
			<< "\tfloat3 sunColor;\n"
			<< "\tfloat forwardOutputLinear;	// 1 inside DeferredRenderer's forward pass: output linear HDR\n"
			<< "\tfloat3 ambientColor;\n"
			<< "\tfloat _forwardPad1;\n"
			<< "\tfloat3 sunScatterColor;\n"
			<< "\tfloat shaftStrength;\n"
			<< "};\n\n";

		if (type == PixelShaderType::Forward)
		{
			// Height fog, identical to AtmosphereCommon.hlsli (and atmosphere_math.h). Change all three
			// together. Emitted after the cbuffer because the functions read its fields.
			m_pixelShaderStream
				<< "static const float FOG_MAX_DENSITY_EXPONENT = 12.0;\n\n"
				<< "float FogDensityAt(float y) {\n"
				<< "\treturn fogDensity * exp(min(-fogHeightFalloff * (y - fogBaseHeight), FOG_MAX_DENSITY_EXPONENT));\n"
				<< "}\n\n"
				<< "float FogOpticalDepth(float originY, float dirY, float len) {\n"
				<< "\tfloat sigmaStart = FogDensityAt(originY);\n"
				<< "\tfloat sigmaEnd = FogDensityAt(originY + dirY * len);\n"
				<< "\tfloat k = fogHeightFalloff * dirY * len;\n"
				<< "\tif (abs(k) < 1e-3) { return len * 0.5 * (sigmaStart + sigmaEnd); }\n"
				<< "\treturn len * (sigmaStart - sigmaEnd) / k;\n"
				<< "}\n\n"
				<< "float FogScatterPhase(float cosTheta) {\n"
				<< "\tfloat g2 = fogAnisotropy * fogAnisotropy;\n"
				<< "\tfloat hg = (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * fogAnisotropy * cosTheta, 1e-4), 1.5);\n"
				<< "\treturn lerp(1.0, hg, 0.8);\n"
				<< "}\n\n"
				<< "float3 ApplyHeightFog(float3 color, float3 worldPos) {\n"
				<< "\tfloat3 toPixel = worldPos - cameraPos;\n"
				<< "\tfloat dist = length(toPixel);\n"
				<< "\tfloat3 dir = toPixel / max(dist, 1e-4);\n"
				<< "\tfloat transmittance = exp(-FogOpticalDepth(cameraPos.y, dir.y, dist));\n"
				<< "\tfloat3 source = fogTint + sunScatterColor * sunColor * sunIntensity * shaftStrength * FogScatterPhase(dot(dir, sunDirection));\n"
				<< "\treturn color * transmittance + source * (1.0 - transmittance);\n"
				<< "}\n\n";
		}

		// Global shader parameters - a single project-wide constant buffer shared by every material.
		// It lives at a fixed reserved register (kGlobalShaderParametersPsSlot) so it never disturbs
		// the dynamically assigned per-material parameter buffers below. The full registry set is
		// declared (vectors first, then scalars) so the cbuffer offsets match the CPU buffer layout
		// built by GlobalShaderParameters::BuildBufferData.
		if (m_usesGlobalParameters)
		{
			const auto sanitize = [](std::string_view name) -> String
			{
				String copy(name);
				std::replace(copy.begin(), copy.end(), ' ', '_');
				return copy;
			};

			const auto& globalParams = GlobalShaderParameters::Get().GetParameters();

			m_pixelShaderStream
				<< "cbuffer GlobalParameters : register(b" << kGlobalShaderParametersPsSlot << ")\n"
				<< "{\n";

			bool wroteAny = false;
			for (const auto& param : globalParams)
			{
				if (param.type == global_shader_parameter_type::Vector)
				{
					m_pixelShaderStream << "\tfloat4 g_" << sanitize(param.name) << ";\n";
					wroteAny = true;
				}
			}
			for (const auto& param : globalParams)
			{
				if (param.type == global_shader_parameter_type::Scalar)
				{
					m_pixelShaderStream << "\tfloat g_" << sanitize(param.name) << ";\n";
					wroteAny = true;
				}
			}

			if (!wroteAny)
			{
				// The material references a global that no longer exists in the registry. Emit a pad
				// so the cbuffer stays valid; the dangling reference will fail to compile and warn.
				m_pixelShaderStream << "\tfloat4 _globalPad;\n";
			}

			m_pixelShaderStream
				<< "};\n\n";
		}

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

		if (m_lit && type != PixelShaderType::UI)
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
			// texparamN is declared without an explicit register, so fxc assigns t0, t1, ... in
			// declaration order. Overrunning into the reserved scene color/depth registers does
			// not fail to compile: the deferred renderer binds and unbinds those slots every
			// frame, so the overflowing textures silently read as black at draw time. That is
			// almost impossible to diagnose from the image, so say so loudly here.
			if (textureParams.size() > kMaxMaterialTextureParameters)
			{
				ELOG("Material declares " << textureParams.size() << " texture parameters, but only "
					<< kMaxMaterialTextureParameters << " fit before the reserved scene color (t"
					<< kSceneColorTextureSlot << ") and scene depth (t" << kSceneDepthTextureSlot
					<< ") registers. The last " << (textureParams.size() - kMaxMaterialTextureParameters)
					<< " will be unbound at draw time and sample black. Reduce the count - note that "
					<< "sampling one texture parameter at several UVs costs only a single slot.");
			}

			for (size_t i = 0; i < textureParams.size(); ++i)
			{
				m_material->AddTextureParameter(textureParams[i].name, textureParams[i].texture);
				m_pixelShaderStream
					<< "// " << textureParams[i].name << "\n"
					<< "Texture2D texparam" << i << ";\n"
					<< "SamplerState paramsampler" << i << ";\n\n";
			}
		}

		if (m_needsSceneDepth)
		{
			// Reserved slot for the opaque scene depth (G-buffer normal RT alpha channel).
			// The engine binds this before drawing translucent objects.
			m_pixelShaderStream
				<< "// Scene depth texture (G-buffer normal alpha)\n"
				<< "Texture2D sceneDepthTex : register(t" << kSceneDepthTextureSlot << ");\n\n";
		}

		if (m_needsSceneColor)
		{
			// Reserved slot for the lit opaque scene color, captured before the translucent pass.
			// The engine binds this before drawing translucent objects (used for refraction).
			m_pixelShaderStream
				<< "// Scene color texture (lit opaque scene captured before translucent pass)\n"
				<< "Texture2D sceneColorTex : register(t" << kSceneColorTextureSlot << ");\n\n";
		}

		if (m_needsScreenSpaceReflection)
		{
			// Screen-space reflection helpers, emitted only for materials using the node so every
			// other material keeps byte-identical shader code. See the comments inside the HLSL.
			m_pixelShaderStream
				<< "// Projects a world position to the scene targets. Returns false when the point is behind the\n"
				<< "// camera or off screen. rayDistance is RADIAL distance from the camera, matching what the\n"
				<< "// G-buffer stores (length(viewPos)); comparing against view-space Z instead would skew hits\n"
				<< "// toward the screen edges.\n"
				<< "bool SSRProject(float3 samplePos, float2 targetSize, out int2 pixel, out float2 ndc, out float rayDistance)\n"
				<< "{\n"
				<< "\tpixel = int2(0, 0);\n"
				<< "\tndc = float2(0.0f, 0.0f);\n"
				<< "\trayDistance = 0.0f;\n"
				<< "\n"
				<< "\tfloat4 sampleView = mul(float4(samplePos, 1.0f), matView);\n"
				<< "\tfloat4 clipPos = mul(sampleView, matProj);\n"
				<< "\tif (clipPos.w <= 0.0001f)\n"
				<< "\t{\n"
				<< "\t\treturn false;\n"
				<< "\t}\n"
				<< "\n"
				<< "\tndc = clipPos.xy / clipPos.w;\n"
				<< "\tif (abs(ndc.x) > 1.0f || abs(ndc.y) > 1.0f)\n"
				<< "\t{\n"
				<< "\t\treturn false;\n"
				<< "\t}\n"
				<< "\n"
				<< "\tfloat2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);\n"
				<< "\tpixel = (int2)(uv * targetSize);\n"
				<< "\trayDistance = length(sampleView.xyz);\n"
				<< "\treturn true;\n"
				<< "}\n"
				<< "\n"
				<< "// Marches the reflected ray through the opaque scene. Returns rgb = reflected colour and\n"
				<< "// a = confidence in [0,1]. A zero confidence is a miss; the caller blends to its own fallback.\n"
				<< "float4 ComputeSSR(float3 worldPos, float3 worldNormal, float maxDistance, float steps)\n"
				<< "{\n"
				<< "\tfloat3 toPixel = normalize(worldPos - cameraPos);\n"
				<< "\tfloat3 reflectDir = reflect(toPixel, worldNormal);\n"
				<< "\n"
				<< "\t// A ray heading back down into the surface can never reach anything the opaque pass\n"
				<< "\t// recorded, so do not pay for the march.\n"
				<< "\tif (reflectDir.y <= 0.001f)\n"
				<< "\t{\n"
				<< "\t\treturn float4(0.0f, 0.0f, 0.0f, 0.0f);\n"
				<< "\t}\n"
				<< "\n"
				<< "\tuint targetWidth;\n"
				<< "\tuint targetHeight;\n"
				<< "\tsceneDepthTex.GetDimensions(targetWidth, targetHeight);\n"
				<< "\tfloat2 targetSize = float2((float)targetWidth, (float)targetHeight);\n"
				<< "\n"
				<< "\tint marchSteps = (int)clamp(steps, 4.0f, 64.0f);\n"
				<< "\tfloat safeMaxDistance = max(maxDistance, 0.001f);\n"
				<< "\tfloat stepLength = safeMaxDistance / (float)marchSteps;\n"
				<< "\n"
				<< "\t// How far behind recorded geometry a ray may be and still count as touching it.\n"
				<< "\tfloat thickness = stepLength * 1.5f;\n"
				<< "\tfloat previousT = 0.0f;\n"
				<< "\n"
				<< "\t[loop]\n"
				<< "\tfor (int i = 1; i <= marchSteps; ++i)\n"
				<< "\t{\n"
				<< "\t\tfloat t = stepLength * (float)i;\n"
				<< "\n"
				<< "\t\tint2 pixel;\n"
				<< "\t\tfloat2 ndc;\n"
				<< "\t\tfloat rayDistance;\n"
				<< "\t\tif (!SSRProject(worldPos + reflectDir * t, targetSize, pixel, ndc, rayDistance))\n"
				<< "\t\t{\n"
				<< "\t\t\tbreak;\n"
				<< "\t\t}\n"
				<< "\n"
				<< "\t\t// Depth 0 is sky or anything else the opaque pass never wrote; it cannot be hit.\n"
				<< "\t\tfloat sceneDistance = sceneDepthTex.Load(int3(pixel, 0)).a;\n"
				<< "\t\tif (sceneDistance > 0.0001f && rayDistance > sceneDistance)\n"
				<< "\t\t{\n"
				<< "\t\t\t// The ray crossed behind recorded geometry somewhere between the previous step and\n"
				<< "\t\t\t// this one. Binary search that interval for the actual crossing: the coarse step alone\n"
				<< "\t\t\t// lands up to a whole step late, which smears the reflection into vertical streaks and\n"
				<< "\t\t\t// makes neighbouring pixels disagree about whether they hit at all.\n"
				<< "\t\t\tfloat lo = previousT;\n"
				<< "\t\t\tfloat hi = t;\n"
				<< "\t\t\tint2 hitPixel = pixel;\n"
				<< "\t\t\tfloat2 hitNdc = ndc;\n"
				<< "\t\t\tfloat hitDelta = rayDistance - sceneDistance;\n"
				<< "\n"
				<< "\t\t\t[loop]\n"
				<< "\t\t\tfor (int r = 0; r < 5; ++r)\n"
				<< "\t\t\t{\n"
				<< "\t\t\t\tfloat mid = 0.5f * (lo + hi);\n"
				<< "\n"
				<< "\t\t\t\tint2 midPixel;\n"
				<< "\t\t\t\tfloat2 midNdc;\n"
				<< "\t\t\t\tfloat midDistance;\n"
				<< "\t\t\t\tif (!SSRProject(worldPos + reflectDir * mid, targetSize, midPixel, midNdc, midDistance))\n"
				<< "\t\t\t\t{\n"
				<< "\t\t\t\t\tlo = mid;\n"
				<< "\t\t\t\t\tcontinue;\n"
				<< "\t\t\t\t}\n"
				<< "\n"
				<< "\t\t\t\tfloat midScene = sceneDepthTex.Load(int3(midPixel, 0)).a;\n"
				<< "\t\t\t\tif (midScene > 0.0001f && midDistance > midScene)\n"
				<< "\t\t\t\t{\n"
				<< "\t\t\t\t\thi = mid;\n"
				<< "\t\t\t\t\thitPixel = midPixel;\n"
				<< "\t\t\t\t\thitNdc = midNdc;\n"
				<< "\t\t\t\t\thitDelta = midDistance - midScene;\n"
				<< "\t\t\t\t}\n"
				<< "\t\t\t\telse\n"
				<< "\t\t\t\t{\n"
				<< "\t\t\t\t\tlo = mid;\n"
				<< "\t\t\t\t}\n"
				<< "\t\t\t}\n"
				<< "\n"
				<< "\t\t\tif (hitDelta < thickness)\n"
				<< "\t\t\t{\n"
				<< "\t\t\t\tfloat3 hitColor = sceneColorTex.Load(int3(hitPixel, 0)).rgb;\n"
				<< "\n"
				<< "\t\t\t\t// Confidence instead of an all-or-nothing hit, so the blend toward the caller's\n"
				<< "\t\t\t\t// fallback is soft:\n"
				<< "\t\t\t\t//  - only a thin fade at the left/right edges; a wide one drops to the fallback\n"
				<< "\t\t\t\t//    colour and paints a visible light column down the side of the screen,\n"
				<< "\t\t\t\t//  - a wide fade toward the top edge, which is where reflected rays actually leave,\n"
				<< "\t\t\t\t//  - hits far along the ray are trusted less,\n"
				<< "\t\t\t\t//  - hits that only just fit inside the thickness window are trusted less.\n"
				<< "\t\t\t\tfloat sideFade = smoothstep(0.0f, 0.05f, 1.0f - abs(hitNdc.x));\n"
				<< "\t\t\t\tfloat topFade = smoothstep(0.0f, 0.25f, 1.0f - hitNdc.y);\n"
				<< "\t\t\t\tfloat bottomFade = smoothstep(0.0f, 0.05f, 1.0f + hitNdc.y);\n"
				<< "\t\t\t\tfloat distanceFade = 1.0f - smoothstep(0.6f, 1.0f, hi / safeMaxDistance);\n"
				<< "\t\t\t\tfloat depthFit = 1.0f - saturate(hitDelta / thickness);\n"
				<< "\n"
				<< "\t\t\t\tfloat confidence = saturate(sideFade * topFade * bottomFade * distanceFade * (0.35f + 0.65f * depthFit));\n"
				<< "\t\t\t\treturn float4(hitColor, confidence);\n"
				<< "\t\t\t}\n"
				<< "\n"
				<< "\t\t\t// The ray passed further behind this geometry than its assumed thickness - thin\n"
				<< "\t\t\t// foliage, a pole. Keep marching: it may still hit something further along.\n"
				<< "\t\t}\n"
				<< "\n"
				<< "\t\tpreviousT = t;\n"
				<< "\t}\n"
				<< "\n"
				<< "\treturn float4(0.0f, 0.0f, 0.0f, 0.0f);\n"
				<< "}\n"
				<< "\n";
		}

		if (m_lit && type != PixelShaderType::UI)
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

		if (m_lit && type != PixelShaderType::UI)
		{
			m_pixelShaderStream << "\tfloat3 N = normalize(input.normal);\n\n";
		}

		m_pixelShaderStream << "\tfloat3 V = normalize(input.viewDir);\n\n";

		if (m_lit && type != PixelShaderType::UI)
		{
			m_pixelShaderStream
				<< "\tfloat3 B = normalize(input.binormal);\n"
				<< "\tfloat3 T = normalize(input.tangent);\n"
				<< "\tfloat3x3 TBN = float3x3(T, B, N);\n";
		}
		else
		{
			// Unlit and UI variants carry no normal, binormal or tangent in VertexOut, so there is
			// no per-pixel basis to build. AddTransform still references TBN for tangent-space
			// transforms, though, and every variant emits every expression - without this, any
			// material using a Tangent-space Transform Vector node fails its UI compile, and an
			// unlit one fails every variant. Treat tangent space as world space here: it is the only
			// basis these variants can honestly offer.
			m_pixelShaderStream
				<< "\tstatic const float3x3 TBN = float3x3(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);\n";
		}

		for (size_t exprIndex = 0; exprIndex < m_expressions.size(); ++exprIndex)
		{
			// When debug comments are enabled, prefix each numbered expression with a comment that
			// maps it back to the originating node in the material graph. This makes it much easier
			// to align the generated expr_N statements with the visual node graph while debugging.
			if (m_generateDebugComments)
			{
				if (const String* comment = GetExpressionComment(static_cast<ExpressionIndex>(exprIndex)))
				{
					m_pixelShaderStream << "\t// expr_" << exprIndex << "  <-  " << *comment << "\n";
				}
			}

			m_pixelShaderStream << "\t" << m_expressions[exprIndex];
		}

		if (type != PixelShaderType::ShadowMap && type != PixelShaderType::UI)
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
			if (type != PixelShaderType::UI)
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

			// Emissive color
			m_pixelShaderStream << "\tfloat3 emissiveColor = float3(0.0, 0.0, 0.0);\n\n";
			if (m_emissiveExpression != IndexNone)
			{
				const auto expressionType = GetExpressionType(m_emissiveExpression);
				if (expressionType == ExpressionType::Float_1 || expressionType == ExpressionType::Float_3)
				{
					m_pixelShaderStream << "\temissiveColor = expr_" << m_emissiveExpression << ";\n\n";
				}
				else
				{
					if (expressionType == ExpressionType::Float_2)
					{
						m_pixelShaderStream << "\temissiveColor = float3(expr_" << m_emissiveExpression << ", 1.0);\n\n";
					}
					else if (expressionType == ExpressionType::Float_4)
					{
						m_pixelShaderStream << "\temissiveColor = expr_" << m_emissiveExpression << ".rgb;\n\n";
					}
				}
			}
		}

		if (type != PixelShaderType::GBuffer)
		{
			if (m_translucent)
			{
				// Translucent materials blend smoothly via alpha, so only cull fully transparent
				// pixels. Using the 0.333 alpha-test cutoff here would punch holes in soft edges
				// such as depth-faded water shorelines, making low-opacity water vanish entirely.
				m_pixelShaderStream
					<< "\tif (opacity <= 0.0) discard;\n";
			}
			else
			{
				m_pixelShaderStream
					<< "\tif (opacity <= 0.333) discard;\n";
			}
		}
		else
		{
			m_pixelShaderStream
				//<< "\tfloat threshold = Dither8x8(input.pos.xy + cameraPos.xy * cameraPos.z);\n"
				<< "\tif (opacity < 0.333) discard;\n";
		}

		if (type == PixelShaderType::UI)
		{
			m_pixelShaderStream
				<< "\toutputColor = float4(emissiveColor, opacity);\n";

			// End of main function
			m_pixelShaderStream
				<< "\treturn outputColor;\n"
				<< "}"
				<< std::endl;
		}
		else if (type != PixelShaderType::ShadowMap)
		{
			if (type != PixelShaderType::GBuffer)
			{
				if (m_lit)
				{
					//m_pixelShaderStream << "\tbaseColor = pow(baseColor, 2.2);\n";
				}
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

					// Sun direction, colour and intensity come from the CameraParameters
					// cbuffer (rows 7-9), populated by Scene::RefreshCameraBuffer from the
					// scene's primary directional light.  This replaces the old hardcoded
					// values and makes forward-rendered objects react to the real sky.
					m_pixelShaderStream
						<< "\tfloat3 L = sunDirection;\n"					// toward-sun unit vector
						<< "\tfloat3 H = normalize(V + L);\n"
						<< "\tfloat3 radiance = sunColor * sunIntensity;\n\n";

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
						<< "\tfloat denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
						<< "\tfloat3 specularity = numerator / denominator;\n";

					m_pixelShaderStream
						<< "\tfloat NdotL = max(dot(N, L), 0.0);\n"
						<< "\tfloat3 Lo = (kD * baseColor / PI + specularity) * radiance * NdotL;\n";

					m_pixelShaderStream
						<< "\tkS = fresnelSchlick(max(dot(N, V), 0.0), F0);\n"
						<< "\tkD = 1.0f.xxx - kS;\n"
						<< "\tkD *= 1.0 - metallic;\n"
						// Use real scene ambient instead of the old static sky tint
						<< "\tfloat3 diffuse = ambientColor * baseColor;\n"
						<< "\tfloat3 ambient = (kD * diffuse) * ao;\n";

					m_pixelShaderStream
						<< "\tfloat3 color = ambient + Lo;\n";

					// Add emissive color additively. Distance fog and tone mapping are applied
					// further below in the shared forward output block (after the lit/unlit
					// branch) so that translucent materials fog in the exact same colour space
					// as the deferred lighting pass and blend seamlessly into the fogged scene.
					m_pixelShaderStream
						<< "\tcolor += emissiveColor;\n";
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

					// Emissive - use base color + emissive color for unlit materials
					m_pixelShaderStream
						<< "\toutput.emissive = float4(baseColor + emissiveColor, 0.0);\n";
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
						<< "\toutput.emissive = float4(emissiveColor, 0.0);\n";
				}

				// Return G-Buffer output
				m_pixelShaderStream
					<< "\treturn output;\n"
					<< "}"
					<< std::endl;
			}
			else
			{
				// Forward rendering output. Both lit and unlit materials converge here with a
				// linear-space HDR `color`, so height fog and tone mapping can be applied
				// uniformly and identically to the deferred lighting pass. This is what lets
				// translucent surfaces (water, etc.) fog out to the exact same colour as the
				// opaque scene behind them instead of standing out as un-fogged patches.
				if (!m_lit)
				{
					// Unlit materials (glowing particles, FX sprites …) author display-referred colour:
					// color = pow(baseColor, 2.2), and the Emissive pin is deliberately NOT added (FX
					// graphs often wire the same value into BaseColor and Emissive for the deferred unlit
					// path, which would double it). The colour is inverse-tonemapped to linear so the
					// height fog composes with the scene, then tone-mapped back only when rendering
					// standalone; inside DeferredRenderer the TonemapPass does that for the whole frame.
					m_pixelShaderStream
						<< "\tfloat3 color = ApplyHeightFog(InverseTonemap(pow(baseColor, 2.2)), input.worldPos);\n"
						<< "\tif (forwardOutputLinear < 0.5)\n"
						<< "\t{\n"
						<< "\t\tcolor = pow(ACESFilm(color), (1.0f/2.2f).xxx);\n"
						<< "\t}\n";
				}
				else
				{
					// Height fog in linear HDR, identical to the deferred atmosphere composite, so
					// translucent surfaces fog out to the same colour as the opaque scene behind them.
					m_pixelShaderStream
						<< "\tcolor = ApplyHeightFog(color, input.worldPos);\n";

					// ACES + gamma only when rendering standalone. Inside DeferredRenderer the
					// TonemapPass tone maps the whole frame, so the colour stays linear here.
					m_pixelShaderStream
						<< "\tif (forwardOutputLinear < 0.5)\n"
						<< "\t{\n"
						<< "\t\tcolor = ACESFilm(color);\n"
						<< "\t\tcolor = pow(color, (1.0f/2.2f).xxx);\n"
						<< "\t}\n";
				}

				m_pixelShaderStream
					<< "\toutputColor = float4(color, opacity);\n";

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

#if 0
#	ifdef _DEBUG
		// Write shader output to asset registry for debug
		if (const auto filePtr = AssetRegistry::CreateNewFile(type == PixelShaderType::Forward ? "PSForward.hlsl" : type == PixelShaderType::GBuffer ? "PSDeferred.hlsl" : type == PixelShaderType::ShadowMap ? "PSShadow.hlsl" : "PSUI.hlsl"))
		{
			io::StreamSink sink(*filePtr);
			io::TextWriter<char> writer(sink);
			writer.write(m_pixelShaderCode[(int)type]);
		}
#	endif
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

		outputStream.flush();

		if (type == SamplerType::Normal)
		{
			// First, add the raw sample as a temporary expression
			const ExpressionIndex rawSample = AddExpression(outputStream.str(), ExpressionType::Float_4);

			// Reconstruct the normal: read RG, remap to [-1,1], derive Z = sqrt(1 - x^2 - y^2)
			// This works correctly for both BC5/RG8 (where B=0) and standard RGBA normal maps
			std::ostringstream normalStream;
			normalStream << "float4(expr_" << rawSample << ".rg * 2.0 - 1.0, "
				<< "sqrt(saturate(1.0 - dot(expr_" << rawSample << ".rg * 2.0 - 1.0, expr_" << rawSample << ".rg * 2.0 - 1.0))), 0.0)";
			normalStream.flush();

			return AddExpression(normalStream.str(), ExpressionType::Float_4);
		}

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
		outputStream << "v" << copy;
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_4);
	}

	ExpressionIndex MaterialCompilerD3D11::AddGlobalScalarParameterExpression(std::string_view name)
	{
		if (name.empty())
		{
			WLOG("Missing name for global scalar parameter");
			return IndexNone;
		}

		m_usesGlobalParameters = true;

		String copy(name);
		std::replace(copy.begin(), copy.end(), ' ', '_');

		std::ostringstream outputStream;
		outputStream << "g_" << copy;
		outputStream.flush();

		return AddExpression(outputStream.str(), ExpressionType::Float_1);
	}

	ExpressionIndex MaterialCompilerD3D11::AddGlobalVectorParameterExpression(std::string_view name)
	{
		if (name.empty())
		{
			WLOG("Missing name for global vector parameter");
			return IndexNone;
		}

		m_usesGlobalParameters = true;

		String copy(name);
		std::replace(copy.begin(), copy.end(), ' ', '_');

		std::ostringstream outputStream;
		outputStream << "g_" << copy;
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
			<< "\tfloat4 color : COLOR;\n";

		if (type != VertexShaderType::UI)
		{
			vertexShaderStream
				<< "\tfloat3 normal : NORMAL;\n"
				<< "\tfloat3 binormal : BINORMAL;\n"
				<< "\tfloat3 tangent : TANGENT;\n";
		}

		const uint32 numTexCoords = (type == VertexShaderType::UI) ? 1u : m_numTexCoordinates;

		for (uint32 i = 0; i < numTexCoords; ++i)
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

		// Instance data for instanced rendering
		if (type == VertexShaderType::Instanced)
		{
			vertexShaderStream
				<< "\tcolumn_major float4x4 instanceWorld : TEXCOORD8;\n"
				<< "\tfloat4 instanceColor : TEXCOORD12;\n";
		}

		vertexShaderStream
			<< "};\n\n";

		// VertexOut struct
		vertexShaderStream
			<< "struct VertexOut\n"
			<< "{\n"
			<< "\tfloat4 pos : SV_POSITION;\n"
			<< "\tfloat4 color : COLOR;\n";

		if (m_lit && type != VertexShaderType::UI)
		{
			vertexShaderStream
				<< "\tfloat3 normal : NORMAL;\n"
				<< "\tfloat3 binormal : BINORMAL;\n"
				<< "\tfloat3 tangent : TANGENT;\n";
		}

		for (uint32 i = 0; i < numTexCoords; ++i)
		{
			vertexShaderStream
				<< "\tfloat2 uv" << i << " : TEXCOORD" << i << ";\n";
		}

		vertexShaderStream
			<< "\tfloat3 worldPos : TEXCOORD" << numTexCoords << ";\n"
			<< "\tfloat3 viewDir : TEXCOORD" << numTexCoords + 1 << ";\n"
			<< "\tfloat3 viewPos : TEXCOORD" << numTexCoords + 2 << ";\n";

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

		// Matrix constant buffers: per-object world matrix at b0 (re-uploaded per draw), per-view
		// matrices at b12 (only re-uploaded when the camera changes). Keep the register assignments
		// in sync with GraphicsDeviceD3D11 (kPerViewMatrixBufferSlot).
		vertexShaderStream
			<< "cbuffer ObjectMatrices : register(b0)\n"
			<< "{\n"
			<< "\tcolumn_major matrix matWorld;\n"
			<< "};\n\n"
			<< "cbuffer ViewMatrices : register(b12)\n"
			<< "{\n"
			<< "\tcolumn_major matrix matView;\n"
			<< "\tcolumn_major matrix matProj;\n"
			<< "\tcolumn_major matrix matInvView;\n"
			<< "\tcolumn_major matrix matInvProj;\n"
			<< "};\n\n";

		if (numBones > 0)
		{
			// Bone matrices are bound via RenderOperation::vertexConstantBuffers starting at slot 1
			// (see GraphicsDeviceD3D11::Render), so declare the register explicitly.
			vertexShaderStream
				<< "cbuffer Bones : register(b1)\n"
				<< "{\n"
				<< "\tcolumn_major matrix matBone[" << numBones << "];\n"	// TODO: Dual quaternion support
				<< "};\n\n";
		}

		// Main procedure start
		vertexShaderStream
			<< "VertexOut main(VertexIn input)\n"
			<< "{\n"
			<< "\tVertexOut output;\n\n";

		if (const bool withSkinning = (type == VertexShaderType::SkinnedLow || type == VertexShaderType::SkinnedMedium || type == VertexShaderType::SkinnedHigh))
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

			if (m_lit && type != VertexShaderType::UI)
			{
				vertexShaderStream
					<< "\tfloat3 transformedNormal = input.normal;\n"
					<< "\tfloat3 transformedBinormal = input.binormal;\n"
					<< "\tfloat3 transformedTangent = input.tangent;\n\n";
			}
		}

		// Basic transformations - use instance world matrix for instanced rendering
		if (type == VertexShaderType::Instanced)
		{
			vertexShaderStream
				<< "\toutput.pos = mul(transformedPos, input.instanceWorld);\n"
				<< "\toutput.worldPos = output.pos.xyz;\n"
				<< "\toutput.viewDir = normalize(matInvView[3].xyz - output.worldPos);\n"
				<< "\toutput.pos = mul(output.pos, matView);\n"
				<< "\toutput.viewPos = output.pos;\n"
				<< "\toutput.pos = mul(output.pos, matProj);\n"
				<< "\toutput.color = input.color * input.instanceColor;\n";

			if (m_lit)
			{
				vertexShaderStream
					<< "\toutput.binormal = normalize(mul(normalize(transformedBinormal), (float3x3)input.instanceWorld));\n"
					<< "\toutput.tangent = normalize(mul(normalize(transformedTangent), (float3x3)input.instanceWorld));\n"
					<< "\toutput.normal = normalize(mul(normalize(transformedNormal), (float3x3)input.instanceWorld));\n";
			}
		}
		else
		{
			vertexShaderStream
				<< "\toutput.pos = mul(transformedPos, matWorld);\n"
				<< "\toutput.worldPos = output.pos.xyz;\n"
				<< "\toutput.viewDir = normalize(matInvView[3].xyz - output.worldPos);\n"
				<< "\toutput.pos = mul(output.pos, matView);\n"
				<< "\toutput.viewPos = output.pos;\n"
				<< "\toutput.pos = mul(output.pos, matProj);\n"
				<< "\toutput.color = input.color;\n";

			if (m_lit && type != VertexShaderType::UI)
			{
				vertexShaderStream
					<< "\toutput.binormal = normalize(mul(normalize(transformedBinormal), (float3x3)matWorld));\n"
					<< "\toutput.tangent = normalize(mul(normalize(transformedTangent), (float3x3)matWorld));\n"
					<< "\toutput.normal = normalize(mul(normalize(transformedNormal), (float3x3)matWorld));\n";
			}
		}

		for (uint32 i = 0; i < m_numTexCoordinates; ++i)
		{
			vertexShaderStream
				<< "\toutput.uv" << i << " = input.uv" << i << ";\n";
		}

		// Main procedure end
		vertexShaderStream
			<< "\n\treturn output;\n"
			<< "}\n"
			<< std::endl;

		m_vertexShaderCode = vertexShaderStream.str();
		vertexShaderStream.clear();

#if 0
#ifdef _DEBUG
		// Write shader output to asset registry for debug
		if (const auto filePtr = AssetRegistry::CreateNewFile("VS_" + std::to_string(static_cast<int>(type)) + ".hlsl"))
		{
			io::StreamSink sink(*filePtr);
			io::TextWriter<char> writer(sink);
			writer.write(m_vertexShaderCode);
		}
#endif
#endif
	}
}
