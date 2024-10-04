// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "vertex_shader_null.h"
#include "base/macros.h"


namespace mmo
{
	VertexShaderNull::VertexShaderNull(GraphicsDeviceNull & InDevice, const void * InShaderCode, size_t InShaderCodeSize)
		: VertexShader()
		, Device(InDevice)
	{
		std::copy(
			static_cast<const uint8*>(InShaderCode), 
			static_cast<const uint8*>(InShaderCode) + InShaderCodeSize, 
			std::back_inserter(m_byteCode));
	}

	void VertexShaderNull::Set()
	{

	}
}
