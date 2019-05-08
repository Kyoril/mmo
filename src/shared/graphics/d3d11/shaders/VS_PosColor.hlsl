// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#define WITH_COLOR 1
#include "VS_InOut.hlsli"

cbuffer Matrices
{
	float4x4 matWorld;
	float4x4 matView;
	float4x4 matProj;
};

VertexOut main(VertexIn input)
{
	VertexOut output;

	float4x4 worldViewProj = mul(matWorld, mul(matView, matProj));
	output.pos = mul(float4(input.pos.xyz, 1.0), worldViewProj);
	output.color= input.color;

	return output;
}