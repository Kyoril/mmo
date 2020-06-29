// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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

	return output;
}