// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Blends this frame's jittered froxels with last frame's result, reprojected through the previous
// camera. Cells that were outside the previous grid, or all cells after a history reset, use the new value
// alone. The sun's scattering is deterministic per cell and never goes through here (see CS_FogInject).

#include "VolumetricFogCommon.hlsli"

Texture3D<float4> CurrentVolume : register(t0);
Texture3D<float4> HistoryVolume : register(t1);
SamplerState LinearClampSampler : register(s0);
RWTexture3D<float4> TemporalOutput : register(u0);

// Catmull-Rom history lookup across the screen axes (linear across slices), from nine bilinear taps. A
// plain bilinear lookup blurs the history a little every frame the camera moves, and at a high history
// weight that blur compounds into a soft smear of every sharp shaft edge while moving.
float4 SampleHistoryCatmullRom(Texture3D<float4> history, float3 uvw)
{
    float2 size = float2(GridWidth, GridHeight);
    float2 samplePos = uvw.xy * size;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - texPos1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12 = w1 + w2;
    float2 texPos0 = (texPos1 - 1.0f) / size;
    float2 texPos3 = (texPos1 + 2.0f) / size;
    float2 texPos12 = (texPos1 + w2 / w12) / size;

    float4 result = 0.0f;
    result += history.SampleLevel(LinearClampSampler, float3(texPos0.x, texPos0.y, uvw.z), 0.0f) * w0.x * w0.y;
    result += history.SampleLevel(LinearClampSampler, float3(texPos12.x, texPos0.y, uvw.z), 0.0f) * w12.x * w0.y;
    result += history.SampleLevel(LinearClampSampler, float3(texPos3.x, texPos0.y, uvw.z), 0.0f) * w3.x * w0.y;
    result += history.SampleLevel(LinearClampSampler, float3(texPos0.x, texPos12.y, uvw.z), 0.0f) * w0.x * w12.y;
    result += history.SampleLevel(LinearClampSampler, float3(texPos12.x, texPos12.y, uvw.z), 0.0f) * w12.x * w12.y;
    result += history.SampleLevel(LinearClampSampler, float3(texPos3.x, texPos12.y, uvw.z), 0.0f) * w3.x * w12.y;
    result += history.SampleLevel(LinearClampSampler, float3(texPos0.x, texPos3.y, uvw.z), 0.0f) * w0.x * w3.y;
    result += history.SampleLevel(LinearClampSampler, float3(texPos12.x, texPos3.y, uvw.z), 0.0f) * w12.x * w3.y;
    result += history.SampleLevel(LinearClampSampler, float3(texPos3.x, texPos3.y, uvw.z), 0.0f) * w3.x * w3.y;
    return max(result, 0.0f);
}

// This frame's value blended with the reprojected history at prevUvw.
float4 Blend(Texture3D<float4> currentVolume, Texture3D<float4> historyVolume, uint3 id, float3 prevUvw)
{
    float4 current = currentVolume.Load(int4(id, 0));
    float4 history = SampleHistoryCatmullRom(historyVolume, prevUvw);
    return lerp(current, history, TemporalBlend);
}

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GridWidth || id.y >= GridHeight || id.z >= GridDepth)
    {
        return;
    }

    bool reproject = HistoryValid >= 0.5f;
    float3 prevUvw = float3(0.0f, 0.0f, 0.0f);
    if (reproject)
    {
        float2 uv = (float2(id.xy) + 0.5f) / float2(GridWidth, GridHeight);
        float3 ray = FogWorldRay(uv);
        float3 worldPos = FogWorldPosition(ray, SliceToDepth((float(id.z) + 0.5f) / float(GridDepth)));

        float4 prevClip = mul(float4(worldPos, 1.0f), PrevViewProj);
        reproject = prevClip.w > 1e-4f;
        if (reproject)
        {
            float2 prevNdc = prevClip.xy / prevClip.w;
            prevUvw = float3(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f, DepthToSlice(dot(worldPos - PrevCameraPosition, PrevCameraForward)));
            reproject = all(prevUvw >= 0.0f) && all(prevUvw <= 1.0f);
        }
    }

    if (!reproject)
    {
        TemporalOutput[id] = CurrentVolume.Load(int4(id, 0));
        return;
    }

    TemporalOutput[id] = Blend(CurrentVolume, HistoryVolume, id, prevUvw);
}
