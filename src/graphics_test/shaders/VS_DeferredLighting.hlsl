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
    float3 ViewRay : TEXCOORD1;
};

// Constant buffer for matrices
cbuffer MatrixBuffer : register(b0)
{
    column_major matrix World;
    column_major matrix View;
    column_major matrix Projection;
    row_major matrix InverseView;
    row_major matrix InverseProjection;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    output.Position = float4(input.Position.xy, 0.0f, 1.0f);
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;

    // Convert from UV [0,1] to NDC [-1,1]
    float2 ndc = input.TexCoord * 2.0f - 1.0f;

	// Reconstruct from NDC (Z=1 at far plane)
    float4 viewRayH = mul(InverseProjection, float4(ndc, 1, 1));
    float3 viewRay = viewRayH.xyz / viewRayH.w;

	// Don't normalize — let the pixel shader scale it by depth
    output.ViewRay = viewRay;
    
    return output;
}
