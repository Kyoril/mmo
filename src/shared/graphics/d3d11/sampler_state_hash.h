
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
			hash.Add64(desc.AddressU);
			hash.Add64(desc.AddressV);
			hash.Add64(desc.AddressW);
			hash.Add64(desc.Filter);
			hash.AddFloat(desc.MipLODBias);
			hash.Add64(desc.MaxAnisotropy);
			hash.Add64(desc.ComparisonFunc);
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
