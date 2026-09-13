// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// 3x3 tent upsample of the coarser bloom level, added to the downsample of the current level.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D LowerTexture : register(t0);      // coarser level (upsample result or last downsample)
Texture2D CurrentTexture : register(t1);    // downsample at this level
SamplerState LinearSampler : register(s0);

// MUST stay in sync with BloomUpsampleConstants in bloom_pass.cpp.
cbuffer BloomUpsampleBuffer : register(b2)
{
    float2 LowerTexelSize;
    float Radius;
    float _UpsamplePadding;
};

float3 Tap(float2 uv)
{
    return LowerTexture.SampleLevel(LinearSampler, uv, 0).rgb;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 uv = input.TexCoord;
    float2 t = LowerTexelSize * Radius;

    float3 tent = Tap(uv + float2(-t.x, -t.y))
        + Tap(uv + float2(0.0f, -t.y)) * 2.0f
        + Tap(uv + float2(t.x, -t.y))
        + Tap(uv + float2(-t.x, 0.0f)) * 2.0f
        + Tap(uv) * 4.0f
        + Tap(uv + float2(t.x, 0.0f)) * 2.0f
        + Tap(uv + float2(-t.x, t.y))
        + Tap(uv + float2(0.0f, t.y)) * 2.0f
        + Tap(uv + float2(t.x, t.y));

    float3 current = CurrentTexture.SampleLevel(LinearSampler, uv, 0).rgb;
    return float4(current + tent / 16.0f, 1.0f);
}
