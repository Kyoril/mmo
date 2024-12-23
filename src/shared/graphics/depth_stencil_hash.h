// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/dynamic_hash.h"

#include <d3d11.h>

#include "base/typedefs.h"


namespace mmo
{
	class DepthStencilHash final
	{
	public:
		size_t operator()(const D3D11_DEPTH_STENCIL_DESC& desc) const
		{			
			DynamicHash hash;
			// Modify values to get more uniqueness
			hash.Add32(desc.DepthWriteMask * 7);
			hash.Add32(desc.DepthEnable * 17);
			hash.Add32(desc.DepthFunc * 19);
			hash.Add32(desc.StencilEnable * 5);
			hash.Add32(desc.StencilReadMask * 9);
			hash.Add32(desc.StencilWriteMask * 3);
			hash.Add32(desc.BackFace.StencilDepthFailOp * 5);
			hash.Add32(desc.BackFace.StencilFailOp * 11);
			hash.Add32(desc.BackFace.StencilFunc * 2);
			hash.Add32(desc.BackFace.StencilPassOp * 7);
			hash.Add32(desc.FrontFace.StencilDepthFailOp * 5);
			hash.Add32(desc.FrontFace.StencilFailOp * 11);
			hash.Add32(desc.FrontFace.StencilFunc * 2);
			hash.Add32(desc.FrontFace.StencilPassOp * 7);
			return hash;
		}
	};
}
