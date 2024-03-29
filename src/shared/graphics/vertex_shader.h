// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
		[[nodiscard]] ShaderType GetType() const noexcept override { return ShaderType::VertexShader; }
	};
}
