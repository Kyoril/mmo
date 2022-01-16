// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "vector3.h"
#include "radian.h"
#include "constants.h"
#include "quaternion.h"

namespace mmo
{
	/// Static constant zero vector.
	Vector3 Vector3::Zero;
	Vector3 Vector3::UnitX{ 1.0f, 0.0f, 0.0f };
	Vector3 Vector3::UnitY{ 0.0f, 1.0f, 0.0f };
	Vector3 Vector3::UnitZ{ 0.0f, 0.0f, 1.0f };
	Vector3 Vector3::NegativeUnitX{ -1.0f, 0.0f, 0.0f };
	Vector3 Vector3::NegativeUnitY{ 0.0f, -1.0f, 0.0f };
	Vector3 Vector3::NegativeUnitZ{ 0.0f, 0.0f, -1.0f };
	Vector3 Vector3::UnitScale{ 1.0f, 1.0f, 1.0f };

	Quaternion Vector3::GetRotationTo(const Vector3& dest, const Vector3& fallbackAxis) const
	{
		// Based on Stan Melax's article in Game Programming Gems
		Quaternion q;

		// Copy, since cannot modify local
		Vector3 v0 = *this;
		Vector3 v1 = dest;
		v0.Normalize();
		v1.Normalize();

		const float d = v0.Dot(v1);

		// If dot == 1, vectors are the same
		if (d >= 1.0f)
		{
			return Quaternion::Identity;
		}

		if (d < (1e-6f - 1.0f))
		{
			if (fallbackAxis != Zero)
			{
				// rotate 180 degrees about the fallback axis
				q.FromAngleAxis(Radian(Pi), fallbackAxis);
			}
			else
			{
				// Generate an axis
				Vector3 axis = UnitX.Cross(*this);
				if (axis.IsZeroLength())
				{
					axis = UnitY.Cross(*this);
				}

				axis.Normalize();

				q.FromAngleAxis(Radian(Pi), axis);
			}
		}
		else
		{
			const float s = ::sqrt( (1+d)*2 );
			const float inverseS = 1 / s;

			const Vector3 c = v0.Cross(v1);

			q.x = c.x * inverseS;
			q.y = c.y * inverseS;
			q.z = c.z * inverseS;
			q.w = s * 0.5f;
			q.Normalize();
		}

		return q;
	}
}
