// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Point and spot light data and math shared by the deferred lighting pass and the volumetric fog.
// The math mirrors scene_graph/light_math.h (unit-tested) - change both together.

#ifndef LIGHT_COMMON_HLSLI
#define LIGHT_COMMON_HLSLI

static const uint LIGHT_TYPE_POINT = 0;
static const uint LIGHT_TYPE_DIRECTIONAL = 1;
static const uint LIGHT_TYPE_SPOT = 2;

// MUST stay field-for-field in sync with ShaderLight in deferred_renderer.h (64 bytes).
struct Light
{
    float3 Position;
    float Range;
    float3 Color;
    float Intensity;
    float3 Direction;
    float SpotCosOuter;     // cos(outer cone / 2), spot lights only
    uint Type;              // LIGHT_TYPE_*
    uint ShadowMap;
    float SpotCosInner;     // cos(inner cone / 2), always above SpotCosOuter
    float FogScattering;    // multiplier on scattering into volumetric fog
};

// (1 - d / range)^2, 0 at and beyond the range. Mirrors light_math::PointAttenuation.
float LightAttenuation(float distance, float range)
{
    if (distance >= range)
    {
        return 0.0f;
    }

    float t = 1.0f - saturate(distance / range);
    return t * t;
}

// Spot cone falloff; lightToPoint is the normalized direction from the light to the lit point.
// Mirrors light_math::SpotFactor.
float SpotFactor(Light light, float3 lightToPoint)
{
    return smoothstep(light.SpotCosOuter, light.SpotCosInner, dot(normalize(light.Direction), lightToPoint));
}

#endif
