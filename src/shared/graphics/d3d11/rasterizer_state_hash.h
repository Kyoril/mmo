
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
			hash.Add(desc.AntialiasedLineEnable);
			hash.Add(desc.CullMode);
			hash.Add(desc.DepthBias);
			hash.Add(desc.DepthBiasClamp);
			hash.Add(desc.DepthClipEnable);
			hash.Add(desc.FillMode);
			hash.Add(desc.FrontCounterClockwise);
			hash.Add(desc.MultisampleEnable);
			hash.Add(desc.ScissorEnable);
			hash.Add(desc.SlopeScaledDepthBias);
			return hash;
		}
	};
}
