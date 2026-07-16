// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Depth-aware separable bilateral blur for the contact shadow term. Run twice: horizontally, then
// vertically. Depth weighting stops the term bleeding across silhouette edges.
//
// Load-bearing rather than cosmetic, for the same reason as PS_SsaoBlur: the march keys its jitter
// on pixel position only (there is no TAA to resolve a temporal pattern), and its hit test is
// binary, so neighbouring pixels disagree with nothing to reconcile them. This pass is what turns
// that disagreement back into a smooth term.
//
// The kernel is deliberately MUCH tighter than the SSAO blur's (radius 2 vs 4). Contact shadows ARE
// the fine detail - a wide kernel would smear away the few centimetres of contact the effect exists
// to produce, which is self-defeating in a way it is not for a broad ambient term.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// The raw (or horizontally-blurred) contact shadow term, single channel.
Texture2D ContactShadowTexture : register(t0);

// G-Buffer normal target - sampled for its alpha (radial depth) to weight the blur.
Texture2D NormalTexture : register(t1);

SamplerState PointSampler : register(s0);

cbuffer ContactShadowBlurBuffer : register(b3)
{
    // Blur direction: (1,0) for the horizontal pass, (0,1) for the vertical pass.
    float2 BlurDirection;
    float2 InvScreenSize;
};

static const int BLUR_RADIUS = 2;

float4 main(PS_INPUT input) : SV_TARGET
{
    float centerDepth = NormalTexture.SampleLevel(PointSampler, input.TexCoord, 0).a;

    // Sky carries no contact shadow.
    if (centerDepth <= 0.0f)
    {
        return 1.0f;
    }

    float2 texelStep = BlurDirection * InvScreenSize;

    float totalTerm = 0.0f;
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

        float sampleTerm = ContactShadowTexture.SampleLevel(PointSampler, sampleUv, 0).r;

        // Gaussian spatial falloff.
        float spatialWeight = exp(-float(i * i) / (2.0f * float(BLUR_RADIUS) * float(BLUR_RADIUS) * 0.25f));

        // Depth weight: reject samples across a depth discontinuity so the term does not bleed over
        // silhouettes. Tolerance scales with depth because distant geometry has coarser depth
        // precision per pixel. 1 unit = 1 metre.
        //
        // Tighter than the SSAO blur's 0.05: this term lives at contact scale, where a 5cm tolerance
        // would happily average a foot together with the ground it stands on - which is the exact
        // boundary the effect is drawing.
        float depthDelta = abs(sampleDepth - centerDepth);
        float depthTolerance = 0.02f * centerDepth;

        // Fade the weight out between half and full tolerance rather than stepping it. A binary
        // cutoff makes taps flip in and out of the sum discretely as the kernel slides across a
        // curved or grazing surface, which bands the very term this pass exists to smooth. Beyond
        // the tolerance the weight still reaches exactly zero, so the term is rejected completely
        // across a silhouette -- that hard rejection is what stops bleeding and must be preserved.
        float depthWeight = 1.0f - smoothstep(depthTolerance * 0.5f, depthTolerance, depthDelta);

        float weight = spatialWeight * depthWeight;

        totalTerm += sampleTerm * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0f)
    {
        return ContactShadowTexture.SampleLevel(PointSampler, input.TexCoord, 0).r;
    }

    return totalTerm / totalWeight;
}
