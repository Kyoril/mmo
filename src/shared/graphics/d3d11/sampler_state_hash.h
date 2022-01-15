// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/dynamic_hash.h"

#include <d3d11.h>

#include "base/typedefs.h"


namespace mmo
{
	class SamplerStateHash final
	{
	public:
		size_t operator()(const D3D11_SAMPLER_DESC& desc) const
		{			
			DynamicHash hash;
			// Modify values to get more uniqueness
			hash.Add32(desc.AddressU * 7);
			hash.Add32(desc.AddressV * 17);
			hash.Add32(desc.AddressW * 19);
			hash.Add32(desc.Filter);
			hash.AddFloat(desc.MipLODBias);
			hash.Add32(desc.MaxAnisotropy);
			hash.Add32(desc.ComparisonFunc);
			hash.AddFloat(desc.BorderColor[0]);
			hash.AddFloat(desc.BorderColor[1]);
			hash.AddFloat(desc.BorderColor[2]);
			hash.AddFloat(desc.BorderColor[3]);
			hash.AddFloat(desc.MinLOD);
			hash.AddFloat(desc.MaxLOD);
			return hash;
		}
	};
}
