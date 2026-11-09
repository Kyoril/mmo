// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// The simple upscale filters: point (nearest texel, crisp and blocky) and bicubic (Catmull-Rom,
// softer than point but noticeably sharper than bilinear). Bilinear itself needs no pass at all,
// the world frame's quad samples the source bilinearly.

#include "UpscaleCommon.hlsli"

// Catmull-Rom from nine bilinear taps instead of sixteen point taps: the inner two weights of each
// axis are folded into one bilinear fetch between them.
float3 SampleCatmullRom(float2 uv)
{
    const float2 samplePos = uv * SourceSize;
    const float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    const float2 f = samplePos - texPos1;

    const float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    const float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    const float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    const float2 w3 = f * f * (-0.5f + 0.5f * f);

    const float2 w12 = w1 + w2;
    const float2 offset12 = w2 / w12;

    const float2 tc0 = (texPos1 - 1.0f) * InvSourceSize;
    const float2 tc3 = (texPos1 + 2.0f) * InvSourceSize;
    const float2 tc12 = (texPos1 + offset12) * InvSourceSize;

    float3 result = 0.0f;
    result += SourceTexture.SampleLevel(LinearSampler, float2(tc0.x,  tc0.y), 0.0f).rgb * w0.x  * w0.y;
    result += SourceTexture.SampleLevel(LinearSampler, float2(tc12.x, tc0.y), 0.0f).rgb * w12.x * w0.y;
    result += SourceTexture.SampleLevel(LinearSampler, float2(tc3.x,  tc0.y), 0.0f).rgb * w3.x  * w0.y;

    result += SourceTexture.SampleLevel(LinearSampler, float2(tc0.x,  tc12.y), 0.0f).rgb * w0.x  * w12.y;
    result += SourceTexture.SampleLevel(LinearSampler, float2(tc12.x, tc12.y), 0.0f).rgb * w12.x * w12.y;
    result += SourceTexture.SampleLevel(LinearSampler, float2(tc3.x,  tc12.y), 0.0f).rgb * w3.x  * w12.y;

    result += SourceTexture.SampleLevel(LinearSampler, float2(tc0.x,  tc3.y), 0.0f).rgb * w0.x  * w3.y;
    result += SourceTexture.SampleLevel(LinearSampler, float2(tc12.x, tc3.y), 0.0f).rgb * w12.x * w3.y;
    result += SourceTexture.SampleLevel(LinearSampler, float2(tc3.x,  tc3.y), 0.0f).rgb * w3.x  * w3.y;

    // The negative lobes overshoot at hard edges; the frame is display-encoded, so clamp to it.
    return saturate(result);
}

float4 main(PS_INPUT input) : SV_TARGET
{
    if (Mode < 0.5f)
    {
        // Nearest source texel of this output pixel's centre.
        const int2 p = int2(floor(input.Position.xy * SourcePerOutput));
        return float4(LoadSource(p), 1.0f);
    }

    const float2 uv = input.Position.xy / OutputSize;
    return float4(SampleCatmullRom(uv), 1.0f);
}
