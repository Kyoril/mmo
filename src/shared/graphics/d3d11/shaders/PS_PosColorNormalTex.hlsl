// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#define WITH_COLOR 1
#define WITH_NORMAL 1
#define WITH_TEX 1
#include "VS_InOut.hlsli"


float4 main(VertexIn input) : SV_Target
{
	return input.color;
}
