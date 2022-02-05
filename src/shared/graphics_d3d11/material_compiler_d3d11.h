// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "graphics/material_compiler.h"

namespace mmo
{
	/// @brief Implementation of the material compiler for D3D11. This compiler generates HLSL code
	///		   for shader model 5.0.
	class MaterialCompilerD3D11 final : public MaterialCompiler
	{
	public:
		/// @brief Creates a new instance of the MaterialCompilerD3D11 class and initialize it.
		MaterialCompilerD3D11() = default;

		/// @brief Default destructor.
		~MaterialCompilerD3D11() override = default;

	public:
		/// @copydoc MaterialCompiler::AddGlobalFunction
		void AddGlobalFunction(std::string_view name, std::string_view code) override;

		/// @copydoc MaterialCompiler::AddExpression
		ExpressionIndex AddExpression(std::string_view code) override;

		/// @copydoc MaterialCompiler::NotifyTextureCoordinateIndex
		void NotifyTextureCoordinateIndex(uint32 textureCoordinateIndex) override;
		
		/// @copydoc MaterialCompiler::SetBaseColorExpression
		void SetBaseColorExpression(ExpressionIndex expression) override;
		
		/// @copydoc MaterialCompiler::AddTextureCoordinate
		ExpressionIndex AddTextureCoordinate(int32 coordinateIndex) override;
		
		/// @copydoc MaterialCompiler::AddTextureSample
		ExpressionIndex AddTextureSample(std::string_view texture, ExpressionIndex coordinates) override;
		
		/// @copydoc MaterialCompiler::AddMultiply
		ExpressionIndex AddMultiply(ExpressionIndex first, ExpressionIndex second) override;
		
		/// @copydoc MaterialCompiler::AddAddition
		ExpressionIndex AddAddition(ExpressionIndex first, ExpressionIndex second) override;
		
		/// @copydoc MaterialCompiler::AddLerp
		ExpressionIndex AddLerp(ExpressionIndex first, ExpressionIndex second, ExpressionIndex alpha) override;
		
		/// @copydoc MaterialCompiler::AddDot
		ExpressionIndex AddDot(ExpressionIndex first, ExpressionIndex second) override;
		
		/// @copydoc MaterialCompiler::AddClamp
		ExpressionIndex AddClamp(ExpressionIndex value, ExpressionIndex min, ExpressionIndex max) override;
		
		/// @copydoc MaterialCompiler::AddOneMinus
		ExpressionIndex AddOneMinus(ExpressionIndex input) override;
		
		/// @copydoc MaterialCompiler::AddPower
		ExpressionIndex AddPower(ExpressionIndex base, ExpressionIndex exponent) override;
		
		/// @copydoc MaterialCompiler::AddWorldPosition
		ExpressionIndex AddWorldPosition() override;
		
		/// @copydoc MaterialCompiler::AddWorldPosition
		ExpressionIndex AddMask(ExpressionIndex input, bool r, bool g, bool b, bool a) override;

	protected:
		/// @copydoc MaterialCompiler::GenerateVertexShaderCode
		void GenerateVertexShaderCode() override;
		
		/// @copydoc MaterialCompiler::GeneratePixelShaderCode
		void GeneratePixelShaderCode() override;
	};
}
