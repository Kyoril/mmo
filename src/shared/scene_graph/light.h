// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "movable_object.h"
#include "math/vector3.h"
#include "math/vector4.h"

namespace mmo
{
	/// @brief Enumeration of different light types.
	enum class LightType
	{
		/// @brief A point light that emits light in all directions from a single point.
		Point,

		/// @brief A directional light that emits light in a single direction.
		Directional,

		/// @brief A spot light that emits light in a cone from a single point.
		Spot
	};

	/// @brief Class that represents a light source in the scene.
	class Light : public MovableObject
	{
	public:
		/// @brief Creates a new instance of the Light class.
		/// @param type The type of the light.
		explicit Light(LightType type = LightType::Point);

		/// @brief Creates a new instance of the Light class with a name.
		/// @param name The name of the light.
		/// @param type The type of the light.
		explicit Light(const String& name, LightType type = LightType::Point);

		/// @brief Virtual destructor.
		~Light() override = default;

	public:
		/// @brief Gets the type of the light.
		/// @return The type of the light.
		[[nodiscard]] LightType GetType() const noexcept { return m_type; }

		/// @brief Sets the type of the light.
		/// @param type The new type of the light.
		void SetType(LightType type) noexcept { m_type = type; }

		/// @brief Gets the color of the light.
		/// @return The color of the light.
		[[nodiscard]] const Vector4& GetColor() const noexcept { return m_color; }

		/// @brief Sets the color of the light.
		/// @param color The new color of the light.
		void SetColor(const Vector4& color) noexcept { m_color = color; }

		/// @brief Gets the intensity of the light.
		/// @return The intensity of the light.
		[[nodiscard]] float GetIntensity() const noexcept { return m_intensity; }

		/// @brief Sets the intensity of the light.
		/// @param intensity The new intensity of the light.
		void SetIntensity(float intensity) noexcept { m_intensity = intensity; }

		/// @brief Gets the range of the light.
		/// @return The range of the light.
		[[nodiscard]] float GetRange() const noexcept { return m_range; }

		/// @brief Sets the range of the light.
		/// @param range The new range of the light.
		void SetRange(float range) noexcept { m_range = range; }

		/// @brief Gets the inner cone angle of the spot light.
		/// @return The inner cone angle of the spot light in radians.
		[[nodiscard]] float GetInnerConeAngle() const noexcept { return m_innerConeAngle; }

		/// @brief Sets the inner cone angle of the spot light.
		/// @param angle The new inner cone angle of the spot light in radians.
		void SetInnerConeAngle(float angle) noexcept { m_innerConeAngle = angle; }

		/// @brief Gets the outer cone angle of the spot light.
		/// @return The outer cone angle of the spot light in radians.
		[[nodiscard]] float GetOuterConeAngle() const noexcept { return m_outerConeAngle; }

		/// @brief Sets the outer cone angle of the spot light.
		/// @param angle The new outer cone angle of the spot light in radians.
		void SetOuterConeAngle(float angle) noexcept { m_outerConeAngle = angle; }

		/// @brief Gets whether the light casts shadows.
		/// @return True if the light casts shadows, false otherwise.
		[[nodiscard]] bool IsCastingShadows() const noexcept { return m_castShadows; }

		/// @brief Sets whether the light casts shadows.
		/// @param cast True if the light should cast shadows, false otherwise.
		void SetCastShadows(bool cast) noexcept { m_castShadows = cast; }

		/// @brief Gets the direction of the light.
		/// @return The direction of the light.
		[[nodiscard]] Vector3 GetDirection() const;

		/// @brief Sets the direction of the light.
		/// @param direction The new direction of the light.
		void SetDirection(const Vector3& direction);

		/// @brief Gets the derived position of the light in world space.
		/// @return The derived position of the light in world space.
		[[nodiscard]] Vector3 GetDerivedPosition() const;

		// Implement MovableObject abstract methods
		[[nodiscard]] const String& GetMovableType() const override;
		[[nodiscard]] const AABB& GetBoundingBox() const override;
		[[nodiscard]] float GetBoundingRadius() const override;
		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables = false) override;
		void PopulateRenderQueue(RenderQueue& queue) override;

		void NotifyAttachmentChanged(Node* parent, bool isTagPoint) override;

	private:
		static const String LIGHT_TYPE_NAME;
		mutable AABB m_boundingBox;
		LightType m_type;
		Vector4 m_color { 1.0f, 1.0f, 1.0f, 1.0f };
		float m_intensity { 1.0f };
		float m_range { 10.0f };
		float m_innerConeAngle { 0.0f };
		float m_outerConeAngle { 0.0f };
		bool m_castShadows { false };
	};
}
