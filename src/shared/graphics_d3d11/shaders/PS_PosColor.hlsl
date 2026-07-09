// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#define WITH_COLOR 1
#include "VS_InOut.hlsli"

#include "Matrices.hlsli"

float4 main(VertexOut input) : SV_Target
{
	return input.color;
}
