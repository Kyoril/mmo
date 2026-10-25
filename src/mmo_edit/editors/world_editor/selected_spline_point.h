// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "selectable.h"

#include "base/typedefs.h"
#include "math/path_spline.h"

namespace mmo
{
	class SelectedSplinePoint;

	/// @brief Owner of the splines a SelectedSplinePoint edits (an edit mode). Splines are looked up by
	///        id on every access, so a selection never dangles when its spline or point is deleted.
	class ISplinePointOwner
	{
	public:
		virtual ~ISplinePointOwner() = default;

	public:
		/// @brief Gets the spline with the id, or null when it no longer exists.
		virtual PathSpline* FindSelectableSpline(uint64 splineId) = 0;

		/// @brief Gets the ground height a point at x/z is shown at.
		virtual float GetSplinePointGroundHeight(float x, float z) const = 0;

		/// @brief Called after the selection changed a spline (marks it changed).
		virtual void OnSplinePointEdited(uint64 splineId) = 0;

		/// @brief Deletes a point. Must not change the editor selection (it runs while the viewport
		///        iterates the selection; the caller clears it afterwards).
		/// @return False if the point does not exist or the spline would drop below its minimum.
		virtual bool RemoveSplinePoint(uint64 splineId, size_t pointIndex) = 0;

		/// @brief Called when the selection drops the point.
		virtual void OnSplinePointDeselected(const SelectedSplinePoint& selectable) = 0;
	};

	/// @brief Selection handle for one control point of a curved or linear PathSpline.
	/// @details On a curved (3D) spline Translate moves the point freely, height included; on a linear one
	///          it moves on the x/z plane and the point sits on the ground. Rotate turns both tangent handles
	///          together (yaw and pitch of the out tangent; a roll around it is ignored), switching the point
	///          to manual tangents first so the curve does not jump. Scale is not supported. The orientation
	///          is the roll-free frame of the leaving (out) tangent: +X along it, +Y in the vertical plane.
	class SelectedSplinePoint final : public Selectable
	{
	public:
		/// @brief Creates the handle.
		/// @param owner Owner of the spline.
		/// @param splineId Id the owner knows the spline by.
		/// @param pointIndex Index of the point in the spline.
		SelectedSplinePoint(ISplinePointOwner& owner, uint64 splineId, size_t pointIndex);

	public:
		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override;

		/// @brief Not supported for spline points.
		void Duplicate() override {}

		/// @brief Moves the point (height only on curved splines).
		void Translate(const Vector3& delta) override;

		/// @brief Turns both tangents so the out tangent points where the delta turns it (yaw and pitch; no roll).
		void Rotate(const Quaternion& delta) override;

		/// @brief Not supported for spline points.
		void Scale(const Vector3& delta) override {}

		/// @brief Deletes the point (refused by the owner below the spline's minimum point count).
		void Remove() override;

		/// @copydoc Selectable::Deselect
		void Deselect() override;

		/// @brief Moves the point to the position (height only on curved splines).
		void SetPosition(const Vector3& position) const override;

		/// @brief Turns both tangents so the out tangent points along the orientation's +X; roll is ignored.
		void SetOrientation(const Quaternion& orientation) const override;

		/// @brief Not supported for spline points.
		void SetScale(const Vector3& scale) const override {}

		/// @brief Gets the point (its own height on curved splines, the ground on linear ones; zero when the
		///        point no longer exists).
		[[nodiscard]] Vector3 GetPosition() const override;

		/// @brief Gets the roll-free frame of the point's out tangent (yaw and pitch).
		[[nodiscard]] Quaternion GetOrientation() const override;

		/// @brief Always one.
		[[nodiscard]] Vector3 GetScale() const override;

		/// @copydoc Selectable::SupportsScale
		bool SupportsScale() const override { return false; }

		/// @copydoc Selectable::SupportsDuplicate
		bool SupportsDuplicate() const override { return false; }

		/// @brief The widget hides Scale for spline points and follows tangent rotations.
		bool HidesUnsupportedTransformModes() const override { return true; }

	public:
		/// @brief Gets the id of the spline.
		[[nodiscard]] uint64 GetSplineId() const { return m_splineId; }

		/// @brief Gets the index of the point in the spline.
		[[nodiscard]] size_t GetPointIndex() const { return m_pointIndex; }

		/// @brief Whether the spline and point still exist.
		[[nodiscard]] bool IsValid() const;

	private:
		/// @brief Gets the spline if it still has the point, else null.
		[[nodiscard]] PathSpline* Resolve() const;

		/// @brief Rotates both tangents of the point (makes them manual first).
		void RotateTangents(const Quaternion& rotation) const;

		/// @brief The rotation from the current out tangent frame to the frame of a new out direction.
		[[nodiscard]] Quaternion RotationToward(const Vector3& direction) const;

		/// @brief Whether the spline's points carry a height (curved splines).
		static bool HasHeight(const PathSpline& spline);

	private:
		ISplinePointOwner& m_owner;
		uint64 m_splineId;
		size_t m_pointIndex;
	};
}
