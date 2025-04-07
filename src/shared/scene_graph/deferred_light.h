// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "light.h"
#include "frame_ui/color.h"

namespace mmo
{
    /// @brief A light that can be used in deferred shading.
    class DeferredLight : public Light
    {
    public:
        /// @brief Creates a new deferred light.
        DeferredLight();

        /// @brief Creates a new deferred light with a name.
        /// @param name The name of the light.
        explicit DeferredLight(const String& name);

        /// @brief Destructor.
        ~DeferredLight() override = default;

    public:
        /// @brief Gets the light parameters for use in the lighting pass.
        /// @param outDirection Output parameter for the light direction.
        /// @param outColor Output parameter for the light color.
        /// @param outIntensity Output parameter for the light intensity.
        void GetLightParameters(Vector3& outDirection, Color& outColor, float& outIntensity) const;

        /// @brief Sets the light intensity.
        /// @param intensity The light intensity.
        void SetIntensity(float intensity) { m_intensity = intensity; }

        /// @brief Gets the light intensity.
        /// @return The light intensity.
        float GetIntensity() const { return m_intensity; }

    private:
        float m_intensity { 1.0f };
    };
}
