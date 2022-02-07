// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#define WITH_COLOR 1
#define WITH_NORMAL 1
#define WITH_BINORMAL 1
#define WITH_TANGENT 1
#define WITH_TEX 1
#include "VS_InOut.hlsli"

cbuffer Matrices
{
	column_major matrix matWorld;
	column_major matrix matView;
	column_major matrix matProj;
	column_major matrix matInvView;
};

VertexOut main(VertexIn input)
{
	VertexOut output;

	// Change the position vector to be 4 units for proper matrix calculations.
	input.pos.w = 1.0f;

	// Calculate the position of the vertex against the world, view, and projection matrices.
	output.pos = mul(input.pos, matWorld);
	output.pos = mul(output.pos, matView);
	output.pos = mul(output.pos, matProj);

	output.color = input.color;
	output.uv0 = input.uv0;

	output.binormal = normalize(mul(input.binormal, (float3x3)matWorld));
	output.tangent = normalize(mul(input.tangent, (float3x3)matWorld));

	 // Calculate the normal vector against the world matrix only.
    output.normal = mul(input.normal, (float3x3)matWorld);
    output.normal = normalize(output.normal);

	return output;
}