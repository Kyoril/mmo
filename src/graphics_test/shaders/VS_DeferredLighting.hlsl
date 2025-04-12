// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Input structure
struct VS_INPUT
{
    float3 Position : POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// Output structure
struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// Constant buffer for matrices
cbuffer MatrixBuffer : register(b0)
{
    column_major matrix World;
    column_major matrix View;
    column_major matrix Projection;
    column_major matrix InverseView;
    column_major matrix InverseProjection;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    output.Position = float4(input.Position.xy, 0.0f, 1.0f);
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;
    
    return output;
}
