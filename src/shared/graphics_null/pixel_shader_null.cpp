// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pixel_shader_null.h"

#include <iterator>


namespace mmo
{
	PixelShaderNull::PixelShaderNull(GraphicsDeviceNull & InDevice, const void * InShaderCode, size_t InShaderCodeSize)
		: PixelShader()
		, Device(InDevice)
	{
		std::copy(
			static_cast<const uint8*>(InShaderCode), 
			static_cast<const uint8*>(InShaderCode) + InShaderCodeSize, 
			std::back_inserter(m_byteCode));
	}

	void PixelShaderNull::Set()
	{

	}
}
