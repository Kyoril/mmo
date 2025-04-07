// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "deferred_light.h"

namespace mmo
{
    namespace
    {
        const String s_movableType = "DeferredLight";
        AABB s_boundingBox = AABB(Vector3(-0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, 0.5f));
    }

    DeferredLight::DeferredLight(const String& name)
        : MovableObject(name)
    {
        // Set default values
        m_visible = true;
        m_debugDisplay = false;
        m_upperDistance = 0.0f;
        m_squaredUpperDistance = 0.0f;
        m_minPixelSize = 0.0f;
        m_beyondFarDistance = false;
    }

    const String& DeferredLight::GetMovableType() const
    {
        return s_movableType;
    }

    const AABB& DeferredLight::GetBoundingBox() const
    {
        return s_boundingBox;
    }

    float DeferredLight::GetBoundingRadius() const
    {
        // For point and spot lights, the bounding radius is the range
        if (m_type == DeferredLightType::Point || m_type == DeferredLightType::Spot)
        {
            return m_range;
        }

        // For directional lights, the bounding radius is infinite
        return 0.0f;
    }

    void DeferredLight::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
    {
        // Lights don't have renderables
    }

    void DeferredLight::PopulateRenderQueue(RenderQueue& queue)
    {
        // Lights don't have renderables to add to the render queue
    }
}
