
#include "shader_compiler_metal.h"

namespace mmo
{
	const String ShaderFormat_Metal_SM = "METAL_SM";

	void ShaderCompilerMetal::Compile(const ShaderCompileInput& input, ShaderCompileResult& output)
	{
		output.succeeded = true;
		output.code.format = GetShaderFormat();
		output.code.data.resize(1, 0);
	}
}
