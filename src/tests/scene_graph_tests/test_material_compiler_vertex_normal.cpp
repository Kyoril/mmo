// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#ifdef _WIN32

#include "graphics/material.h"
#include "graphics/shader_compiler.h"
#include "graphics_d3d11/material_compiler_d3d11.h"
#include "graphics_d3d11/shader_compiler_d3d11.h"

using namespace mmo;

// The Vertex Normal and Reflection Vector nodes emit `N`, and a Fresnel node without a normal
// input read `input.normal`. Every generated pixel shader variant includes every expression, but
// `N` was only declared for LIT, non-UI variants and only those carry a normal in VertexOut. So
// every lit mesh material using one of these nodes failed its UI compile with "undeclared
// identifier 'N'", and an unlit one failed every variant.

namespace
{
	/// Compiles one generated pixel shader variant with fxc.
	/// @return The compiler's error text, or an empty string on success.
	String CompilePixelVariant(const MaterialCompilerD3D11& compiler, const PixelShaderType type)
	{
		ShaderCompilerD3D11 shaderCompiler;

		ShaderCompileInput input;
		input.shaderCode = compiler.GetPixelShaderCode(type);
		input.shaderType = ShaderType::PixelShader;

		ShaderCompileResult output;
		shaderCompiler.Compile(input, output);

		return output.succeeded ? String() : (output.errorMessage.empty() ? String("<no message>") : output.errorMessage);
	}

	/// Builds a material that reads the vertex normal directly, through a reflection vector and
	/// through a Fresnel node without a normal input.
	void CompileVertexNormalMaterial(MaterialCompilerD3D11& compiler, const bool lit)
	{
		compiler.SetLit(lit);

		const ExpressionIndex normal = compiler.AddVertexNormal();
		REQUIRE(normal != IndexNone);
		REQUIRE(compiler.AddExpression("reflect(-V, N)", ExpressionType::Float_3) != IndexNone);

		const ExpressionIndex exponent = compiler.AddExpression("5.0", ExpressionType::Float_1);
		const ExpressionIndex base = compiler.AddExpression("0.04", ExpressionType::Float_1);
		const ExpressionIndex fresnel = compiler.AddFresnel(exponent, base, IndexNone);
		REQUIRE(fresnel != IndexNone);
		compiler.SetEmissiveExpression(fresnel);

		Material material("VertexNormalTest");
		ShaderCompilerD3D11 shaderCompiler;
		compiler.Compile(material, shaderCompiler);
	}
}

TEST_CASE("Vertex normal compiles in every lit pixel variant", "[material_compiler]")
{
	MaterialCompilerD3D11 compiler;
	CompileVertexNormalMaterial(compiler, true);

	CHECK(CompilePixelVariant(compiler, PixelShaderType::Forward) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::GBuffer) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::ShadowMap) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::UI) == "");
}

TEST_CASE("Vertex normal compiles in every unlit pixel variant", "[material_compiler]")
{
	MaterialCompilerD3D11 compiler;
	CompileVertexNormalMaterial(compiler, false);

	CHECK(CompilePixelVariant(compiler, PixelShaderType::Forward) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::GBuffer) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::ShadowMap) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::UI) == "");
}

#endif
