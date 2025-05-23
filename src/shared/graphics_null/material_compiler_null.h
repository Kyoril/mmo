// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/material_compiler.h"

namespace mmo
{
	/// @brief Default implementation of a MaterialCompiler which does not do anything.
	class MaterialCompilerNull final : public MaterialCompiler
	{
	public:
		/// @copydoc MaterialCompiler::AddGlobalFunction
		void AddGlobalFunction(std::string_view name, std::string_view code) override;
		
		/// @copydoc MaterialCompiler::AddExpression
		ExpressionIndex AddExpression(std::string_view code, ExpressionType type) override;
		
		/// @copydoc MaterialCompiler::NotifyTextureCoordinateIndex
		void NotifyTextureCoordinateIndex(uint32 textureCoordinateIndex) override;
		
		/// @copydoc MaterialCompiler::AddTextureCoordinate
		ExpressionIndex AddTextureCoordinate(int32 coordinateIndex) override;
		
		/// @copydoc MaterialCompiler::AddTextureSample
		ExpressionIndex AddTextureSample(std::string_view texture, ExpressionIndex coordinates, bool srgb, SamplerType type) override;
		
		/// @copydoc MaterialCompiler::AddMultiply
		ExpressionIndex AddMultiply(ExpressionIndex first, ExpressionIndex second) override;
		
		/// @copydoc MaterialCompiler::AddAddition
		ExpressionIndex AddAddition(ExpressionIndex first, ExpressionIndex second) override;
		
		/// @copydoc MaterialCompiler::AddSubtract
		ExpressionIndex AddSubtract(ExpressionIndex first, ExpressionIndex second) override;

		/// @copydoc MaterialCompiler::AddLerp
		ExpressionIndex AddLerp(ExpressionIndex first, ExpressionIndex second, ExpressionIndex alpha) override;
		
		/// @copydoc MaterialCompiler::AddDot
		ExpressionIndex AddDot(ExpressionIndex first, ExpressionIndex second) override;
		
		/// @copydoc MaterialCompiler::AddClamp
		ExpressionIndex AddClamp(ExpressionIndex base, ExpressionIndex min, ExpressionIndex max) override;
		
		/// @copydoc MaterialCompiler::AddOneMinus
		ExpressionIndex AddOneMinus(ExpressionIndex input) override;
		
		/// @copydoc MaterialCompiler::AddPower
		ExpressionIndex AddPower(ExpressionIndex base, ExpressionIndex exponent) override;
		
		/// @copydoc MaterialCompiler::AddWorldPosition
		ExpressionIndex AddWorldPosition() override;

		/// @copydoc MaterialCompiler::AddCameraVector
		ExpressionIndex AddCameraVector() override;

		/// @copydoc MaterialCompiler::AddMask
		ExpressionIndex AddMask(ExpressionIndex input, bool r, bool g, bool b, bool a) override;
		
		/// @copydoc MaterialCompiler::AddVertexNormal
		ExpressionIndex AddVertexNormal() override;
		
		/// @copydoc MaterialCompiler::AddDivide
		ExpressionIndex AddDivide(ExpressionIndex first, ExpressionIndex second) override;
		
		/// @copydoc MaterialCompiler::AddAbs
		ExpressionIndex AddAbs(ExpressionIndex input) override;
		
		/// @copydoc MaterialCompiler::AddNormalize
		ExpressionIndex AddNormalize(ExpressionIndex input) override;
		
		/// @copydoc MaterialCompiler::AddVertexColor
		ExpressionIndex AddVertexColor() override;
		
		/// @copydoc MaterialCompiler::AddAppend
		ExpressionIndex AddAppend(ExpressionIndex first, ExpressionIndex second) override;

		/// @copydoc MaterialCompiler::AddTransform
		ExpressionIndex AddTransform(ExpressionIndex input, Space sourceSpace, Space targetSpace) override;

	protected:
		/// @copydoc MaterialCompiler::GenerateVertexShaderCode
		void GenerateVertexShaderCode(VertexShaderType type) override;
		
		/// @copydoc MaterialCompiler::GeneratePixelShaderCode
		void GeneratePixelShaderCode(PixelShaderType type = PixelShaderType::Forward) override;

	public:
		ExpressionIndex AddTextureParameterSample(std::string_view name, std::string_view texture,
			ExpressionIndex coordinates, bool srgb, SamplerType type) override;

		ExpressionIndex AddScalarParameterExpression(std::string_view name, float defaultValue) override;

		ExpressionIndex AddVectorParameterExpression(std::string_view name, const Vector4& defaultValue) override;
	};
}
