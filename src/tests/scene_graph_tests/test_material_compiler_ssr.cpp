// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#ifdef _WIN32

#include "graphics/material.h"
#include "graphics/shader_compiler.h"
#include "graphics_d3d11/material_compiler_d3d11.h"
#include "graphics_d3d11/shader_compiler_d3d11.h"

using namespace mmo;

// The screen-space reflection helper is HLSL assembled from C++ string literals, emitted only when
// a material uses the node. A typo in it compiles the engine fine and only fails when some
// material author saves a water material in the editor. These tests run the real HLSL compiler
// over every generated pixel variant of a material that uses the node, with and without its
// optional inputs connected, so the helper is checked where it is written.

namespace
{
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

	void RequireAllVariantsCompile(const MaterialCompilerD3D11& compiler)
	{
		CHECK(CompilePixelVariant(compiler, PixelShaderType::Forward) == "");
		CHECK(CompilePixelVariant(compiler, PixelShaderType::GBuffer) == "");
		CHECK(CompilePixelVariant(compiler, PixelShaderType::ShadowMap) == "");
		CHECK(CompilePixelVariant(compiler, PixelShaderType::UI) == "");
	}

	void CompileMaterial(MaterialCompilerD3D11& compiler)
	{
		Material material("ScreenSpaceReflectionTest");
		ShaderCompilerD3D11 shaderCompiler;
		compiler.Compile(material, shaderCompiler);
	}
}

TEST_CASE("Screen space reflection compiles with default inputs", "[material_compiler]")
{
	for (const bool lit : { true, false })
	{
		MaterialCompilerD3D11 compiler;
		compiler.SetLit(lit);
		compiler.SetTranslucent(true);

		const ExpressionIndex reflection = compiler.AddScreenSpaceReflection(IndexNone, IndexNone, IndexNone);
		REQUIRE(reflection != IndexNone);
		CHECK(compiler.GetExpressionType(reflection) == ExpressionType::Float_4);

		CompileMaterial(compiler);
		RequireAllVariantsCompile(compiler);
	}
}

TEST_CASE("Screen space reflection compiles with every input connected", "[material_compiler]")
{
	for (const bool lit : { true, false })
	{
		MaterialCompilerD3D11 compiler;
		compiler.SetLit(lit);
		compiler.SetTranslucent(true);

		const ExpressionIndex normal = compiler.AddExpression("float3(0.0, 1.0, 0.0)", ExpressionType::Float_3);
		const ExpressionIndex maxDistance = compiler.AddExpression("150.0", ExpressionType::Float_1);
		const ExpressionIndex steps = compiler.AddExpression("24.0", ExpressionType::Float_1);

		REQUIRE(compiler.AddScreenSpaceReflection(normal, maxDistance, steps) != IndexNone);

		CompileMaterial(compiler);
		RequireAllVariantsCompile(compiler);
	}
}

#endif
