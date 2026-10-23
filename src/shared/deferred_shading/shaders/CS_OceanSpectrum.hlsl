// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Initial ocean spectrum h0(k) per cascade. Runs only when the wave settings change.
//
// Energy model: JONSWAP frequency spectrum (fetch-limited wind sea) with a normalised
// |cos(theta/2)|^(2s) directional spreading, converted to wave-number space through the deep-water
// dispersion relation w = sqrt(g k). Each texel draws one complex Gaussian sample scaled so the
// height variance of the summed field equals the integral of the spectrum, i.e. heights come out in
// metres before the user's wave scale.
//
// Output (per cascade layer): xy = h0(k), zw = conj(h0(-k)), so the time evolution needs one load.

#include "OceanSimCommon.hlsli"

RWTexture2DArray<float4> Spectrum : register(u0);

uint Hash(uint x)
{
    // PCG-style integer hash.
    uint state = x * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Uniform01(inout uint state)
{
    state = Hash(state);
    // (0, 1]: never zero, so log() below stays finite.
    return ((float)(state >> 8) + 1.0f) / 16777216.0f;
}

// Two independent standard normal samples (Box-Muller), deterministic per texel and cascade.
float2 Gaussian(uint2 texel, uint cascade)
{
    uint state = Hash(texel.x + Hash(texel.y + Hash(cascade + Seed * 7919u)));
    float u1 = Uniform01(state);
    float u2 = Uniform01(state);
    float r = sqrt(-2.0f * log(u1));
    float a = 2.0f * OCEAN_PI * u2;
    return float2(r * cos(a), r * sin(a));
}

float Jonswap(float omega)
{
    float sigma = omega <= PeakOmega ? 0.07f : 0.09f;
    float d = (omega - PeakOmega) / (sigma * PeakOmega);
    float r = exp(-0.5f * d * d);
    float ratio = PeakOmega / omega;
    float r4 = ratio * ratio * ratio * ratio;
    return JonswapAlpha * OCEAN_GRAVITY * OCEAN_GRAVITY / pow(omega, 5.0f) * exp(-1.25f * r4) * exp2(r * log2(JonswapGamma));
}

float Spreading(float2 kDir)
{
    float cosTheta = clamp(dot(kDir, WindDirection), -1.0f, 1.0f);
    // cos(theta / 2) from cos(theta), without an acos.
    float halfCos = sqrt(saturate(0.5f * (1.0f + cosTheta)));
    return SpreadNorm * pow(max(halfCos, 1e-6f), 2.0f * SpreadExponent);
}

float2 H0(uint2 texel, uint cascade)
{
    float2 k = WaveVector(texel, cascade);
    float kLength = length(k);
    if (kLength < BandLow[cascade] || kLength >= BandHigh[cascade] || kLength < 1e-4f)
    {
        return float2(0.0f, 0.0f);
    }

    float omega = sqrt(OCEAN_GRAVITY * kLength);
    float dOmegaDk = OCEAN_GRAVITY / (2.0f * omega);

    // S(k_x, k_z) = S(w) * D(theta) * (dw/dk) / k   (polar -> cartesian Jacobian is 1/k)
    float spectrum = Jonswap(omega) * Spreading(k / kLength) * dOmegaDk / kLength;

    // Damp the short-wave tail. Physically it carries most of the surface slope, which reads as
    // noisy chop; the stylised look wants smooth rolling swell with the ripples as a fine overlay.
    spectrum *= exp(-kLength * kLength * ShortWaveLength * ShortWaveLength);
    spectrum *= CascadeWeight[cascade] * CascadeWeight[cascade];

    float deltaK = 2.0f * OCEAN_PI / CascadeSize[cascade];
    float amplitude = 0.5f * deltaK * sqrt(max(spectrum, 0.0f)) * AmplitudeScale;
    return Gaussian(texel, cascade) * amplitude;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 texel = id.xy;
    uint cascade = id.z;
    if (texel.x >= OCEAN_N || texel.y >= OCEAN_N || cascade >= OCEAN_CASCADES)
    {
        return;
    }

    // Texel of -k: n -> N - n (mod N).
    uint2 mirrored = (uint2(OCEAN_N, OCEAN_N) - texel) % OCEAN_N;
    float2 h0 = H0(texel, cascade);
    float2 h0Minus = H0(mirrored, cascade);
    Spectrum[uint3(texel, cascade)] = float4(h0, ComplexConj(h0Minus));
}
