#pragma once

#include "graphics/shader_compiler.h"

namespace mmo
{
	extern const String ShaderFormat_Metal_SM;

	class ShaderCompilerMetal final : public ShaderCompiler
	{
	public:
		/// @copydoc ShaderCompiler::GetShaderFormat
		[[nodiscard]] const String& GetShaderFormat() const noexcept override { return ShaderFormat_Metal_SM; }
		
		/// @copydoc ShaderCompiler::Compile
		void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) override;
	};
}
