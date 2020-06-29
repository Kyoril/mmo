// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#define WITH_COLOR 1
#include "VS_InOut.hlsli"

cbuffer Matrices
{
	float4x4 matWorld;
	float4x4 matViewProj;
};

VertexOut main(VertexIn input)
{
	VertexOut output;

	output.pos = mul(float4(input.pos.xyz, 1.0), matWorld);
	output.pos = mul(output.pos, matViewProj);
	output.color= input.color;

	return output;
}