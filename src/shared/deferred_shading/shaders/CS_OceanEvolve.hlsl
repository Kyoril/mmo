// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Advances the ocean spectrum to the current time and builds the eight spectra the surface needs,
// packed two real fields per complex number so four complex FFTs cover them:
//
//   layer 2c    : xy = Dx + i*Dz            zw = h + i*dh/dx
//   layer 2c+1  : xy = dh/dz + i*dDx/dx     zw = dDz/dz + i*dDx/dz
//
// D is the horizontal (choppy) displacement before the choppiness factor, h the height.

#include "OceanSimCommon.hlsli"

Texture2DArray<float4> Spectrum : register(t0);
RWTexture2DArray<float4> FftData : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 texel = id.xy;
    uint cascade = id.z;
    if (texel.x >= OCEAN_N || texel.y >= OCEAN_N || cascade >= OCEAN_CASCADES)
    {
        return;
    }

    float4 h0 = Spectrum.Load(int4(texel, cascade, 0));
    float2 k = WaveVector(texel, cascade);
    float kLength = length(k);

    // Deep-water dispersion, quantised to the repeat period so the animation loops seamlessly and
    // the phase never needs a large time value.
    float omega = sqrt(OCEAN_GRAVITY * kLength);
    omega = floor(omega / RepeatOmega) * RepeatOmega;
    float phase = omega * SimTime;
    float2 rotation = float2(cos(phase), sin(phase));

    float2 h = ComplexMul(h0.xy, rotation) + ComplexMul(h0.zw, ComplexConj(rotation));

    float2 kDir = kLength > 1e-6f ? k / kLength : float2(0.0f, 0.0f);
    float invK = kLength > 1e-6f ? 1.0f / kLength : 0.0f;

    // i*h and -i*h
    float2 ih = float2(-h.y, h.x);

    float2 dx = -ih * kDir.x;                   // Dx = -i kx/|k| h
    float2 dz = -ih * kDir.y;                   // Dz = -i kz/|k| h
    float2 dhdx = ih * k.x;                     // dh/dx = i kx h
    float2 dhdz = ih * k.y;                     // dh/dz = i kz h
    float2 dDxdx = h * (k.x * k.x * invK);      // dDx/dx = kx^2/|k| h
    float2 dDzdz = h * (k.y * k.y * invK);      // dDz/dz = kz^2/|k| h
    float2 dDxdz = h * (k.x * k.y * invK);      // dDx/dz = kx kz/|k| h

    FftData[uint3(texel, cascade * 2)] = float4(PackReal(dx, dz), PackReal(h, dhdx));
    FftData[uint3(texel, cascade * 2 + 1)] = float4(PackReal(dhdz, dDxdx), PackReal(dDzdz, dDxdz));
}
