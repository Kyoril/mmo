// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "deferred_light.h"

namespace mmo
{
    DeferredLight::DeferredLight()
        : Light()
    {
    }

    DeferredLight::DeferredLight(const String& name)
        : Light(name)
    {
    }

    void DeferredLight::GetLightParameters(Vector3& outDirection, Color& outColor, float& outIntensity) const
    {
        // For directional lights, use the derived direction
        if (GetType() == LightType::Directional)
        {
            outDirection = GetDerivedDirection();
        }
        // For point and spot lights, we don't use direction in the same way
        else
        {
            outDirection = Vector3::Zero;
        }

        outColor = GetColor();
        outIntensity = m_intensity;
    }
}
