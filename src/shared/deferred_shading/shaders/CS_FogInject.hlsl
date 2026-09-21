// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Fills every froxel with this frame's fog: extinction from height fog times wind-scrolled noise, and
// the light scattered toward the camera (fog ambient + shadowed sun + unshadowed point and spot
// lights). Output rgb = radiance * sigma, a = sigma.
//
// Point and spot lights come from the deferred renderer's light buffer (t9). Each 8x8x8 thread group
// culls the frame's lights once against its block's bounding sphere into a group-shared list, so a
// froxel only evaluates the lights that can reach its block.

#include "VolumetricFogCommon.hlsli"
#include "LightCommon.hlsli"

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

StructuredBuffer<Light> Lights : register(t9);

static const uint FOG_GROUP_SIZE = 8;
static const uint FOG_GROUP_THREADS = FOG_GROUP_SIZE * FOG_GROUP_SIZE * FOG_GROUP_SIZE;
static const uint MAX_BLOCK_LIGHTS = 64; // mirrors light_math::MaxLightsPerFogBlock

groupshared uint s_blockLightHits;                 // lights that reach the block (may exceed the capacity)
groupshared uint s_blockLights[MAX_BLOCK_LIGHTS];  // indices into Lights

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

// World-space bounding sphere (xyz center, w radius) of one 8x8x8 froxel block: the sphere around the
// block's eight corners between its first and last depth-slice boundary. Every jittered sample of the
// block lies inside the convex hull of these corners.
float4 FroxelBlockSphere(uint3 groupId)
{
    uint3 first = groupId * FOG_GROUP_SIZE;
    uint3 last = min(first + FOG_GROUP_SIZE, uint3(GridWidth, GridHeight, GridDepth));

    float2 uvMin = float2(first.xy) / float2(GridWidth, GridHeight);
    float2 uvMax = float2(last.xy) / float2(GridWidth, GridHeight);
    float nearDepth = SliceToDepth(float(first.z) / float(GridDepth));
    float farDepth = SliceToDepth(float(last.z) / float(GridDepth));

    float3 corners[8];
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        float2 uv = float2((i & 1) ? uvMax.x : uvMin.x, (i & 2) ? uvMax.y : uvMin.y);
        float depth = (i & 4) ? farDepth : nearDepth;
        corners[i] = FogWorldPosition(FogWorldRay(uv), depth);
    }

    float3 center = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (uint j = 0; j < 8; ++j)
    {
        center += corners[j];
    }
    center *= 0.125f;

    float radiusSquared = 0.0f;
    [unroll]
    for (uint k = 0; k < 8; ++k)
    {
        float3 offset = corners[k] - center;
        radiusSquared = max(radiusSquared, dot(offset, offset));
    }

    return float4(center, sqrt(radiusSquared));
}

// Radiance scattered toward the camera by the block's lights. ray: camera-to-froxel direction.
float3 BlockLightScattering(float3 worldPos, float3 ray, uint blockLightCount)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < blockLightCount; ++i)
    {
        Light light = Lights[s_blockLights[i]];
        float3 toFroxel = worldPos - light.Position;
        float distance = length(toFroxel);
        float attenuation = LightAttenuation(distance, light.Range);
        if (attenuation <= 0.0f)
        {
            continue;
        }

        float3 lightToFroxel = toFroxel / max(distance, 1e-4f);
        if (light.Type == LIGHT_TYPE_SPOT)
        {
            attenuation *= SpotFactor(light, lightToFroxel);
        }

        // Light travelling along lightToFroxel scatters toward the camera (-ray).
        sum += light.Color * (light.Intensity * attenuation * light.FogScattering) * ScatterPhase(dot(ray, -lightToFroxel));
    }

    return sum * LightScatterStrength;
}

[numthreads(FOG_GROUP_SIZE, FOG_GROUP_SIZE, FOG_GROUP_SIZE)]
void main(uint3 id : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    // 1. Cull the frame's lights against this block once, in parallel. Every thread of the group must
    //    reach both barriers, so the grid bounds check comes after them.
    if (groupIndex == 0)
    {
        s_blockLightHits = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    float4 blockSphere = FroxelBlockSphere(groupId);
    for (uint lightIndex = groupIndex; lightIndex < LightCount; lightIndex += FOG_GROUP_THREADS)
    {
        if (LightReachesFogSphere(Lights[lightIndex], blockSphere))
        {
            uint slot;
            InterlockedAdd(s_blockLightHits, 1, slot);
            if (slot < MAX_BLOCK_LIGHTS)
            {
                s_blockLights[slot] = lightIndex;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (id.x >= GridWidth || id.y >= GridHeight || id.z >= GridDepth)
    {
        return;
    }

    uint blockLightCount = min(s_blockLightHits, MAX_BLOCK_LIGHTS);

    if (DebugMode == 4)
    {
        // Lights per block: black none, blue one .. yellow sixteen or more, red over capacity.
        float3 color = float3(0.0f, 0.0f, 0.0f);
        if (s_blockLightHits > MAX_BLOCK_LIGHTS)
        {
            color = float3(1.0f, 0.0f, 0.0f);
        }
        else if (blockLightCount > 0)
        {
            color = lerp(float3(0.0f, 0.2f, 1.0f), float3(1.0f, 1.0f, 0.0f), saturate(float(blockLightCount - 1) / 15.0f));
        }

        const float debugSigma = 0.02f;
        InjectOutput[id] = float4(color * debugSigma, debugSigma);
        return;
    }

    // 2. This froxel.
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
    radiance += BlockLightScattering(worldPos, ray, blockLightCount);

    InjectOutput[id] = float4(radiance * sigma, sigma);
}
