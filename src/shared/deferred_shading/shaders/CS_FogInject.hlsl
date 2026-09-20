// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Fills every froxel with this frame's fog: extinction from height fog times wind-scrolled noise, and
// the light scattered toward the camera (fog ambient + shadowed sun). Output rgb = radiance * sigma,
// a = sigma.

#include "VolumetricFogCommon.hlsli"

static const uint NUM_SHADOW_CASCADES = 4;

Texture3D<float> NoiseVolume : register(t0);
Texture2D ShadowMapCascade0 : register(t5);
Texture2D ShadowMapCascade1 : register(t6);
Texture2D ShadowMapCascade2 : register(t7);
Texture2D ShadowMapCascade3 : register(t8);

SamplerState NoiseSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);

// MUST stay field-for-field in sync with struct ShadowBuffer in deferred_renderer.cpp. Only the leading
// fields are read here.
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

RWTexture3D<float4> InjectOutput : register(u0);

// One hardware-PCF tap in the cascade covering this distance. 1 = lit.
// viewDepth: distance along the camera's forward axis (not radial distance to the camera) - matches
// how CascadeSplitDistances is built (the lighting shader also selects cascades by view depth).
float SampleSunVisibility(float3 worldPos, float viewDepth)
{
    if (CascadeCount == 0)
    {
        return 1.0f;
    }

    uint cascade = CascadeCount - 1;
    for (uint i = 0; i < CascadeCount; ++i)
    {
        if (viewDepth <= CascadeSplitDistances[i])
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

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GridWidth || id.y >= GridHeight || id.z >= GridDepth)
    {
        return;
    }

    float2 uv = (float2(id.xy) + 0.5f) / float2(GridWidth, GridHeight);
    float3 ray = FogWorldRay(uv);
    float viewDepth = SliceToDepth((float(id.z) + Jitter) / float(GridDepth));
    float3 worldPos = FogWorldPosition(ray, viewDepth);

    // The pattern moves with the wind: the noise at p now is the noise that was at p - offset.
    float3 noiseUvw = float3(worldPos.x / NoiseSize - WindOffset.x, worldPos.y / NoiseSize, worldPos.z / NoiseSize - WindOffset.y);
    float noise = NoiseVolume.SampleLevel(NoiseSampler, noiseUvw, 0.0f);

    float sigma = FogDensityAt(worldPos.y) * NoiseDensityFactor(noise, NoiseAmount);
    float visibility = SampleSunVisibility(worldPos, viewDepth);
    float3 radiance = FogSource(dot(ray, SunDirection), visibility);

    InjectOutput[id] = float4(radiance * sigma, sigma);
}
