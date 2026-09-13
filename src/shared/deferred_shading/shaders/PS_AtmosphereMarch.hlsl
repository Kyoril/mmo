// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Reduced-resolution march of the height fog through the cascaded shadow maps. Output rgb is the
// light scattered toward the camera over the first MarchDistance metres of the view ray, a is the
// transmittance over that stretch. PS_AtmosphereComposite adds the closed-form remainder.

#include "AtmosphereCommon.hlsli"

static const uint NUM_SHADOW_CASCADES = 4;

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

// G-Buffer normal target: rgb = normal, a = linear radial depth (0 where nothing was drawn).
Texture2D NormalTexture : register(t1);

Texture2D ShadowMapCascade0 : register(t5);
Texture2D ShadowMapCascade1 : register(t6);
Texture2D ShadowMapCascade2 : register(t7);
Texture2D ShadowMapCascade3 : register(t8);

SamplerComparisonState ShadowSampler : register(s1);

// MUST stay field-for-field in sync with struct ShadowBuffer in deferred_renderer.cpp and the
// ShadowBuffer cbuffer in PS_DeferredLighting.hlsl. Only the leading fields are read here.
cbuffer ShadowBuffer : register(b3)
{
    column_major matrix CascadeViewProj[NUM_SHADOW_CASCADES];
    float4 CascadeSplitDistances;
    float ShadowBias;
    float NormalBiasScale;
    float ShadowSoftness;
    float BlockerSearchRadius;
    float LightSize;
    uint CascadeCount;
};

// MUST stay in sync with AtmosphereMarchConstants in atmosphere_pass.cpp.
cbuffer AtmosphereMarchBuffer : register(b2)
{
    float MarchDistance;
    float SkyDistance;
    uint StepCount;
    uint Divisor;
    float2 FullResolution;
    uint DebugMode;
    float _MarchPadding;
};

// One hardware-PCF tap in the cascade covering this radial distance. 1 = lit.
float SampleSunVisibility(float3 worldPos, float distanceFromCamera)
{
    if (CascadeCount == 0)
    {
        return 1.0f;
    }

    uint cascade = CascadeCount - 1;
    for (uint i = 0; i < CascadeCount; ++i)
    {
        if (distanceFromCamera <= CascadeSplitDistances[i])
        {
            cascade = i;
            break;
        }
    }

    float4 lightSpace = mul(float4(worldPos, 1.0f), CascadeViewProj[cascade]);
    if (lightSpace.w <= 0.0f)
    {
        return 1.0f;
    }

    float3 ndc = lightSpace.xyz / lightSpace.w;
    float2 uv = ndc.xy * float2(1.0f, -1.0f) * 0.5f + 0.5f;
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return 1.0f;
    }

    float compareDepth = ndc.z - ShadowBias;
    switch (cascade)
    {
        case 0: return ShadowMapCascade0.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
        case 1: return ShadowMapCascade1.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
        case 2: return ShadowMapCascade2.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
        default: return ShadowMapCascade3.SampleCmpLevelZero(ShadowSampler, uv, compareDepth);
    }
}

float4 main(PS_INPUT input) : SV_TARGET
{
    int2 lowPixel = int2(input.Position.xy);

    // The composite and the blur look up this texel's depth at the same full-resolution pixel,
    // which keeps the bilateral weights consistent with what was marched.
    int2 fullPixel = lowPixel * int(Divisor);
    float depth = NormalTexture.Load(int3(fullPixel, 0)).a;
    float sceneDistance = depth <= 0.0f ? SkyDistance : depth;
    float marchLength = min(sceneDistance, MarchDistance);

    if (marchLength <= 0.0f || StepCount == 0)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float2 uv = (float2(fullPixel) + 0.5f) / FullResolution;
    float3 rayDir = WorldViewRay(uv);
    float cosTheta = dot(rayDir, SunDirection);

    // Static per-pixel offset: without temporal accumulation a per-frame offset would shimmer.
    float jitter = InterleavedGradientNoise(float2(lowPixel));
    float stepCount = float(StepCount);

    float3 inscatter = float3(0.0f, 0.0f, 0.0f);
    float transmittance = 1.0f;
    float visibilitySum = 0.0f;

    [loop]
    for (uint i = 0; i < StepCount; ++i)
    {
        // Quadratic spacing puts most samples near the camera, where shafts are readable.
        float segmentStart = marchLength * pow(float(i) / stepCount, 2.0f);
        float segmentEnd = marchLength * pow(float(i + 1) / stepCount, 2.0f);
        float sampleDistance = marchLength * pow((float(i) + jitter) / stepCount, 2.0f);

        float visibility = SampleSunVisibility(CameraPosition + rayDir * sampleDistance, sampleDistance);
        visibilitySum += visibility;

        float segmentTau = FogOpticalDepth(CameraPosition.y + rayDir.y * segmentStart, rayDir.y, segmentEnd - segmentStart);
        float segmentTransmittance = exp(-segmentTau);

        inscatter += transmittance * FogSource(cosTheta, visibility) * (1.0f - segmentTransmittance);
        transmittance *= segmentTransmittance;
    }

    if (DebugMode == 3)
    {
        return float4((visibilitySum / stepCount).xxx, 1.0f);
    }

    return float4(inscatter, transmittance);
}
