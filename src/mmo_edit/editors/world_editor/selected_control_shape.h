// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "selectable.h"

#include "base/typedefs.h"
#include "pcg/graph/controls.h"

namespace mmo
{
	class SelectedControlShape;

	/// @brief Owner of the placed loop and spline controls a SelectedControlShape edits (an edit mode). Controls
	///        are looked up by id on every access, so a selection never dangles when its control is deleted.
	class IControlShapeOwner
	{
	public:
		virtual ~IControlShapeOwner() = default;

	public:
		/// @brief Gets the placed loop or spline control with the id, or null when it no longer exists.
		virtual pcg::graph::ControlInstance* FindShapeControl(uint64 controlId) = 0;

		/// @brief Called after the selection changed a control (marks it changed).
		virtual void OnControlShapeEdited(uint64 controlId) = 0;

		/// @brief Called when the selection drops the control.
		virtual void OnControlShapeDeselected(const SelectedControlShape& selectable) = 0;
	};

	/// @brief Selection handle for a placed loop or spline control as a whole.
	/// @details The transform widget sits on the control's pivot (pcg::graph::ControlPivot, the mean of its
	///          points). Translate moves every point (heights follow the single point rule, see
	///          pcg::graph::TranslateControl), Rotate turns the control about the pivot around world up (only the
	///          yaw of the delta counts) and Scale scales it about the pivot along world x and z. Point ids, point
	///          widths and the next point id never change. The selection has no orientation or scale of its own:
	///          GetOrientation is the identity, GetScale is one, and SetOrientation / SetScale do nothing.
	///          Remove does nothing: a control is deleted with the Delete button of the mode panel.
	class SelectedControlShape final : public Selectable
	{
	public:
		/// @brief Creates the handle.
		/// @param owner Owner of the control.
		/// @param controlId Id of the placed control.
		SelectedControlShape(IControlShapeOwner& owner, uint64 controlId);

	public:
		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override;

		/// @brief Not supported for placed controls.
		void Duplicate() override {}

		/// @brief Moves the whole control by the world-space offset.
		void Translate(const Vector3& delta) override;

		/// @brief Turns the whole control about its pivot by the rotation about +Y contained in the delta.
		void Rotate(const Quaternion& delta) override;

		/// @brief Scales the whole control about its pivot by the x and z factors of the delta (y is ignored).
		void Scale(const Vector3& delta) override;

		/// @brief Does nothing (see the class description).
		void Remove() override {}

		/// @copydoc Selectable::Deselect
		void Deselect() override;

		/// @brief Moves the whole control so its pivot lands on the position.
		void SetPosition(const Vector3& position) const override;

		/// @brief Does nothing (see the class description).
		void SetOrientation(const Quaternion& /*orientation*/) const override {}

		/// @brief Does nothing (see the class description).
		void SetScale(const Vector3& /*scale*/) const override {}

		/// @brief Gets the control's pivot (zero when it no longer exists).
		[[nodiscard]] Vector3 GetPosition() const override;

		/// @brief Gets the identity (see the class description).
		[[nodiscard]] Quaternion GetOrientation() const override { return Quaternion::Identity; }

		/// @brief Gets one on every axis (see the class description).
		[[nodiscard]] Vector3 GetScale() const override { return Vector3::UnitScale; }

		/// @copydoc Selectable::SupportsScale
		bool SupportsScale() const override { return true; }

		/// @copydoc Selectable::SupportsDuplicate
		bool SupportsDuplicate() const override { return false; }

	public:
		/// @brief Gets the id of the control.
		[[nodiscard]] uint64 GetControlId() const { return m_controlId; }

		/// @brief Whether the control still exists.
		[[nodiscard]] bool IsValid() const;

		/// @brief Whether the control is a closed loop (false for an open spline or a control that no longer exists).
		[[nodiscard]] bool IsLoop() const;

	private:
		IControlShapeOwner& m_owner;
		uint64 m_controlId;
	};
}
