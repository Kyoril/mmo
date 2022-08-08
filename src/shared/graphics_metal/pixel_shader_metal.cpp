// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "pixel_shader_metal.h"

#include <iterator>


namespace mmo
{
    PixelShaderMetal::PixelShaderMetal(GraphicsDeviceMetal & InDevice, const void * InShaderCode, size_t InShaderCodeSize)
		: PixelShader()
		, Device(InDevice)
	{
		std::copy(
			static_cast<const uint8*>(InShaderCode), 
			static_cast<const uint8*>(InShaderCode) + InShaderCodeSize, 
			std::back_inserter(m_byteCode));
	}

	void PixelShaderMetal::Set()
	{

	}
}
