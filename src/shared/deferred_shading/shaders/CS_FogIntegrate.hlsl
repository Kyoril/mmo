// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Accumulates the smoothed froxels front to back along each grid column. Texel z holds the light
// scattered toward the camera and the transmittance from the near plane to the END of slice z.

#include "VolumetricFogCommon.hlsli"

Texture3D<float4> ScatteringVolume : register(t0);
RWTexture3D<float4> IntegratedOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GridWidth || id.y >= GridHeight)
    {
        return;
    }

    float2 uv = (float2(id.xy) + 0.5f) / float2(GridWidth, GridHeight);
    float3 ray = FogWorldRay(uv);
    float pathScale = 1.0f / max(dot(ray, CameraForward), 1e-4f);

    float3 scattered = float3(0.0f, 0.0f, 0.0f);
    float transmittance = 1.0f;

    [loop]
    for (uint z = 0; z < GridDepth; ++z)
    {
        float4 cell = ScatteringVolume.Load(int4(id.xy, z, 0));
        float sigma = max(cell.a, 0.0f);

        float thickness = (SliceToDepth(float(z + 1) / float(GridDepth)) - SliceToDepth(float(z) / float(GridDepth))) * pathScale;
        float sliceTransmittance = exp(-sigma * thickness);
        float3 radiance = cell.rgb / max(sigma, 1e-6f);

        scattered += transmittance * radiance * (1.0f - sliceTransmittance);
        transmittance *= sliceTransmittance;

        IntegratedOutput[uint3(id.xy, z)] = float4(scattered, transmittance);
    }
}
