// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#define WITH_COLOR 1
#define WITH_TEX 1
#include "VS_InOut.hlsli"

#include "Matrices.hlsli"

Texture2D tex;
SamplerState texSampler;

float4 main(VertexOut input) : SV_Target
{
	return input.color * tex.Sample(texSampler, input.uv0);
}
