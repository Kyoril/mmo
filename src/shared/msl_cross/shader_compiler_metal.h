// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/shader_compiler.h"

namespace mmo
{
	/// @brief Compiles the HLSL the material compiler generates to Metal shader blobs: cross compiles it
	///        to Metal Shading Language (msl_cross) and, for Output::Library, compiles that with the
	///        Xcode toolchain to a Metal library targeting arm64, macOS 11.0 and MSL 2.3.
	class ShaderCompilerMetal final : public ShaderCompiler
	{
	public:
		/// @brief What the produced blobs carry.
		enum class Output
		{
			/// Metal Shading Language source, compiled by the device at load time.
			Source,

			/// A precompiled Metal library. Falls back to source (with a warning) when the Metal
			/// compiler of the Xcode toolchain is not available.
			Library
		};

	public:
		/// @brief Creates the compiler.
		/// @param output What the produced blobs carry.
		explicit ShaderCompilerMetal(Output output);

	public:
		[[nodiscard]] const String& GetShaderFormat() const override;

		[[nodiscard]] std::string_view GetPlatform() const override;

		void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) override;

	private:
		Output m_output;
	};
}
