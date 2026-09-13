// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Separable depth-aware blur of the atmosphere march target. Resolves the static jitter pattern
// without smearing shafts across depth discontinuities such as tree trunks against the sky.

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D MarchTexture : register(t0);
Texture2D NormalTexture : register(t1);

// MUST stay in sync with AtmosphereBlurConstants in atmosphere_pass.cpp.
cbuffer AtmosphereBlurBuffer : register(b2)
{
    float2 Direction;
    int2 LowResSize;
    uint Divisor;
    float3 _BlurPadding;
};

static const float BLUR_WEIGHTS[4] = { 0.2270270f, 0.1945946f, 0.1216216f, 0.0540541f };

float LowResDistance(int2 lowPixel)
{
    float depth = NormalTexture.Load(int3(lowPixel * int(Divisor), 0)).a;
    return depth <= 0.0f ? 100000.0f : depth;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    int2 center = int2(input.Position.xy);
    float centerDistance = LowResDistance(center);
    int2 stepDirection = int2(Direction);

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;

    [unroll]
    for (int k = -3; k <= 3; ++k)
    {
        int2 tapPixel = clamp(center + stepDirection * k, int2(0, 0), LowResSize - 1);
        float tapDistance = LowResDistance(tapPixel);
        float depthWeight = exp(-abs(tapDistance - centerDistance) / max(centerDistance * 0.05f, 0.25f));
        float weight = BLUR_WEIGHTS[abs(k)] * depthWeight;

        sum += MarchTexture.Load(int3(tapPixel, 0)) * weight;
        weightSum += weight;
    }

    return sum / max(weightSum, 1e-5f);
}
