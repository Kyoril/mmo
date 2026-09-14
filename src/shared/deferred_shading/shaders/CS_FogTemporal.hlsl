// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Blends this frame's jittered froxels with last frame's result, reprojected through the previous
// camera. Cells that were outside the previous grid, or all cells after a history reset, use the new
// value alone.

#include "VolumetricFogCommon.hlsli"

Texture3D<float4> CurrentVolume : register(t0);
Texture3D<float4> HistoryVolume : register(t1);
SamplerState LinearClampSampler : register(s0);
RWTexture3D<float4> TemporalOutput : register(u0);

[numthreads(8, 8, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GridWidth || id.y >= GridHeight || id.z >= GridDepth)
    {
        return;
    }

    float4 current = CurrentVolume.Load(int4(id, 0));
    if (HistoryValid < 0.5f)
    {
        TemporalOutput[id] = current;
        return;
    }

    float2 uv = (float2(id.xy) + 0.5f) / float2(GridWidth, GridHeight);
    float3 ray = FogWorldRay(uv);
    float3 worldPos = FogWorldPosition(ray, SliceToDepth((float(id.z) + 0.5f) / float(GridDepth)));

    float4 prevClip = mul(float4(worldPos, 1.0f), PrevViewProj);
    if (prevClip.w <= 1e-4f)
    {
        TemporalOutput[id] = current;
        return;
    }

    float2 prevNdc = prevClip.xy / prevClip.w;
    float3 prevUvw = float3(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f, DepthToSlice(dot(worldPos - PrevCameraPosition, PrevCameraForward)));
    if (any(prevUvw < 0.0f) || any(prevUvw > 1.0f))
    {
        TemporalOutput[id] = current;
        return;
    }

    float4 history = HistoryVolume.SampleLevel(LinearClampSampler, prevUvw, 0.0f);
    TemporalOutput[id] = lerp(current, history, TemporalBlend);
}
