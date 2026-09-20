// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shader_base.h"

namespace mmo
{
	/// @brief Base class of a compute shader. Set() makes it the active compute program.
	class ComputeShader : public ShaderBase
	{
	public:
		ComputeShader() = default;
		~ComputeShader() override = default;

	public:
		/// @brief Always ShaderType::ComputeShader.
		[[nodiscard]] ShaderType GetType() const override { return ShaderType::ComputeShader; }
	};
}
