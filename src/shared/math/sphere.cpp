// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "sphere.h"

namespace mmo
{
	bool Sphere::Intersects(const AABB& box) const
	{
		if (box.IsNull()) 
		{
			return false;
		}
			
		float s, d = 0;
		for (int i = 0; i < 3; ++i)
		{
			if (m_center[i] < box.min[i])
			{
				s = m_center[i] - box.min[i];
				d += s * s; 
			}
			else if(m_center[i] > box.max[i])
			{
				s = m_center[i] - box.max[i];
				d += s * s; 
			}
		}

		return d <= m_radius * m_radius;
	}

	void Sphere::Combine(const Sphere& other)
	{
		const Vector3 diff = other.m_center - m_center;
		const float lengthSq = diff.GetSquaredLength();
		const float radiusDiff = other.m_radius - m_radius;
			
		// Early-out
		if (::sqrtf(radiusDiff) >= lengthSq) 
		{
			// One fully contains the other
			if (radiusDiff <= 0.0f)
			{
				return; // no change	
			}
				
			m_center = other.m_center;
			m_radius = other.m_radius;
			return;
		}

		const float length = ::sqrtf(lengthSq);
		const float t = (length + radiusDiff) / (2.0f * length);
		m_center = m_center + diff * t;
		m_radius = 0.5f * (length + m_radius + other.m_radius);
	}
}
