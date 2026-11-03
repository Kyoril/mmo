// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graphics/material.h"
#include "graphics/material_compiler_hlsl.h"
#include "graphics/shader_compiler.h"
#include "graphics/shader_platform.h"

using namespace mmo;

namespace
{
	/// @brief Shader compiler for a platform that is not the one the tests run on. "Compiles" by
	///        returning the input text as bytes and can be told to fail one vertex variant.
	class FakeShaderCompiler final : public ShaderCompiler
	{
	public:
		/// @brief Index (in compile order) of the vertex shader to fail, or -1 for none.
		int failVertexVariant = -1;

		[[nodiscard]] const String& GetShaderFormat() const override
		{
			static const String format("TEST_FORMAT");
			return format;
		}

		[[nodiscard]] std::string_view GetPlatform() const override
		{
			return "TEST";
		}

		void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) override
		{
			if (input.shaderType == ShaderType::VertexShader && m_vertexCompiles++ == failVertexVariant)
			{
				output.succeeded = false;
				output.errorMessage = "forced failure";
				return;
			}

			output.succeeded = true;
			output.code.format = GetShaderFormat();
			output.code.data.assign(input.shaderCode.begin(), input.shaderCode.end());
		}

	private:
		int m_vertexCompiles = 0;
	};
}

TEST_CASE("material compiler stores shaders under the shader compiler's platform", "[graphics]")
{
	MaterialCompilerHlsl compiler;
	Material material("PlatformTest");
	FakeShaderCompiler shaderCompiler;
	compiler.Compile(material, shaderCompiler);

	REQUIRE(!material.GetVertexShaderCode("TEST", VertexShaderType::Default).empty());
	REQUIRE(!material.GetPixelShaderCode("TEST", PixelShaderType::Forward).empty());
	REQUIRE(material.GetVertexShaderCode(shader_platform::GetCurrentPlatform(), VertexShaderType::Default).empty());
	REQUIRE(material.GetPixelShaderCode(shader_platform::GetCurrentPlatform(), PixelShaderType::Forward).empty());
}

TEST_CASE("a failed shader variant is stored as empty", "[graphics]")
{
	MaterialCompilerHlsl compiler;
	Material material("PlatformTest");
	FakeShaderCompiler shaderCompiler;

	// Vertex variants compile in enum order, so this fails the UI variant.
	shaderCompiler.failVertexVariant = static_cast<int>(VertexShaderType::UI);
	compiler.Compile(material, shaderCompiler);

	REQUIRE(material.GetVertexShaderCode("TEST", VertexShaderType::UI).empty());
	REQUIRE(!material.GetVertexShaderCode("TEST", VertexShaderType::Default).empty());
}
