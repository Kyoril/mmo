#pragma once

#include "graphics/shader_compiler.h"

namespace mmo
{
	extern const String ShaderFormat_D3D_SM5;

	class ShaderCompilerD3D11 final : public ShaderCompiler
	{
	public:
		/// @copydoc ShaderCompiler::GetShaderFormat
		[[nodiscard]] const String& GetShaderFormat() const noexcept override { return ShaderFormat_D3D_SM5; }
		
		/// @copydoc ShaderCompiler::Compile
		void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) override;
	};
}
