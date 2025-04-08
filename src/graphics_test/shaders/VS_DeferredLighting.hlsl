// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Input structure
struct VS_INPUT
{
    float3 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// Output structure
struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
    float3 ViewRay : TEXCOORD1;
};

// Constant buffer for matrices
cbuffer MatrixBuffer : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    matrix InverseView;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    
    // Transform position to clip space
    output.Position = float4(input.Position, 1.0f);
    
    // Pass through color and texture coordinates
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;
    
    // Calculate view ray for reconstructing position from depth
    float4 viewRay = mul(InverseView, float4(input.Position.x, input.Position.y, 1.0f, 0.0f));
    output.ViewRay = viewRay.xyz;
    
    return output;
}
