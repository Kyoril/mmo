// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "shader_base.h"


namespace mmo
{
	/// Base class of a pixel shader.
	class PixelShader : public ShaderBase
	{
	public:
		PixelShader() = default;
		virtual ~PixelShader() override = default;

	public:
		[[nodiscard]] ShaderType GetType() const noexcept override { return ShaderType::PixelShader; }
	};
}
