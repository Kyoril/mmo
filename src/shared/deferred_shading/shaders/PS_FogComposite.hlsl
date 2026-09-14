// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Applies the integrated fog volume to the lit opaque scene and continues with the closed-form height
// fog (sun unshadowed) from the volume's far plane to the pixel. With the volume disabled the whole
// ray uses the closed form.

#include "VolumetricFogCommon.hlsli"

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

Texture2D SceneTexture : register(t0);
Texture2D NormalTexture : register(t1);          // a = linear radial depth, 0 = sky
Texture3D<float4> IntegratedVolume : register(t2);
Texture3D<float4> DensityVolume : register(t3);
SamplerState LinearClampSampler : register(s0);

float4 main(PS_INPUT input) : SV_TARGET
{
    int2 pixel = int2(input.Position.xy);
    float4 scene = SceneTexture.Load(int3(pixel, 0));
    float radialDepth = NormalTexture.Load(int3(pixel, 0)).a;
    float sceneDistance = radialDepth <= 0.0f ? SkyDistance : radialDepth;

    float3 ray = FogWorldRay(input.TexCoord);
    float cosForward = max(dot(ray, CameraForward), 1e-4f);
    float viewDepth = sceneDistance * cosForward;

    float3 volumeScattered = float3(0.0f, 0.0f, 0.0f);
    float volumeTransmittance = 1.0f;
    float volumeEndDistance = 0.0f;
    float slice = 0.0f;

    if (VolumeEnabled > 0.5f)
    {
        float volumeDepth = min(viewDepth, FarDistance);
        slice = DepthToSlice(volumeDepth);

        float w = IntegratedTexelCoordinate(slice);
        float4 integrated = IntegratedVolume.SampleLevel(LinearClampSampler, float3(input.TexCoord, w), 0.0f);
        volumeScattered = integrated.rgb;
        volumeTransmittance = integrated.a;
        volumeEndDistance = volumeDepth / cosForward;
    }

    float tailLength = max(sceneDistance - volumeEndDistance, 0.0f);
    float tailTau = FogOpticalDepth(CameraPosition.y + ray.y * volumeEndDistance, ray.y, tailLength);
    float tailTransmittance = exp(-tailTau);
    float3 tailScattered = volumeTransmittance * FogSource(dot(ray, SunDirection), 1.0f) * (1.0f - tailTransmittance);

    float totalTransmittance = volumeTransmittance * tailTransmittance;
    float3 totalScattered = volumeScattered + tailScattered;

    if (DebugMode == 1)
    {
        return float4(totalScattered, 1.0f);
    }

    if (DebugMode == 2)
    {
        return float4(totalTransmittance.xxx, 1.0f);
    }

    if (DebugMode == 3)
    {
        float sigma = VolumeEnabled > 0.5f ? DensityVolume.SampleLevel(LinearClampSampler, float3(input.TexCoord, saturate(slice)), 0.0f).a : 0.0f;
        return float4(saturate(sigma / max(FogDensity * 4.0f, 1e-6f)).xxx, 1.0f);
    }

    return float4(scene.rgb * totalTransmittance + totalScattered, scene.a);
}
