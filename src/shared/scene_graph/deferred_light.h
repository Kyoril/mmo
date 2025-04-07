// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "movable_object.h"
#include "math/vector3.h"

namespace mmo
{
    /// @brief Enumerates possible light types.
    enum class DeferredLightType
    {
        /// @brief Point light that emits light in all directions.
        Point,

        /// @brief Directional light that emits light in a specific direction.
        Directional,

        /// @brief Spot light that emits light in a cone.
        Spot
    };

    /// @brief Class that represents a light in the scene.
    class DeferredLight : public MovableObject
    {
    public:
        /// @brief Creates a new instance of the DeferredLight class.
        /// @param name The name of the light.
        explicit DeferredLight(const String& name);

        /// @brief Destructor.
        ~DeferredLight() override = default;

    public:
        /// @brief Gets the type flags of the movable object.
        /// @return The type flags of the movable object.
        [[nodiscard]] uint32 GetTypeFlags() const { return 0x00000002; }

        /// @brief Gets the movable type.
        /// @return The movable type.
        [[nodiscard]] const String& GetMovableType() const override;

        /// @brief Gets the bounding box of the light.
        /// @return The bounding box of the light.
        [[nodiscard]] const AABB& GetBoundingBox() const override;

        /// @brief Gets the bounding radius of the light.
        /// @return The bounding radius of the light.
        [[nodiscard]] float GetBoundingRadius() const override;

        /// @brief Visits the renderables of the light.
        /// @param visitor The visitor to visit the renderables.
        /// @param debugRenderables Whether to visit debug renderables.
        void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables = false) override;

        /// @brief Populates the render queue with the light.
        /// @param queue The render queue to populate.
        void PopulateRenderQueue(RenderQueue& queue) override;

        /// @brief Gets the type of the light.
        /// @return The type of the light.
        [[nodiscard]] DeferredLightType GetType() const { return m_type; }

        /// @brief Sets the type of the light.
        /// @param type The type of the light.
        void SetType(DeferredLightType type) { m_type = type; }

        /// @brief Gets the color of the light.
        /// @return The color of the light.
        [[nodiscard]] const Vector3& GetColor() const { return m_color; }

        /// @brief Sets the color of the light.
        /// @param color The color of the light.
        void SetColor(const Vector3& color) { m_color = color; }

        /// @brief Gets the intensity of the light.
        /// @return The intensity of the light.
        [[nodiscard]] float GetIntensity() const { return m_intensity; }

        /// @brief Sets the intensity of the light.
        /// @param intensity The intensity of the light.
        void SetIntensity(float intensity) { m_intensity = intensity; }

        /// @brief Gets the range of the light.
        /// @return The range of the light.
        [[nodiscard]] float GetRange() const { return m_range; }

        /// @brief Sets the range of the light.
        /// @param range The range of the light.
        void SetRange(float range) { m_range = range; }

        /// @brief Gets the direction of the light.
        /// @return The direction of the light.
        [[nodiscard]] const Vector3& GetDirection() const { return m_direction; }

        /// @brief Sets the direction of the light.
        /// @param direction The direction of the light.
        void SetDirection(const Vector3& direction) { m_direction = direction; }

        /// @brief Gets the spot angle of the light.
        /// @return The spot angle of the light.
        [[nodiscard]] float GetSpotAngle() const { return m_spotAngle; }

        /// @brief Sets the spot angle of the light.
        /// @param spotAngle The spot angle of the light.
        void SetSpotAngle(float spotAngle) { m_spotAngle = spotAngle; }

        /// @brief Gets whether the light casts shadows.
        /// @return Whether the light casts shadows.
        [[nodiscard]] bool IsCastingShadows() const { return m_castShadows; }

        /// @brief Sets whether the light casts shadows.
        /// @param castShadows Whether the light casts shadows.
        void SetCastShadows(bool castShadows) { m_castShadows = castShadows; }

    private:
        /// @brief The type of the light.
        DeferredLightType m_type { DeferredLightType::Point };

        /// @brief The color of the light.
        Vector3 m_color { 1.0f, 1.0f, 1.0f };

        /// @brief The intensity of the light.
        float m_intensity { 1.0f };

        /// @brief The range of the light.
        float m_range { 10.0f };

        /// @brief The direction of the light.
        Vector3 m_direction { 0.0f, -1.0f, 0.0f };

        /// @brief The spot angle of the light.
        float m_spotAngle { 45.0f };

        /// @brief Whether the light casts shadows.
        bool m_castShadows { false };
    };
}
