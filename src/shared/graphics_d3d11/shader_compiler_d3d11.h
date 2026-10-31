#pragma once

#include "graphics/shader_compiler.h"
#include "graphics/shader_platform.h"


namespace mmo
{
	extern const String ShaderFormat_Null_SM;

	class ShaderCompilerD3D11 final : public ShaderCompiler
	{
	public:
		/// @copydoc ShaderCompiler::GetShaderFormat
		[[nodiscard]] const String& GetShaderFormat() const override { return ShaderFormat_Null_SM; }

		/// @copydoc ShaderCompiler::GetPlatform
		[[nodiscard]] std::string_view GetPlatform() const override { return shader_platform::D3D11; }
		
		/// @copydoc ShaderCompiler::Compile
		void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) override;
	};
}
