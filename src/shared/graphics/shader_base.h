// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <memory>
#include <vector>
#include <span>


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
		ShaderBase() = default;
		virtual ~ShaderBase() override = default;

	public:
		/// Activates the vertex shader for the current graphics device pipeline.
		virtual void Set() = 0;

		[[nodiscard]] virtual ShaderType GetType() const noexcept = 0;

		[[nodiscard]] std::span<uint8> GetByteCode() { return m_byteCode; }

	protected:
		std::vector<uint8> m_byteCode;
	};

	typedef std::unique_ptr<ShaderBase> ShaderPtr;
}
