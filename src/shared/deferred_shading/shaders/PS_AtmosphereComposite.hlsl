// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Full-resolution atmosphere composite: bilaterally upsamples the march result and adds the
// closed-form height fog for the rest of each view ray, including everything past the shadow
// range and the sky.

#include "AtmosphereCommon.hlsli"

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SceneTexture : register(t0);
Texture2D NormalTexture : register(t1);
Texture2D MarchTexture : register(t2);

// MUST stay in sync with AtmosphereCompositeConstants in atmosphere_pass.cpp.
cbuffer AtmosphereCompositeBuffer : register(b2)
{
    float MarchDistance;    // 0 when the march did not run
    float SkyDistance;
    uint Divisor;
    uint DebugMode;
    int2 LowResSize;
    float2 _CompositePadding;
};

float DistanceOrSky(float depth)
{
    return depth <= 0.0f ? SkyDistance : depth;
}

float4 UpsampleMarch(int2 pixel, float pixelDistance)
{
    if (Divisor <= 1)
    {
        return MarchTexture.Load(int3(min(pixel, LowResSize - 1), 0));
    }

    uint fullWidth;
    uint fullHeight;
    NormalTexture.GetDimensions(fullWidth, fullHeight);

    float2 lowPosition = (float2(pixel) + 0.5f) / float(Divisor) - 0.5f;
    int2 basePixel = int2(floor(lowPosition));
    float2 fraction = lowPosition - float2(basePixel);

    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    float bestDelta = 1e20f;
    float4 nearest = float4(0.0f, 0.0f, 0.0f, 1.0f);

    [unroll]
    for (int y = 0; y <= 1; ++y)
    {
        [unroll]
        for (int x = 0; x <= 1; ++x)
        {
            int2 lowPixel = clamp(basePixel + int2(x, y), int2(0, 0), LowResSize - 1);
            int2 fullPixel = min(lowPixel * int(Divisor), int2(fullWidth, fullHeight) - 1);
            float neighbourDistance = DistanceOrSky(NormalTexture.Load(int3(fullPixel, 0)).a);

            float bilinear = (x == 0 ? 1.0f - fraction.x : fraction.x) * (y == 0 ? 1.0f - fraction.y : fraction.y);
            float delta = abs(neighbourDistance - pixelDistance);
            float depthWeight = 1.0f / (1e-3f + delta / max(pixelDistance, 1.0f));

            float4 value = MarchTexture.Load(int3(lowPixel, 0));
            sum += value * bilinear * depthWeight;
            weightSum += bilinear * depthWeight;

            if (delta < bestDelta)
            {
                bestDelta = delta;
                nearest = value;
            }
        }
    }

    return weightSum > 1e-4f ? sum / weightSum : nearest;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    int2 pixel = int2(input.Position.xy);
    float4 scene = SceneTexture.Load(int3(pixel, 0));
    float sceneDistance = DistanceOrSky(NormalTexture.Load(int3(pixel, 0)).a);

    float3 rayDir = WorldViewRay(input.TexCoord);
    float nearDistance = min(sceneDistance, MarchDistance);
    float4 march = MarchDistance > 0.0f ? UpsampleMarch(pixel, sceneDistance) : float4(0.0f, 0.0f, 0.0f, 1.0f);

    float tailTau = FogOpticalDepth(CameraPosition.y + rayDir.y * nearDistance, rayDir.y, sceneDistance - nearDistance);
    float tailTransmittance = exp(-tailTau);
    float3 tailInscatter = march.a * FogSource(dot(rayDir, SunDirection), 1.0f) * (1.0f - tailTransmittance);

    if (DebugMode == 1)
    {
        return float4(march.rgb + tailInscatter, 1.0f);
    }

    if (DebugMode == 2)
    {
        return float4((march.a * tailTransmittance).xxx, 1.0f);
    }

    if (DebugMode == 3)
    {
        return float4(march.rgb, 1.0f);
    }

    return float4(scene.rgb * march.a * tailTransmittance + march.rgb + tailInscatter, scene.a);
}
