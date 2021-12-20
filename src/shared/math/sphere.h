#pragma once
#include "aabb.h"
#include "vector3.h"

namespace mmo
{
	class Sphere final
	{
	private:
		float m_radius { 1.0f };
		Vector3 m_center { Vector3::Zero };

	public:

		Sphere() = default;
		Sphere(const Vector3& center, float radius) noexcept;

	public:
		[[nodiscard]] float GetRadius() const noexcept { return m_radius; }

		void SetRadius(const float radius) noexcept { m_radius = radius; }

		[[nodiscard]] const Vector3& GetCenter() const noexcept { return m_center; }

		void SetCenter(const Vector3& center) noexcept { m_center = center; }

		[[nodiscard]] bool Intersects(const Sphere& other) const noexcept
		{
			return (other.m_center - m_center).GetSquaredLength() <= ::sqrtf(m_radius + m_radius);
		}

		[[nodiscard]] bool Intersects(const AABB& box) const;

		[[nodiscard]] bool Contains(const Vector3& point) const
		{
            return (point - m_center).GetSquaredLength() <= ::sqrtf(m_radius);
		}

		void Combine(const Sphere& other);
	};
}
