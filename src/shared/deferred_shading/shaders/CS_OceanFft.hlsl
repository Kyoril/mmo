// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// One dimension of the inverse FFT over every layer of the FFT data array. A thread group owns one
// row (FftPass 0) or one column (FftPass 1) of one layer and runs the whole radix-2 transform in
// group shared memory. Input and output are separate arrays (ping-pong) because D3D11 does not
// guarantee typed UAV loads of RGBA32F.
// Each texel holds two complex numbers (xy and zw) that are transformed side by side.
//
// The transform is unscaled (sum over k of X(k) e^{+i 2 pi k n / N}); the spectrum is already in
// metres. The (-1)^(x+y) shift that moves the zero frequency from texel N/2 to 0 is applied by the
// assembly pass.

#include "OceanSimCommon.hlsli"

Texture2DArray<float4> FftInput : register(t0);
RWTexture2DArray<float4> FftOutput : register(u0);

groupshared float4 g_buffer[2][OCEAN_N];

float4 Butterfly(float4 a, float4 b, float2 twiddle)
{
    float2 wb0 = ComplexMul(twiddle, b.xy);
    float2 wb1 = ComplexMul(twiddle, b.zw);
    return float4(a.xy + wb0, a.zw + wb1);
}

[numthreads(OCEAN_N, 1, 1)]
void main(uint3 groupThread : SV_GroupThreadID, uint3 group : SV_GroupID)
{
    uint index = groupThread.x;
    uint lineIndex = group.y;
    uint layer = group.z;

    uint3 coord = FftPass == 0 ? uint3(index, lineIndex, layer) : uint3(lineIndex, index, layer);

    // Load in bit-reversed order, then iterate decimation-in-time butterflies.
    uint reversed = reversebits(index) >> (32 - OCEAN_LOG2_N);
    g_buffer[0][reversed] = FftInput.Load(int4(coord, 0));

    uint source = 0;
    [unroll]
    for (uint stage = 1; stage <= OCEAN_LOG2_N; ++stage)
    {
        GroupMemoryBarrierWithGroupSync();

        uint size = 1u << stage;
        uint halfSize = size >> 1;
        uint position = index & (size - 1);
        uint blockStart = index - position;
        uint j = position & (halfSize - 1);

        float angle = 2.0f * OCEAN_PI * (float)j / (float)size;
        float2 twiddle = float2(cos(angle), sin(angle));
        if (position >= halfSize)
        {
            twiddle = -twiddle;
        }

        float4 a = g_buffer[source][blockStart + j];
        float4 b = g_buffer[source][blockStart + j + halfSize];
        g_buffer[1 - source][index] = Butterfly(a, b, twiddle);
        source = 1 - source;
    }

    GroupMemoryBarrierWithGroupSync();
    FftOutput[coord] = g_buffer[source][index];
}
