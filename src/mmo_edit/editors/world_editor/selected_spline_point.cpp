// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "selected_spline_point.h"

#include "world_editor_instance.h"

#include <cmath>

namespace mmo
{
	namespace
	{
		/// The yaw part of a rotation: its twist around world up (swing-twist decomposition), i.e. the
		/// quaternion's w and y components renormalised. Unlike projecting a rotated axis onto the ground
		/// this does not flip by 180 degrees when roll or pitch exceed 90 degrees (q and -q are the same
		/// rotation, so the sign a large pitch puts on both components cancels). Identity when the
		/// rotation is a pure half turn around a horizontal axis (no defined twist).
		Quaternion YawPart(const Quaternion& rotation)
		{
			const float lengthSq = rotation.w * rotation.w + rotation.y * rotation.y;
			if (lengthSq < 1.0e-8f)
			{
				return Quaternion::Identity;
			}

			const float inverseLength = 1.0f / std::sqrt(lengthSq);
			Quaternion twist(rotation.w * inverseLength, 0.0f, rotation.y * inverseLength, 0.0f);
			if (twist.w < 0.0f)
			{
				twist = Quaternion(-twist.w, 0.0f, -twist.y, 0.0f);
			}
			return twist;
		}
	}

	SelectedSplinePoint::SelectedSplinePoint(ISplinePointOwner& owner, const uint64 splineId, const size_t pointIndex)
		: m_owner(owner)
		, m_splineId(splineId)
		, m_pointIndex(pointIndex)
	{
	}

	void SelectedSplinePoint::Visit(SelectableVisitor& visitor)
	{
		visitor.Visit(*this);
	}

	PathSpline* SelectedSplinePoint::Resolve() const
	{
		PathSpline* spline = m_owner.FindSelectableSpline(m_splineId);
		if (!spline || m_pointIndex >= spline->points.size())
		{
			return nullptr;
		}

		return spline;
	}

	bool SelectedSplinePoint::IsValid() const
	{
		return Resolve() != nullptr;
	}

	void SelectedSplinePoint::Translate(const Vector3& delta)
	{
		PathSpline* spline = Resolve();
		if (!spline || (delta.x == 0.0f && delta.z == 0.0f))
		{
			return;
		}

		SplinePoint& point = spline->points[m_pointIndex];
		point.x += delta.x;
		point.z += delta.z;
		m_owner.OnSplinePointEdited(m_splineId);
		positionChanged(*this);
	}

	void SelectedSplinePoint::SetPosition(const Vector3& position) const
	{
		PathSpline* spline = Resolve();
		if (!spline)
		{
			return;
		}

		SplinePoint& point = spline->points[m_pointIndex];
		if (point.x == position.x && point.z == position.z)
		{
			return;
		}

		point.x = position.x;
		point.z = position.z;
		m_owner.OnSplinePointEdited(m_splineId);
		positionChanged(*this);
	}

	void SelectedSplinePoint::RotateTangents(const Quaternion& yaw) const
	{
		PathSpline* spline = Resolve();
		if (!spline)
		{
			return;
		}

		// Manual first: an automatic point would recompute its tangents and ignore the rotation.
		MakeTangentsManual(*spline, m_pointIndex);
		SplinePoint& point = spline->points[m_pointIndex];
		const Vector3 in = yaw * Vector3(point.inX, 0.0f, point.inZ);
		const Vector3 out = yaw * Vector3(point.outX, 0.0f, point.outZ);
		point.inX = in.x;
		point.inZ = in.z;
		point.outX = out.x;
		point.outZ = out.z;
		m_owner.OnSplinePointEdited(m_splineId);
		rotationChanged(*this);
	}

	void SelectedSplinePoint::Rotate(const Quaternion& delta)
	{
		const Quaternion yaw = YawPart(delta);
		if (yaw == Quaternion::Identity)
		{
			return;
		}

		RotateTangents(yaw);
	}

	void SelectedSplinePoint::SetOrientation(const Quaternion& orientation) const
	{
		// Rotations around one axis commute, so the difference of the two yaws is the delta.
		Quaternion delta = YawPart(orientation) * GetOrientation().UnitInverse();
		delta.Normalize();
		const Quaternion yaw = YawPart(delta);
		if (yaw == Quaternion::Identity)
		{
			return;
		}

		RotateTangents(yaw);
	}

	Vector3 SelectedSplinePoint::GetPosition() const
	{
		const PathSpline* spline = Resolve();
		if (!spline)
		{
			return Vector3::Zero;
		}

		const SplinePoint& point = spline->points[m_pointIndex];
		return Vector3(point.x, m_owner.GetSplinePointGroundHeight(point.x, point.z), point.z);
	}

	Quaternion SelectedSplinePoint::GetOrientation() const
	{
		const PathSpline* spline = Resolve();
		if (!spline)
		{
			return Quaternion::Identity;
		}

		float inX, inZ, outX, outZ;
		GetEffectiveTangents(*spline, m_pointIndex, inX, inZ, outX, outZ);
		Vector3 out(outX, 0.0f, outZ);
		if (out.GetSquaredLength() < 1.0e-8f)
		{
			return Quaternion::Identity;
		}

		out.Normalize();
		return Vector3::UnitX.GetRotationTo(out, Vector3::UnitY);
	}

	Vector3 SelectedSplinePoint::GetScale() const
	{
		return Vector3::UnitScale;
	}

	void SelectedSplinePoint::Remove()
	{
		m_owner.RemoveSplinePoint(m_splineId, m_pointIndex);
	}

	void SelectedSplinePoint::Deselect()
	{
		m_owner.OnSplinePointDeselected(*this);
	}
}
