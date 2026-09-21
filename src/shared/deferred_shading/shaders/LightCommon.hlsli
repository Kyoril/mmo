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

// Conservative spot cone vs sphere test: never false for a sphere containing a lit point of the cone.
// direction must be normalized; cosHalfAngle is the outer cone cosine. Mirrors
// light_math::SpotConeIntersectsSphere.
bool SpotConeIntersectsSphere(float3 apex, float3 direction, float range, float cosHalfAngle, float3 center, float radius)
{
    float3 toCenter = center - apex;
    float lengthSquared = dot(toCenter, toCenter);
    float along = dot(toCenter, direction);
    float sinHalfAngle = sqrt(saturate(1.0f - cosHalfAngle * cosHalfAngle));
    float closest = cosHalfAngle * sqrt(max(lengthSquared - along * along, 0.0f)) - along * sinHalfAngle;

    bool outsideAngle = closest > radius;
    bool beyondRange = along > range + radius;
    bool behindApex = along < -radius;
    return !(outsideAngle || beyondRange || behindApex);
}

// Whether a point or spot light can reach any point of the sphere (xyz center, w radius) and
// scatters into fog at all.
bool LightReachesFogSphere(Light light, float4 sphere)
{
    if (light.Type == LIGHT_TYPE_DIRECTIONAL || light.FogScattering <= 0.0f)
    {
        return false;
    }

    float3 toCenter = sphere.xyz - light.Position;
    float reach = light.Range + sphere.w;
    if (dot(toCenter, toCenter) > reach * reach)
    {
        return false;
    }

    if (light.Type == LIGHT_TYPE_SPOT)
    {
        return SpotConeIntersectsSphere(light.Position, normalize(light.Direction), light.Range, light.SpotCosOuter, sphere.xyz, sphere.w);
    }

    return true;
}

#endif
