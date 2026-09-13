// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// 13-tap downsample (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare").
// The first level Karis-averages each 2x2 group so single bright texels cannot flicker, and applies
// the soft brightness threshold.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SourceTexture : register(t0);
SamplerState LinearSampler : register(s0);

// MUST stay in sync with BloomDownsampleConstants in bloom_pass.cpp.
cbuffer BloomDownsampleBuffer : register(b2)
{
    float2 SourceTexelSize;     // tap spacing in source UV units
    uint ApplyPrefilter;        // 1 on the first level only
    float Threshold;
    float Knee;
    float3 _DownsamplePadding;
};

float3 Tap(float2 uv)
{
    return SourceTexture.SampleLevel(LinearSampler, uv, 0).rgb;
}

float KarisWeight(float3 color)
{
    return 1.0f / (1.0f + dot(color, float3(0.2126f, 0.7152f, 0.0722f)));
}

float3 KarisAverage(float3 a, float3 b, float3 c, float3 d)
{
    float wa = KarisWeight(a);
    float wb = KarisWeight(b);
    float wc = KarisWeight(c);
    float wd = KarisWeight(d);
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

float3 SoftThreshold(float3 color)
{
    float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - Threshold + Knee, 0.0f, 2.0f * Knee);
    soft = soft * soft / (4.0f * Knee + 1e-5f);
    float contribution = max(soft, brightness - Threshold) / max(brightness, 1e-5f);
    return color * contribution;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 uv = input.TexCoord;
    float2 t = SourceTexelSize;

    float3 a = Tap(uv + t * float2(-2.0f, -2.0f));
    float3 b = Tap(uv + t * float2( 0.0f, -2.0f));
    float3 c = Tap(uv + t * float2( 2.0f, -2.0f));
    float3 d = Tap(uv + t * float2(-1.0f, -1.0f));
    float3 e = Tap(uv + t * float2( 1.0f, -1.0f));
    float3 f = Tap(uv + t * float2(-2.0f,  0.0f));
    float3 g = Tap(uv);
    float3 h = Tap(uv + t * float2( 2.0f,  0.0f));
    float3 i = Tap(uv + t * float2(-1.0f,  1.0f));
    float3 j = Tap(uv + t * float2( 1.0f,  1.0f));
    float3 k = Tap(uv + t * float2(-2.0f,  2.0f));
    float3 l = Tap(uv + t * float2( 0.0f,  2.0f));
    float3 m = Tap(uv + t * float2( 2.0f,  2.0f));

    float3 result;
    if (ApplyPrefilter != 0)
    {
        result = KarisAverage(d, e, i, j) * 0.5f
            + KarisAverage(a, b, f, g) * 0.125f
            + KarisAverage(b, c, g, h) * 0.125f
            + KarisAverage(f, g, k, l) * 0.125f
            + KarisAverage(g, h, l, m) * 0.125f;
        result = SoftThreshold(result);
    }
    else
    {
        result = (d + e + i + j) * 0.125f
            + (a + b + f + g) * 0.03125f
            + (b + c + g + h) * 0.03125f
            + (f + g + k + l) * 0.03125f
            + (g + h + l + m) * 0.03125f;
    }

    return float4(max(result, 0.0f), 1.0f);
}
