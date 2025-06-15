// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shader_base.h"


namespace mmo
{
	/// Base class of a vertex shader.
	class VertexShader : public ShaderBase
	{
	public:
		VertexShader() = default;
		virtual ~VertexShader() override = default;

	public:
		[[nodiscard]] ShaderType GetType() const override { return ShaderType::VertexShader; }
	};
}
