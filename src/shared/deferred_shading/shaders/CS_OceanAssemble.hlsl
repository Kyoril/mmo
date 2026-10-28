// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Turns the transformed fields into the textures the surface samples:
//
//   Displacement (no mips) : xyz = (choppiness * Dx, h, choppiness * Dz)
//   Surface (mipmapped)    : x = dh/dx, y = dh/dz (slopes of the choppy surface),
//                            z = x^2 + y^2 (its mips give the slope variance a pixel covers),
//                            w = foam
//
// Foam is persistent: it builds up where the surface folds (Jacobian below the threshold) and
// dissolves exponentially elsewhere, so crests leave white trails that fade behind them.

#include "OceanSimCommon.hlsli"

Texture2DArray<float4> FftData : register(t0);
RWTexture2DArray<float4> Displacement : register(u0);
RWTexture2DArray<float4> Surface : register(u1);
RWTexture2DArray<float> FoamState : register(u2);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 texel = id.xy;
    uint cascade = id.z;
    if (texel.x >= OCEAN_N || texel.y >= OCEAN_N || cascade >= OCEAN_CASCADES)
    {
        return;
    }

    // Undo the half-period frequency shift of the centred spectrum.
    float shiftSign = ((texel.x + texel.y) & 1u) ? -1.0f : 1.0f;
    float4 a = FftData.Load(int4(texel, cascade * 2, 0)) * shiftSign;
    float4 b = FftData.Load(int4(texel, cascade * 2 + 1, 0)) * shiftSign;

    float dx = a.x;
    float dz = a.y;
    float height = a.z;
    float dhdx = a.w;
    float dhdz = b.x;
    float dDxdx = b.y;
    float dDzdz = b.z;
    float dDxdz = b.w;

    float jxx = 1.0f + Choppiness * dDxdx;
    float jzz = 1.0f + Choppiness * dDzdz;
    float jxz = Choppiness * dDxdz;
    float jacobian = jxx * jzz - jxz * jxz;

    // Slopes of the displaced surface: the choppy displacement compresses crests horizontally,
    // which steepens them.
    float2 slope = float2(dhdx / max(jxx, 0.25f), dhdz / max(jzz, 0.25f));

    float foam = FoamState[uint3(texel, cascade)];
    float generation = saturate((FoamThreshold - jacobian) * 2.0f);
    foam += FoamGrowth * generation * (1.0f - foam) * DeltaTime;
    foam -= FoamDecay * foam * DeltaTime;
    foam = saturate(foam);
    FoamState[uint3(texel, cascade)] = foam;

    Displacement[uint3(texel, cascade)] = float4(Choppiness * dx, height, Choppiness * dz, jacobian);
    Surface[uint3(texel, cascade)] = float4(slope, dot(slope, slope), foam);
}
