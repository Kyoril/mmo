// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Ocean surface vertex shader. The mesh is a polar grid centred on the camera (see
// ocean_grid.h): POSITION.xy is a unit direction, POSITION.z the distance from the camera. Rings get
// further apart with distance, so the vertex density per screen pixel stays roughly constant out to
// the horizon, and a single mesh covers it without seams or cracks.
//
// Every vertex samples the FFT displacement at its undisplaced world position. Cascades whose
// waves are too short for the local vertex spacing fade out (the pixel shader still draws their
// normals), and all of them flatten in shallow water and toward the horizon.

#include "OceanRenderCommon.hlsli"

struct VertexIn
{
    float3 pos : POSITION;
};

struct VertexOut
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float2 baseXZ : TEXCOORD1;
    float waveHeight : TEXCOORD2;
    float waterDepth : TEXCOORD3;
    float mask : TEXCOORD4;
};

VertexOut main(VertexIn input)
{
    VertexOut output;

    float2 direction = input.pos.xy;
    float radius = input.pos.z;
    float2 baseXZ = CameraPosition.xz + direction * radius;

    float3 mask = SampleOceanMask(baseXZ);

    float spacing = max(radius * GridAngularStep, 0.02f);
    float distanceFade = 1.0f - saturate((radius - WaveFadeStart) * WaveFadeInverseRange);

    float3 displacement = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (uint cascade = 0; cascade < OCEAN_CASCADES; ++cascade)
    {
        float size = CascadeSize[cascade];

        // Roughly eight vertices per wavelength of the shortest waves in the cascade are needed;
        // below that the cascade only adds vertex shimmer.
        float resolved = 1.0f - saturate((spacing - size * (1.0f / 96.0f)) / (size * (1.0f / 48.0f)));
        float weight = resolved * ShallowWaveWeight(cascade, mask.y);
        if (weight > 0.0f)
        {
            float3 uvw = float3(baseXZ / size, (float)cascade);
            displacement += OceanDisplacement.SampleLevel(OceanWrapSampler, uvw, 0.0f).xyz * weight;
        }
    }
    displacement *= distanceFade;

    // Swash: in the last few metres of depth the surface breathes with the shore waves.
    float swashZone = 1.0f - saturate(mask.y / 3.0f);
    displacement.y += SHORE_SWASH_HEIGHT * swashZone * sin(2.0f * OCEAN_PI * (OceanTime / SHORE_WAVE_PERIOD));

    float3 worldPos = float3(baseXZ.x, SeaLevel, baseXZ.y) + displacement;

    float4 viewPos = mul(float4(worldPos, 1.0f), matView);
    float4 clipPos = mul(viewPos, matProj);

    // The grid runs past the far clip plane to the horizon. Clamp the depth just inside it instead
    // of letting the rasteriser clip the ocean off: beyond the far plane only the sky (cleared depth)
    // competes, and the ocean must win there.
    clipPos.z = min(clipPos.z, clipPos.w * 0.999999f);

    output.pos = clipPos;
    output.worldPos = worldPos;
    output.baseXZ = baseXZ;
    output.waveHeight = displacement.y;
    output.waterDepth = mask.y;
    output.mask = mask.x;
    return output;
}
