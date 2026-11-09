// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Shared declarations of the upscale passes (PS_Upscale, PS_FsrEasu, PS_FsrRcas). They all run on
// the finished, tonemapped frame, so every colour here is display-encoded, not linear.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SourceTexture : register(t0);
SamplerState LinearSampler : register(s0);

// Mirrors UpscaleConstants in upscale_pass.cpp.
cbuffer UpscaleBuffer : register(b2)
{
    float2 SourceSize;      // source texture size in pixels
    float2 InvSourceSize;   // 1 / SourceSize
    float2 OutputSize;      // output target size in pixels
    float2 SourcePerOutput; // SourceSize / OutputSize
    float Sharpness;        // RCAS: exp2(-stops), 1 = strongest
    float Mode;             // PS_Upscale: 0 = point, 1 = bicubic
    float2 _UpscalePadding;
};

// Reads one source texel, clamped to the image so the border taps repeat the edge.
float3 LoadSource(int2 p)
{
    const int2 maxP = int2(SourceSize) - 1;
    return SourceTexture.Load(int3(clamp(p, int2(0, 0), maxP), 0)).rgb;
}

// Luma times two, the cheap weighting FSR uses for its edge and noise detection.
float FsrLuma(float3 c)
{
    return c.b * 0.5f + (c.r * 0.5f + c.g);
}
