// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "VS_InOut.hlsli"

cbuffer Matrices
{
	column_major matrix matWorld;
	column_major matrix matView;
	column_major matrix matProj;
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

	return output;
}