// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#define WITH_COLOR 1
#include "VS_InOut.hlsli"

float4 main(VertexIn input) : SV_Target
{
	return input.color;
}
