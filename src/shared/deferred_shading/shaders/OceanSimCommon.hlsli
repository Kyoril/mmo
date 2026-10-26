// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Constants and helpers shared by the FFT ocean compute shaders (CS_Ocean*.hlsl). The layout must
// match OceanSimConstants in ocean_pass.cpp field for field.

#ifndef OCEAN_SIM_COMMON_HLSLI
#define OCEAN_SIM_COMMON_HLSLI

#define OCEAN_N 256
#define OCEAN_LOG2_N 8
#define OCEAN_CASCADES 3

static const float OCEAN_PI = 3.14159265f;
static const float OCEAN_GRAVITY = 9.81f;

cbuffer OceanSimConstants : register(b0)
{
    float4 CascadeSize;      // xyz = patch size in metres per cascade
    float4 BandLow;          // xyz = smallest |k| a cascade owns
    float4 BandHigh;         // xyz = largest |k| a cascade owns (exclusive)

    float2 WindDirection;    // unit vector the wind blows toward (x, z)
    float JonswapAlpha;      // Phillips constant of the JONSWAP fit
    float PeakOmega;         // angular frequency of the spectral peak

    float JonswapGamma;      // peak enhancement
    float SpreadExponent;    // directional spreading exponent s
    float SpreadNorm;        // normalisation of |cos(theta/2)|^(2s) over the circle
    float AmplitudeScale;    // user wave scale

    float SimTime;           // seconds, wrapped to the repeat period
    float DeltaTime;         // seconds since the last update
    float Choppiness;        // horizontal displacement factor
    float RepeatOmega;       // 2*pi / repeat period; dispersion is quantised to it

    float FoamThreshold;     // Jacobian below which crests turn white
    float FoamGrowth;        // per second
    float FoamDecay;         // per second
    uint FftPass;            // 0 = rows, 1 = columns

    uint Seed;
    float ShortWaveLength;   // waves much shorter than this (m) are damped away
    float2 _SimPadding;

    float4 CascadeWeight;    // xyz = amplitude multiplier per cascade
};

// Wave vector of a texel: k = 2*pi*(n - N/2) / L, so texel N/2 is the zero frequency.
float2 WaveVector(uint2 texel, uint cascade)
{
    float2 n = (float2)texel - (float)(OCEAN_N / 2);
    return n * (2.0f * OCEAN_PI / CascadeSize[cascade]);
}

float2 ComplexMul(float2 a, float2 b)
{
    return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float2 ComplexConj(float2 a)
{
    return float2(a.x, -a.y);
}

// Packs the spectra of two real fields into one complex FFT: IFFT(a + i*b) = fieldA + i*fieldB.
float2 PackReal(float2 a, float2 b)
{
    return float2(a.x - b.y, a.y + b.x);
}

#endif
