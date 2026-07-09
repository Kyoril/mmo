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

// Note: this pass renders a pre-transformed fullscreen quad, so no matrix constants are needed.

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    output.Position = float4(input.Position.xy, 0.0f, 1.0f);
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;
    
    return output;
}
