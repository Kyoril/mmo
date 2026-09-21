// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Local fog volume data and math. Mirrors deferred_shading/fog_volume_math.h (unit-tested) - change both together.

#ifndef FOG_VOLUME_COMMON_HLSLI
#define FOG_VOLUME_COMMON_HLSLI

// MUST stay field-for-field in sync with fog_volume::GpuFogVolume (80 bytes).
struct FogVolume
{
    float3 Center;
    float Density;          // already scaled by the time of day
    float3 HalfSize;
    float HeightFalloff;
    float3 Color;
    float EdgeFade;
    float YawSin;
    float YawCos;
    float NoiseAmount;
    float NoiseDetail;
    uint Shape;             // 0 box, 1 ellipsoid
    float3 _Padding;
};

float3 FogVolumeToLocal(FogVolume volume, float3 worldPos)
{
    float3 d = worldPos - volume.Center;
    return float3(d.x * volume.YawCos - d.z * volume.YawSin, d.y, d.x * volume.YawSin + d.z * volume.YawCos);
}

float FogVolumeShapeDistance(FogVolume volume, float3 local)
{
    float3 n = local / volume.HalfSize;
    if (volume.Shape == 1)
    {
        return length(n);
    }
    return max(abs(n.x), max(abs(n.y), abs(n.z)));
}

float FogVolumeEdgeFade(float distance, float edgeFade)
{
    if (distance >= 1.0f)
    {
        return 0.0f;
    }
    if (edgeFade <= 0.0f)
    {
        return 1.0f;
    }
    return smoothstep(0.0f, edgeFade, 1.0f - distance);
}

float FogVolumeHeightFactor(FogVolume volume, float3 local)
{
    return exp(-volume.HeightFalloff * (local.y + volume.HalfSize.y));
}

#endif
