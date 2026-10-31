// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shader_compiler_metal.h"

#include "metal_shader_blob.h"
#include "msl_cross.h"
#include "graphics/shader_platform.h"
#include "log/default_log_levels.h"

#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace mmo
{
	namespace
	{
		/// The generated code defines a scalar select() helper and sometimes passes vectors to it,
		/// which fxc truncates with a warning but glslang rejects. Declares the truncating overloads
		/// explicitly, so both compilers produce the same result.
		String AddSelectOverloads(const String& source)
		{
			static const String selectDefinition = "float select(bool expression, float whenTrue, float whenFalse) {";
			const size_t position = source.find(selectDefinition);
			if (position == String::npos)
			{
				return source;
			}

			static const char* types[] = { "float", "float2", "float3", "float4" };
			std::ostringstream overloads;
			for (const char* whenTrue : types)
			{
				for (const char* whenFalse : types)
				{
					if (whenTrue == types[0] && whenFalse == types[0])
					{
						continue;
					}

					overloads << "float select(bool expression, " << whenTrue << " whenTrue, " << whenFalse << " whenFalse) { return expression ? (float)whenTrue : (float)whenFalse; }\n";
				}
			}

			String result = source;
			result.insert(position, overloads.str());
			return result;
		}

		String Quote(const std::filesystem::path& path)
		{
			String quoted = "'";
			for (const char c : path.string())
			{
				if (c == '\'')
				{
					quoted += "'\\''";
				}
				else
				{
					quoted += c;
				}
			}
			return quoted + "'";
		}

		String ReadText(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		}

		/// Whether the Metal compiler of the Xcode toolchain can be run. Checked once.
		bool IsMetalToolchainAvailable()
		{
			static const bool available = std::system("xcrun -sdk macosx -f metal >/dev/null 2>&1") == 0
				&& std::system("xcrun -sdk macosx -f metallib >/dev/null 2>&1") == 0;
			return available;
		}

		/// Compiles Metal source to a library with xcrun metal and metallib.
		/// @return false with the tool output in outLog if a step failed.
		bool CompileLibrary(const String& source, std::vector<uint8>& outLibrary, String& outLog)
		{
			static std::atomic<uint32> s_counter { 0 };

			std::error_code error;
			const std::filesystem::path directory = std::filesystem::temp_directory_path(error)
				/ ("mmo_metal_" + std::to_string(::getpid()) + "_" + std::to_string(s_counter++));
			std::filesystem::create_directories(directory, error);
			if (error)
			{
				outLog = "Unable to create " + directory.string() + ": " + error.message();
				return false;
			}

			const auto sourcePath = directory / "shader.metal";
			const auto airPath = directory / "shader.air";
			const auto libraryPath = directory / "shader.metallib";
			const auto logPath = directory / "log.txt";
			{
				std::ofstream file(sourcePath, std::ios::binary);
				file << source;
			}

			const String compile = "xcrun -sdk macosx metal -std=macos-metal2.3 -mmacosx-version-min=11.0 -c "
				+ Quote(sourcePath) + " -o " + Quote(airPath) + " >" + Quote(logPath) + " 2>&1";
			const String link = "xcrun -sdk macosx metallib " + Quote(airPath) + " -o " + Quote(libraryPath)
				+ " >" + Quote(logPath) + " 2>&1";

			bool succeeded = std::system(compile.c_str()) == 0 && std::system(link.c_str()) == 0;
			if (succeeded)
			{
				std::ifstream library(libraryPath, std::ios::binary);
				outLibrary.assign(std::istreambuf_iterator<char>(library), std::istreambuf_iterator<char>());
				succeeded = !outLibrary.empty();
			}

			if (!succeeded)
			{
				outLog = ReadText(logPath);
			}

			std::filesystem::remove_all(directory, error);
			return succeeded;
		}
	}

	ShaderCompilerMetal::ShaderCompilerMetal(const Output output)
		: m_output(output)
	{
	}

	const String& ShaderCompilerMetal::GetShaderFormat() const
	{
		static const String format(shader_platform::Metal);
		return format;
	}

	std::string_view ShaderCompilerMetal::GetPlatform() const
	{
		return shader_platform::Metal;
	}

	void ShaderCompilerMetal::Compile(const ShaderCompileInput& input, ShaderCompileResult& output)
	{
		msl_cross::CompileInput crossInput;
		crossInput.source = AddSelectOverloads(input.shaderCode);
		crossInput.sourceName = "material.hlsl";
		switch (input.shaderType)
		{
		case ShaderType::PixelShader:
			crossInput.stage = metal_shader_blob::Stage::Pixel;
			break;
		case ShaderType::ComputeShader:
			crossInput.stage = metal_shader_blob::Stage::Compute;
			break;
		default:
			crossInput.stage = metal_shader_blob::Stage::Vertex;
			break;
		}

		output.code.format = GetShaderFormat();
		output.code.data.clear();

		msl_cross::CompileOutput crossOutput;
		if (!msl_cross::CompileHlslToMsl(crossInput, crossOutput))
		{
			output.succeeded = false;
			output.errorMessage = crossOutput.log;
			return;
		}

		if (m_output == Output::Library)
		{
			if (IsMetalToolchainAvailable())
			{
				std::vector<uint8> library;
				if (!CompileLibrary(crossOutput.source, library, output.errorMessage))
				{
					output.succeeded = false;
					return;
				}

				output.code.data = metal_shader_blob::PackLibrary(crossInput.stage, library,
					crossOutput.threadgroupSize[0], crossOutput.threadgroupSize[1], crossOutput.threadgroupSize[2]);
				output.succeeded = true;
				return;
			}

			static bool s_warned = false;
			if (!s_warned)
			{
				s_warned = true;
				WLOG("The Metal compiler (xcrun metal) is not available; shaders are stored as Metal source instead of libraries");
			}
		}

		output.code.data = crossOutput.ToBlob(crossInput.stage);
		output.succeeded = true;
	}
}
