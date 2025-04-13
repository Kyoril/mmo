// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
	return float4(1.0f, 0.0f, 1.0f, 1.0f);
}