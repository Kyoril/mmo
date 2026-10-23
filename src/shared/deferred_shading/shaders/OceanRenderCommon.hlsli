// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Constants and resources shared by VS_Ocean and PS_Ocean. The cbuffer layout must match
// OceanRenderConstants in ocean_pass.cpp field for field.

#ifndef OCEAN_RENDER_COMMON_HLSLI
#define OCEAN_RENDER_COMMON_HLSLI

#include "AtmosphereCommon.hlsli"

#define OCEAN_CASCADES 3
static const float OCEAN_PI = 3.14159265f;

// Range of the water depth the terrain ocean mask encodes in green (metres). Mirrors
// OceanMaskDepthRange in ocean_settings.h.
static const float OCEAN_MASK_DEPTH_RANGE = 32.0f;

// Range of the shore distance the mask encodes in blue (metres). Mirrors ocean_shore::DistanceRange.
static const float OCEAN_SHORE_DISTANCE_RANGE = 32.0f;

// Shore waves: one breaker reaches the beach every SHORE_WAVE_PERIOD seconds (a whole fraction of the
// simulation's 240 s repeat period, so the loop stays seamless); foam bands sit SHORE_WAVE_SPACING
// metres apart.
static const float SHORE_WAVE_PERIOD = 8.0f;
static const float SHORE_WAVE_SPACING = 3.2f;

// Vertical swash at the waterline (m): the sea level rises and falls with each shore wave, so the
// waterline creeps up the beach and back.
static const float SHORE_SWASH_HEIGHT = 0.07f;

cbuffer OceanRenderConstants : register(b4)
{
    float4 CascadeSize;          // xyz = patch size in metres
    float4 CascadeWaveNumber;    // xyz = 2*pi / size, for shallow-water damping

    float SeaLevel;
    float GridAngularStep;       // vertex spacing of the grid divided by distance
    float ShallowDepth;          // depth at which the longest waves have flattened noticeably
    float MaskEnabled;           // 0 = treat everything as open, deep ocean

    float2 MaskOrigin;           // world xz of the mask window's minimum corner
    float MaskInvExtent;         // 1 / world size of the mask window
    float RefractionStrength;

    float3 Absorption;           // per metre, linear
    float SubsurfaceStrength;

    float3 TroughColor;
    float SubsurfaceAnisotropy;

    float3 CrestColor;
    float AmbientScatter;

    float3 SkyHorizonLinear;     // linear HDR sky colour at the horizon
    float Roughness;

    float3 SkyZenithLinear;      // linear HDR sky colour straight up
    float ReflectionStrength;

    float SunSpecularStrength;
    float FoamBrightness;
    float ShoreFoamStrength;
    float ShoreFoamWidth;         // distance from the shore (m) the rolling foam bands reach

    float CrestHeightScale;      // 1 / wave height that counts as a full crest
    float OceanTime;
    float WaveFadeStart;         // distance beyond which displacement starts to flatten
    float WaveFadeInverseRange;
};

Texture2DArray<float4> OceanDisplacement : register(t0);
Texture2DArray<float4> OceanMask : register(t1);
Texture2DArray<float4> OceanSurface : register(t2);

SamplerState OceanWrapSampler : register(s2);    // s0 belongs to the device's fixed-function sampler
SamplerState OceanClampSampler : register(s3);

// x = 1 where ocean is drawn, y = water depth below sea level (m), z = distance to the shore (m).
float3 SampleOceanMask(float2 worldXZ)
{
    if (MaskEnabled < 0.5f)
    {
        return float3(1.0f, OCEAN_MASK_DEPTH_RANGE, OCEAN_SHORE_DISTANCE_RANGE);
    }

    float2 uv = (worldXZ - MaskOrigin) * MaskInvExtent;
    if (any(uv < 0.0f) || any(uv > 1.0f))
    {
        // Outside the window nothing is known: open ocean, and the depth test against the distant
        // terrain hides it wherever there is land.
        return float3(1.0f, OCEAN_MASK_DEPTH_RANGE, OCEAN_SHORE_DISTANCE_RANGE);
    }

    float4 mask = OceanMask.SampleLevel(OceanClampSampler, float3(uv, 0.0f), 0.0f);
    return float3(mask.x, mask.y * OCEAN_MASK_DEPTH_RANGE, mask.z * OCEAN_SHORE_DISTANCE_RANGE);
}

// How much of a cascade survives in water of the given depth. Long waves feel the bottom first and
// flatten toward the shore; short ones are never damped.
float ShallowWaveWeight(uint cascade, float depth)
{
    float floorWeight = saturate(CascadeWaveNumber[cascade] * ShallowDepth * 0.25f);
    float x = clamp(2.0f * CascadeWaveNumber[cascade] * depth, 0.0f, 20.0f);
    return saturate(floorWeight + (1.0f - floorWeight) * tanh(x));
}

#endif
