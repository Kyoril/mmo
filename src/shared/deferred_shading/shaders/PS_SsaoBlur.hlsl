// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Depth-aware separable bilateral blur for the SSAO term. Run twice: horizontally, then
// vertically. Depth weighting stops AO bleeding across silhouette edges.
//
// This blur is load-bearing rather than cosmetic: the AO pass keys its noise on pixel position
// only (no TAA exists to resolve a temporal pattern), so this pass is what turns that fixed
// pattern back into a smooth term.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// The raw (or horizontally-blurred) AO term, single channel.
Texture2D AoTexture : register(t0);

// G-Buffer normal target — sampled for its alpha (radial depth) to weight the blur.
Texture2D NormalTexture : register(t1);

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

// Blur direction: (1,0) for the horizontal pass, (0,1) for the vertical pass.
cbuffer SsaoBlurBuffer : register(b3)
{
    float2 BlurDirection;
    float2 BlurPadding;
};

static const int BLUR_RADIUS = 4;

float4 main(PS_INPUT input) : SV_TARGET
{
    float centerDepth = NormalTexture.SampleLevel(PointSampler, input.TexCoord, 0).a;

    // Sky pixels carry no AO.
    if (centerDepth <= 0.0f)
    {
        return 1.0f;
    }

    float2 texelStep = BlurDirection * InvScreenSize;

    float totalAo = 0.0f;
    float totalWeight = 0.0f;

    [unroll]
    for (int i = -BLUR_RADIUS; i <= BLUR_RADIUS; ++i)
    {
        float2 sampleUv = input.TexCoord + texelStep * float(i);

        float sampleDepth = NormalTexture.SampleLevel(PointSampler, sampleUv, 0).a;

        if (sampleDepth <= 0.0f)
        {
            continue;
        }

        float sampleAo = AoTexture.SampleLevel(PointSampler, sampleUv, 0).r;

        // Gaussian spatial falloff.
        float spatialWeight = exp(-float(i * i) / (2.0f * float(BLUR_RADIUS) * float(BLUR_RADIUS) * 0.25f));

        // Depth weight: reject samples across a depth discontinuity so AO does not bleed over
        // silhouettes. Tolerance scales with depth because distant geometry has coarser
        // depth precision per pixel. 0.05 = 5cm at 1m, and 1 unit = 1 metre.
        float depthDelta = abs(sampleDepth - centerDepth);
        float depthTolerance = 0.05f * centerDepth;

        // Fade the weight out between half and full tolerance rather than stepping it. A binary
        // cutoff makes taps flip in and out of the sum discretely as the kernel slides across a
        // curved or grazing surface, which bands the very term this pass exists to smooth. Beyond
        // the tolerance the weight still reaches exactly zero, so AO is rejected completely across
        // a silhouette -- that hard rejection is what stops bleeding and must be preserved.
        float depthWeight = 1.0f - smoothstep(depthTolerance * 0.5f, depthTolerance, depthDelta);

        float weight = spatialWeight * depthWeight;

        totalAo += sampleAo * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0f)
    {
        return AoTexture.SampleLevel(PointSampler, input.TexCoord, 0).r;
    }

    return totalAo / totalWeight;
}
