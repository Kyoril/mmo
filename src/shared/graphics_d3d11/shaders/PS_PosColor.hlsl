// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#define WITH_COLOR 1
#include "VS_InOut.hlsli"

cbuffer Matrices : register(b0)
{
    column_major matrix matWorld;
    column_major matrix matView;
    column_major matrix matProj;
    column_major matrix matInvView;
    column_major matrix matInvProj;
};

float4 main(VertexOut input) : SV_Target
{
	return input.color;
}
