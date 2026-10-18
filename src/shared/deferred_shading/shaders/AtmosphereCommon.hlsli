// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// View matrices, the camera constant buffer and the height/distance-fog model shared by the deferred
// lighting and atmosphere shaders. The generated forward material shaders carry a copy of the fog
// functions (MaterialCompilerD3D11), and deferred_shading/atmosphere_math.h mirrors them in C++ for
// the unit tests - change all three together.

#ifndef ATMOSPHERE_COMMON_HLSLI
#define ATMOSPHERE_COMMON_HLSLI

// Per-view matrices (b12). Must match the layout uploaded by GraphicsDeviceD3D11 (see
// kPerViewMatrixBufferSlot).
cbuffer ViewMatrices : register(b12)
{
    column_major matrix matView;
    column_major matrix matProj;
    column_major matrix matInvView;
    column_major matrix InverseProjection;
};

// MUST stay field-for-field in sync with PsCameraConstantBuffer in scene.cpp and the
// CameraParameters cbuffer emitted by MaterialCompilerD3D11.
cbuffer CameraBuffer : register(b1)
{
    float3 CameraPosition;
    float FogDensity;               // extinction per metre at FogBaseHeight; 0 = fog off
    float FogHeightFalloff;
    float3 FogTint;                 // ambient radiance of the fog
    column_major matrix InverseViewMatrix;
    float Time;
    float FogBaseHeight;
    float FogAnisotropy;
    float _CameraPadding0;
    float3 SunDirection;            // world space, toward the sun
    float SunIntensity;
    float3 SunColor;
    float ForwardOutputLinear;
    float3 CameraAmbientColor;
    float ForwardExposure;          // exposure the TonemapPass applies; 1 outside the forward pass
    float3 SunScatterColor;
    float ShaftStrength;
    float3 DistanceFogColor;        // linear radiance distant geometry fades into
    float DistanceFogOpacity;       // opacity at the end distance; 0 = distance fog off
    float DistanceFogStart;
    float DistanceFogInverseRange;  // 1 / (end - start)
    float DistanceFogSkyBand;       // view-ray Y up to which the sky is fogged; 0 = sky untouched
    float _CameraPadding1;
};

// Upper bound of the height-fog density exponent. Keeps rays that reach far below the base height
// from overflowing floating point; fog below the base saturates at e^3 ~= 20x the base density
// instead of climbing toward opacity. Mirrored in atmosphere_math.h and material_compiler_d3d11.cpp.
static const float FOG_MAX_DENSITY_EXPONENT = 3.0f;

// Extinction coefficient at world height y.
float FogDensityAt(float y)
{
    return FogDensity * exp(min(-FogHeightFalloff * (y - FogBaseHeight), FOG_MAX_DENSITY_EXPONENT));
}

// Closed-form optical depth along a ray segment of length len starting at height originY.
float FogOpticalDepth(float originY, float dirY, float len)
{
    float sigmaStart = FogDensityAt(originY);
    float sigmaEnd = FogDensityAt(originY + dirY * len);
    float k = FogHeightFalloff * dirY * len;
    if (abs(k) < 1e-3f)
    {
        return len * 0.5f * (sigmaStart + sigmaEnd);
    }

    return len * (sigmaStart - sigmaEnd) / k;
}

// Henyey-Greenstein blended 80/20 with isotropic, normalised so isotropic = 1.
float ScatterPhase(float cosTheta)
{
    float g = FogAnisotropy;
    float g2 = g * g;
    float hg = (1.0f - g2) / pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f);
    return lerp(1.0f, hg, 0.8f);
}

// Radiance the fog scatters toward the camera per unit of (1 - transmittance).
// sunVisibility is the shadow term at the scattering point (1 = lit).
float3 FogSource(float cosTheta, float sunVisibility)
{
    return FogTint + SunScatterColor * SunColor * SunIntensity * ShaftStrength * ScatterPhase(cosTheta) * sunVisibility;
}

// Opacity of the distance fog for geometry at the given distance from the camera.
float DistanceFogAmount(float distance)
{
    return DistanceFogOpacity * saturate((distance - DistanceFogStart) * DistanceFogInverseRange);
}

// Opacity of the distance fog for a sky pixel: full at and below the horizon, fading out over the
// sky band above it, so distant fogged terrain meets a horizon of the same colour.
float DistanceFogSkyAmount(float rayY)
{
    if (DistanceFogSkyBand <= 0.0f)
    {
        return 0.0f;
    }

    return DistanceFogOpacity * (1.0f - smoothstep(0.0f, DistanceFogSkyBand, rayY));
}

// Blends an already height-fogged colour toward the distance fog colour.
float3 ApplyDistanceFog(float3 color, float amount)
{
    return lerp(color, DistanceFogColor, amount);
}

// World-space, normalised view ray through a screen UV.
float3 WorldViewRay(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 viewH = mul(float4(ndc, 1.0f, 1.0f), InverseProjection);
    float3 viewRay = normalize(viewH.xyz / viewH.w);
    return normalize(mul(float4(viewRay, 0.0f), matInvView).xyz);
}

float InterleavedGradientNoise(float2 position)
{
    return frac(52.9829189f * frac(dot(position, float2(0.06711056f, 0.00583715f))));
}

#endif
