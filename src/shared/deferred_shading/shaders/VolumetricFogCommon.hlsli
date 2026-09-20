// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Froxel volumetric fog: the pass constant buffer and the grid math shared by the inject, temporal,
// integrate and composite shaders. The slice math and noise weighting mirror
// deferred_shading/volumetric_fog_settings.h (unit-tested) - change both together.

#ifndef VOLUMETRIC_FOG_COMMON_HLSLI
#define VOLUMETRIC_FOG_COMMON_HLSLI

#include "AtmosphereCommon.hlsli"

// MUST stay field-for-field in sync with VolumetricFogConstants in volumetric_fog_pass.cpp.
cbuffer VolumetricFogBuffer : register(b2)
{
    column_major matrix InverseViewProj;
    column_major matrix PrevViewProj;
    float3 CameraForward;
    float Jitter;               // [0, 1) depth offset of this frame's samples, in slices
    float3 PrevCameraPosition;
    float TemporalBlend;        // history weight
    float3 PrevCameraForward;
    float HistoryValid;         // 1 when the history volume may be reprojected
    uint GridWidth;
    uint GridHeight;
    uint GridDepth;
    uint DebugMode;             // 0 off, 1 scattered light, 2 transmittance, 3 density
    float NearDistance;
    float FarDistance;
    float NoiseSize;            // metres per noise tile
    float NoiseAmount;          // 0 smooth .. 1 patchy
    float2 WindOffset;          // accumulated wind scroll in noise tiles, wrapped to [0, 1)
    float SkyDistance;
    float VolumeEnabled;        // 0 when the froxel volume is off (analytic fog only)
};

// View depth of a normalized slice coordinate, exponential between NearDistance and FarDistance.
float SliceToDepth(float slice01)
{
    return NearDistance * pow(abs(FarDistance / NearDistance), slice01);
}

// Normalized slice coordinate of a view depth: 0 at or before the near plane, above 1 past the far plane.
float DepthToSlice(float viewDepth)
{
    if (viewDepth <= NearDistance)
    {
        return 0.0f;
    }

    return log(viewDepth / NearDistance) / log(FarDistance / NearDistance);
}

// Density multiplier of a noise sample. Averages 1 over uniform noise.
float NoiseDensityFactor(float noise, float amount)
{
    return max(0.0f, 1.0f + amount * (2.0f * noise - 1.0f));
}

// Texture-space z coordinate sampling the integrated volume at the end of the slice containing slice01.
// Integrated texel z holds the accumulated value at slice coordinate (z + 1) / GridDepth, one
// texel-width ahead of the texel-centre convention SliceToDepth/DepthToSlice use. Mirrors
// IntegratedTexelCoordinate in volumetric_fog_settings.h.
float IntegratedTexelCoordinate(float slice01)
{
    return saturate((slice01 * float(GridDepth) - 0.5f) / float(GridDepth));
}

// World-space unit ray through a screen UV (y down).
float3 FogWorldRay(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), InverseViewProj);
    return normalize(farPoint.xyz / farPoint.w - CameraPosition);
}

// World position at a view depth (distance along the camera's forward axis) on a ray.
float3 FogWorldPosition(float3 ray, float viewDepth)
{
    return CameraPosition + ray * (viewDepth / max(dot(ray, CameraForward), 1e-4f));
}

#endif
