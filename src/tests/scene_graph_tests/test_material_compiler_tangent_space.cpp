// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#ifdef _WIN32

#include "graphics/material.h"
#include "graphics/shader_compiler.h"
#include "graphics_d3d11/material_compiler_d3d11.h"
#include "graphics_d3d11/shader_compiler_d3d11.h"

using namespace mmo;

// AddTransform emits `mul(v, TBN)` / `mul(TBN, v)` for tangent-space transforms, and every
// generated pixel shader variant includes every expression. The per-pixel TBN basis, however,
// was only declared for LIT, non-UI variants - the UI variant's input struct carries no normal,
// binormal or tangent at all. So any material using a Transform Vector node with Tangent space
// failed its UI compile with "undeclared identifier 'TBN'", and an unlit one failed every
// variant. These tests run the real HLSL compiler over each generated variant so the failure is
// caught here rather than in the editor log.

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

	/// Builds a material whose only interesting content is a tangent-to-world transform.
	void CompileTangentTransformMaterial(MaterialCompilerD3D11& compiler, const bool lit)
	{
		compiler.SetLit(lit);

		const ExpressionIndex tangentNormal = compiler.AddExpression("float3(0.0, 0.0, 1.0)", ExpressionType::Float_3);
		REQUIRE(tangentNormal != IndexNone);
		REQUIRE(compiler.AddTransform(tangentNormal, Space::Tangent, Space::World) != IndexNone);

		Material material("TangentTransformTest");
		ShaderCompilerD3D11 shaderCompiler;
		compiler.Compile(material, shaderCompiler);
	}
}

TEST_CASE("Tangent space transform compiles in every lit pixel variant", "[material_compiler]")
{
	MaterialCompilerD3D11 compiler;
	CompileTangentTransformMaterial(compiler, true);

	CHECK(CompilePixelVariant(compiler, PixelShaderType::Forward) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::GBuffer) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::ShadowMap) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::UI) == "");
}

TEST_CASE("Tangent space transform compiles in every unlit pixel variant", "[material_compiler]")
{
	// Unlit variants carry no per-pixel basis either, so the same reference must still compile.
	MaterialCompilerD3D11 compiler;
	CompileTangentTransformMaterial(compiler, false);

	CHECK(CompilePixelVariant(compiler, PixelShaderType::Forward) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::GBuffer) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::ShadowMap) == "");
	CHECK(CompilePixelVariant(compiler, PixelShaderType::UI) == "");
}

#endif
