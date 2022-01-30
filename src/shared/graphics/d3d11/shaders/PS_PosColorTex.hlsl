// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#define WITH_COLOR 1
#define WITH_TEX 1
#include "VS_InOut.hlsli"

Texture2D tex;
SamplerState texSampler;

float4 main(VertexIn input) : SV_Target
{
	return input.color * tex.Sample(texSampler, input.uv0);
}
