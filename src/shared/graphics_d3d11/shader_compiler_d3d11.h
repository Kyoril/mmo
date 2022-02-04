#pragma once

#include "graphics/shader_compiler.h"


namespace mmo
{
	extern const String ShaderFormat_Null_SM;

	class ShaderCompilerD3D11 final : public ShaderCompiler
	{
	public:
		/// @copydoc ShaderCompiler::GetShaderFormat
		[[nodiscard]] const String& GetShaderFormat() const noexcept override { return ShaderFormat_Null_SM; }
		
		/// @copydoc ShaderCompiler::Compile
		void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) override;
	};
}
