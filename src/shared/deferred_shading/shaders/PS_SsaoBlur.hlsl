// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// Raw AO term produced by PS_Ssao.hlsl, single channel.
Texture2D AoTexture : register(t0);
SamplerState PointSampler : register(s0);

cbuffer SsaoBuffer : register(b2)
{
    float2 ScreenSize;
    float2 InvScreenSize;
    float Radius;
    float Intensity;
    float Thickness;
    uint SliceCount;
    uint StepCount;
    uint DebugMode;
    float2 SsaoPadding;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    // STUB: pass-through. Task 5 replaces this body with a real bilateral blur; the input/output
    // contract (sample AoTexture, return the AO value) stays the same.
    return AoTexture.Sample(PointSampler, input.TexCoord).r;
}
