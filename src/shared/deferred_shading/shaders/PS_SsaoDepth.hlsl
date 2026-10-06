// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Copies the radial depth out of the G-Buffer normal target into a single-channel R32F target at
// the SSAO resolution. The AO march takes dozens of scattered depth samples per pixel; reading them
// from a compact, (usually) half-resolution target instead of the full-resolution RGBA16F normal
// target keeps far more of them in the texture cache, which is what the march is bound by.
//
// Each output texel point-samples the normal target at its own UV, exactly what PS_Ssao used to
// read for that pixel, so the AO centre depth is unchanged.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D NormalTexture : register(t1);
SamplerState PointSampler : register(s0);

float main(PS_INPUT input) : SV_TARGET
{
    return NormalTexture.SampleLevel(PointSampler, input.TexCoord, 0).a;
}
