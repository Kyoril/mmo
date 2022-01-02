// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <memory>


namespace mmo
{
	/// Enumerates the possible shader types.
	enum class ShaderType
	{
		VertexShader,
		PixelShader,
		GeometryShader,
		ComputeShader,
		DomainShader,
		HullShader
	};


	/// Base class of a shader.
	class ShaderBase : public NonCopyable
	{
	public:
		/// Activates the vertex shader for the current graphics device pipeline.
		virtual void Set() = 0;
	};

	typedef std::unique_ptr<ShaderBase> ShaderPtr;
}
