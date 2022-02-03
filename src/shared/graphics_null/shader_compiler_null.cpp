
#include "shader_compiler_null.h"

namespace mmo
{
	const String ShaderFormat_Null_SM = "NULL_SM";

	void ShaderCompilerNull::Compile(const ShaderCompileInput& input, ShaderCompileResult& output)
	{
		output.succeeded = true;
		output.code.format = GetShaderFormat();
		output.code.data.resize(1, 0);
	}
}
