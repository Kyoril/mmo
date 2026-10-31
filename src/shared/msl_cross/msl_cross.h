// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "metal_shader_blob.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mmo::msl_cross
{
	/// @brief Resolves an #include of HLSL source.
	/// @param includeName The name between the quotes or brackets.
	/// @param includerName The name of the source containing the #include.
	/// @returns The included source, or no value if it can not be found.
	using IncludeResolver = std::function<std::optional<std::string>(const std::string& includeName, const std::string& includerName)>;

	/// @brief Input of a cross compilation.
	struct CompileInput
	{
		/// HLSL source (shader model 5).
		std::string source;
		/// Name of the source, used in error messages and passed to the include resolver.
		std::string sourceName { "shader.hlsl" };
		/// Entry point function name in the HLSL source.
		std::string entryPoint { "main" };
		metal_shader_blob::Stage stage { metal_shader_blob::Stage::Vertex };
		/// Preprocessor definitions as "NAME" or "NAME=VALUE".
		std::vector<std::string> defines;
		/// Resolves #include directives. May be empty if the source has none.
		IncludeResolver includeResolver;
	};

	/// @brief Output of a cross compilation.
	struct CompileOutput
	{
		bool succeeded { false };
		/// Metal Shading Language source with the entry point "main0".
		std::string source;
		/// Threads per threadgroup of a compute shader.
		uint32 threadgroupSize[3] { 1, 1, 1 };
		/// Compiler messages; the reason of the failure if it did not succeed.
		std::string log;

		/// @brief Packs the result into a blob as stored in material files.
		[[nodiscard]] std::vector<uint8> ToBlob(const metal_shader_blob::Stage stage) const
		{
			return metal_shader_blob::Pack(stage, source, threadgroupSize[0], threadgroupSize[1], threadgroupSize[2]);
		}
	};

	/// @brief Cross compiles HLSL to Metal Shading Language via SPIR-V (glslang and SPIRV-Cross).
	/// @remarks Resource registers are mapped as described in metal_bindings. Vertex inputs are named
	///          after their semantic (e.g. TEXCOORD1) so the backend can match them to vertex
	///          declarations, and values passed between stages get a location derived from their
	///          semantic, so separately compiled vertex and pixel shaders always agree.
	/// @param input What to compile.
	/// @param output Receives the result.
	/// @returns Whether the compilation succeeded.
	bool CompileHlslToMsl(const CompileInput& input, CompileOutput& output);
}
