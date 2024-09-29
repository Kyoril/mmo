// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/dynamic_hash.h"

#include <d3d11.h>


namespace mmo
{
	class RasterizerStateHash final
	{
	public:
		size_t operator()(const D3D11_RASTERIZER_DESC& desc) const 
		{
			DynamicHash hash;
			hash.Add64(desc.AntialiasedLineEnable);
			hash.Add64(desc.CullMode);
			hash.Add64(desc.DepthBias);
			hash.AddFloat(desc.DepthBiasClamp);
			hash.Add64(desc.DepthClipEnable);
			hash.Add64(desc.FillMode);
			hash.Add64(desc.FrontCounterClockwise);
			hash.Add64(desc.MultisampleEnable);
			hash.Add64(desc.ScissorEnable);
			hash.AddFloat(desc.SlopeScaledDepthBias);
			return hash;
		}
	};
}
