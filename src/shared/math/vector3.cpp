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
        float a = ::sqrtf(((const Vector3*)this)->GetSquaredLength() * dest.GetSquaredLength());
        float b = a + dest.Dot(*this);

        if (::abs(b - 2 * a) <= FLT_EPSILON || a == 0)
        {
            return Quaternion::Identity;
        }
        
        Vector3 axis;

        if (b < 1e-06 * a)
        {
            b = 0.0f;
            axis = fallbackAxis != Vector3::Zero ? fallbackAxis
                : ::abs(x) > ::abs(z) ? Vector3(-y, x, 0.0f)
                : Vector3(0.0f, -z, y);
        }
        else
        {
            axis = this->Cross(dest);
        }

        Quaternion q(b, axis.x, axis.y, axis.z);
        q.Normalize();
        return q;
	}
}
