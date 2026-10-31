#pragma once

#include <string_view>
#include <vector>

#include "shader_base.h"
#include "base/typedefs.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	class ShaderCode
	{
	public:
		/// @brief Format of the shader code.
		String format;
		/// @brief The raw shader code.
		std::vector<uint8> data;
	};

	io::Writer& operator<<(io::Writer& writer, const ShaderCode& code);
	io::Reader& operator>>(io::Reader& reader, ShaderCode& code);
	
	struct ShaderCompileResult
	{
		bool succeeded { false };
		ShaderCode code;
		String errorMessage;
	};

	struct ShaderCompileInput
	{
		String shaderCode;
		ShaderType shaderType;
	};
	
	class ShaderCompiler
	{
	public:
		virtual ~ShaderCompiler() = default;

	public:
		/// @brief Gets the shader format that this compiler supports.
		[[nodiscard]] virtual const String& GetShaderFormat() const = 0;

		/// @brief Gets the shader platform id (a shader_platform::* value) that the code this compiler
		///        produces is meant for. Compiled variants are stored in a material under this platform,
		///        regardless of the platform the process itself runs on.
		[[nodiscard]] virtual std::string_view GetPlatform() const = 0;

		/// @brief Compiles the given shader code input and generates an output result.
		/// @param input The input parameter.
		/// @param output The output parameter.
		virtual void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) = 0;
	};
}
