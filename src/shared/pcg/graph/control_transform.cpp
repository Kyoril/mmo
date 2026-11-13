// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "control_transform.h"

#include "rectangle_shape.h"

#include "math/quaternion.h"
#include "math/radian.h"

#include <algorithm>
#include <cmath>

namespace mmo::pcg::graph
{
	namespace
	{
		/// Whether the instance is placed as a spline (loop or open spline) rather than as a transform.
		bool IsSplineShape(const ControlInstance& inst)
		{
			return !inst.spline.points.empty();
		}

		/// A scale factor clamped to the allowed range; non-finite factors count as 1.
		float SanitizeFactor(const float factor)
		{
			if (!std::isfinite(factor))
			{
				return 1.0f;
			}

			return std::clamp(factor, MinControlScaleFactor, MaxControlScaleFactor);
		}

		/// A half extent scaled by factor and kept at MinRectangleHalfExtent or more. A half extent that is not above 0
		/// (a point's unused extents) is returned unchanged.
		float ScaleHalfExtent(const float halfExtent, const float factor)
		{
			if (!(halfExtent > 0.0f))
			{
				return halfExtent;
			}

			return std::max(halfExtent * factor, MinRectangleHalfExtent);
		}

		/// Turns an x/z offset by the yaw whose cosine and sine are given (Quaternion(yaw, +Y) convention:
		/// +X goes to (c, -s), +Z goes to (s, c)).
		void TurnOffset(const float c, const float s, float& x, float& z)
		{
			const float newX = x * c + z * s;
			const float newZ = -x * s + z * c;
			x = newX;
			z = newZ;
		}
	}

	Vector3 ControlPivot(const ControlInstance& inst)
	{
		if (!IsSplineShape(inst))
		{
			return inst.position;
		}

		double sumX = 0.0;
		double sumY = 0.0;
		double sumZ = 0.0;
		for (const SplinePoint& p : inst.spline.points)
		{
			sumX += p.x;
			sumY += p.y;
			sumZ += p.z;
		}

		const double count = static_cast<double>(inst.spline.points.size());
		return Vector3(static_cast<float>(sumX / count), static_cast<float>(sumY / count), static_cast<float>(sumZ / count));
	}

	void TranslateControl(ControlInstance& inst, const Vector3& delta)
	{
		if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z))
		{
			return;
		}

		if (!IsSplineShape(inst))
		{
			inst.position += delta;
			return;
		}

		const bool moveHeight = inst.spline.interpolation == SplineInterpolation::CatmullRom;
		for (SplinePoint& p : inst.spline.points)
		{
			p.x += delta.x;
			p.z += delta.z;
			if (moveHeight)
			{
				p.y += delta.y;
			}
		}
	}

	void RotateControl(ControlInstance& inst, const float yawRadians)
	{
		if (!std::isfinite(yawRadians))
		{
			return;
		}

		if (!IsSplineShape(inst))
		{
			inst.rotation = Quaternion(Radian(yawRadians), Vector3::UnitY) * inst.rotation;
			inst.rotation.Normalize();
			return;
		}

		const Vector3 pivot = ControlPivot(inst);
		const float c = std::cos(yawRadians);
		const float s = std::sin(yawRadians);
		for (SplinePoint& p : inst.spline.points)
		{
			float dx = p.x - pivot.x;
			float dz = p.z - pivot.z;
			TurnOffset(c, s, dx, dz);
			p.x = pivot.x + dx;
			p.z = pivot.z + dz;
			TurnOffset(c, s, p.inX, p.inZ);
			TurnOffset(c, s, p.outX, p.outZ);
		}
	}

	void ScaleControl(ControlInstance& inst, const float scaleX, const float scaleZ)
	{
		const float sx = SanitizeFactor(scaleX);
		const float sz = SanitizeFactor(scaleZ);
		if (sx == 1.0f && sz == 1.0f)
		{
			return;
		}

		if (!IsSplineShape(inst))
		{
			inst.halfExtents.x = ScaleHalfExtent(inst.halfExtents.x, sx);
			inst.halfExtents.z = ScaleHalfExtent(inst.halfExtents.z, sz);
			return;
		}

		const Vector3 pivot = ControlPivot(inst);
		for (SplinePoint& p : inst.spline.points)
		{
			p.x = pivot.x + (p.x - pivot.x) * sx;
			p.z = pivot.z + (p.z - pivot.z) * sz;
			p.inX *= sx;
			p.inZ *= sz;
			p.outX *= sx;
			p.outZ *= sz;
		}
	}
}
