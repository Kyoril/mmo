// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "selected_spline_point.h"

#include "world_editor_instance.h"

#include <cmath>

namespace mmo
{
	namespace
	{
		/// Smallest squared length a direction may have to define an orientation.
		constexpr float MinDirectionLengthSq = 1.0e-8f;

		/// Squared sin(angle / 2) below which a rotation counts as none (about 0.006 degrees).
		constexpr float MinRotationSinHalfAngleSq = 2.5e-9f;

		/// The roll-free frame of a direction: yaw around world up turns +X to the direction's ground
		/// projection, then pitch around the horizontal axis perpendicular to it tilts +X onto the
		/// direction. Its +X is the direction and its +Y stays in the vertical plane through it.
		/// Identity for a zero direction.
		Quaternion FrameOf(Vector3 direction)
		{
			if (direction.GetSquaredLength() < MinDirectionLengthSq)
			{
				return Quaternion::Identity;
			}
			direction.Normalize();

			Vector3 ground(direction.x, 0.0f, direction.z);
			if (ground.GetSquaredLength() < MinDirectionLengthSq)
			{
				// Straight up or down: no yaw, a quarter turn of pitch.
				return Vector3::UnitX.GetRotationTo(direction, Vector3::UnitZ);
			}
			ground.Normalize();

			const Quaternion yaw = Vector3::UnitX.GetRotationTo(ground, Vector3::UnitY);
			const Quaternion pitch = ground.GetRotationTo(direction, Vector3::UnitZ);
			Quaternion frame = pitch * yaw;
			frame.Normalize();
			return frame;
		}

		Vector3 OutTangent(const SplineTangents& t)
		{
			return Vector3(t.outX, t.outY, t.outZ);
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

	bool SelectedSplinePoint::HasHeight(const PathSpline& spline)
	{
		return spline.interpolation != SplineInterpolation::Linear;
	}

	void SelectedSplinePoint::Translate(const Vector3& delta)
	{
		PathSpline* spline = Resolve();
		if (!spline)
		{
			return;
		}

		const bool withHeight = HasHeight(*spline);
		if (delta.x == 0.0f && delta.z == 0.0f && (!withHeight || delta.y == 0.0f))
		{
			return;
		}

		SplinePoint& point = spline->points[m_pointIndex];
		point.x += delta.x;
		point.z += delta.z;
		if (withHeight)
		{
			point.y += delta.y;
		}
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

		const bool withHeight = HasHeight(*spline);
		SplinePoint& point = spline->points[m_pointIndex];
		if (point.x == position.x && point.z == position.z && (!withHeight || point.y == position.y))
		{
			return;
		}

		point.x = position.x;
		point.z = position.z;
		if (withHeight)
		{
			point.y = position.y;
		}
		m_owner.OnSplinePointEdited(m_splineId);
		positionChanged(*this);
	}

	void SelectedSplinePoint::RotateTangents(const Quaternion& rotation) const
	{
		PathSpline* spline = Resolve();
		if (!spline)
		{
			return;
		}

		// A (numerically) zero rotation, e.g. a roll or an unchanged orientation, must not switch an
		// automatic point to manual. The vector part is sin(angle / 2) times the axis.
		const float sinHalfAngleSq = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z;
		if (sinHalfAngleSq < MinRotationSinHalfAngleSq)
		{
			return;
		}

		// Manual first: an automatic point would recompute its tangents and ignore the rotation.
		MakeTangentsManual(*spline, m_pointIndex);
		SplinePoint& point = spline->points[m_pointIndex];
		const Vector3 in = rotation * Vector3(point.inX, point.inY, point.inZ);
		const Vector3 out = rotation * Vector3(point.outX, point.outY, point.outZ);
		point.inX = in.x;
		point.inZ = in.z;
		point.outX = out.x;
		point.outZ = out.z;
		// Linear splines have no height; their tangents would not be used anyway.
		if (HasHeight(*spline))
		{
			point.inY = in.y;
			point.outY = out.y;
		}
		m_owner.OnSplinePointEdited(m_splineId);
		rotationChanged(*this);
	}

	Quaternion SelectedSplinePoint::RotationToward(const Vector3& direction) const
	{
		// From the current roll-free frame to the one of the new out direction: yaw and pitch of the
		// out tangent change, a roll around it never happens (it does not move the direction).
		Quaternion rotation = FrameOf(direction) * GetOrientation().UnitInverse();
		rotation.Normalize();
		return rotation;
	}

	void SelectedSplinePoint::Rotate(const Quaternion& delta)
	{
		const PathSpline* spline = Resolve();
		if (!spline)
		{
			return;
		}

		const Vector3 out = OutTangent(GetEffectiveTangents(*spline, m_pointIndex));
		if (out.GetSquaredLength() < MinDirectionLengthSq)
		{
			return;
		}

		Vector3 rotated = delta * out;
		if (!HasHeight(*spline))
		{
			rotated.y = 0.0f;
		}
		RotateTangents(RotationToward(rotated));
	}

	void SelectedSplinePoint::SetOrientation(const Quaternion& orientation) const
	{
		const PathSpline* spline = Resolve();
		if (!spline)
		{
			return;
		}

		// Only the direction of +X matters: it is where the out tangent should point. A roll (around +X)
		// leaves it unchanged and is ignored.
		Vector3 direction = orientation * Vector3::UnitX;
		if (!HasHeight(*spline))
		{
			direction.y = 0.0f;
		}
		RotateTangents(RotationToward(direction));
	}

	Vector3 SelectedSplinePoint::GetPosition() const
	{
		const PathSpline* spline = Resolve();
		if (!spline)
		{
			return Vector3::Zero;
		}

		const SplinePoint& point = spline->points[m_pointIndex];
		const float height = HasHeight(*spline) ? point.y : m_owner.GetSplinePointGroundHeight(point.x, point.z);
		return Vector3(point.x, height, point.z);
	}

	Quaternion SelectedSplinePoint::GetOrientation() const
	{
		const PathSpline* spline = Resolve();
		if (!spline)
		{
			return Quaternion::Identity;
		}

		Vector3 out = OutTangent(GetEffectiveTangents(*spline, m_pointIndex));
		if (!HasHeight(*spline))
		{
			out.y = 0.0f;
		}
		return FrameOf(out);
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
