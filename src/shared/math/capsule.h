#pragma once

#include "aabb.h"
#include "vector3.h"

namespace mmo
{
	struct Capsule
	{
		Capsule() = default;
		Capsule(const Vector3& pointA, const Vector3& pointB, float radius)
			: m_pointA(pointA), m_pointB(pointB), m_radius(radius)
		{
			m_boundsDirty = true; // Mark bounds as dirty to recalculate
		}

		const Vector3& GetPointA() const { return m_pointA; }
		const Vector3& GetPointB() const { return m_pointB; }
		float GetRadius() const { return m_radius; }

		void Update(const Vector3& pointA, const Vector3& pointB, const float radius)
		{
			this->m_pointA = pointA;
			this->m_pointB = pointB;
			this->m_radius = radius;
			m_boundsDirty = true; // Mark bounds as dirty to recalculate
		}

		const AABB& GetBounds() const
		{
			if (m_boundsDirty)
			{
				m_bounds.min = TakeMinimum(m_pointA, m_pointB) - Vector3(m_radius, m_radius, m_radius);
				m_bounds.max = TakeMaximum(m_pointA, m_pointB) + Vector3(m_radius, m_radius, m_radius);
				m_boundsDirty = false;
			}

			return m_bounds;
		}

	private:
		Vector3 m_pointA;
		Vector3 m_pointB;
		float m_radius = 0.0f;

		mutable bool m_boundsDirty = true;
		mutable AABB m_bounds;
	};
}
