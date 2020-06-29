// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#define WITH_COLOR 1
#define WITH_TEX 1
#include "VS_InOut.hlsli"

cbuffer Matrices
{
	float4x4 matWorld;
	float4x4 matViewProj;
};

VertexOut main(VertexIn input)
{
	VertexOut output;

	output.pos = mul(float4(input.pos.xyz, 1.0), mul(matWorld, matViewProj));
	output.color = input.color;
	output.uv1 = input.uv1;

	return output;
}