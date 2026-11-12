// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "selectable.h"

#include "base/typedefs.h"
#include "pcg/graph/controls.h"

namespace mmo
{
	class SelectedControlTransform;

	/// @brief Owner of the placed controls a SelectedControlTransform edits (an edit mode). Controls are
	///        looked up by id on every access, so a selection never dangles when its control is deleted.
	class IControlTransformOwner
	{
	public:
		virtual ~IControlTransformOwner() = default;

	public:
		/// @brief Gets the placed volume or point control with the id, or null when it no longer exists.
		virtual pcg::graph::ControlInstance* FindTransformControl(uint64 controlId) = 0;

		/// @brief Called after the selection changed a control (marks it changed).
		virtual void OnControlTransformEdited(uint64 controlId) = 0;

		/// @brief Called when the selection drops the control.
		virtual void OnControlTransformDeselected(const SelectedControlTransform& selectable) = 0;
	};

	/// @brief Selection handle for the transform of a placed volume or point control.
	/// @details Translate moves the control freely (height included). Rotate only turns it around world up
	///          (yaw; pitch and roll are dropped). On a volume, Scale multiplies the half extents per axis
	///          (each at least 0.5 m) and the scale the widget sees is the half extents; points cannot be
	///          scaled. Remove does nothing: a control is deleted with the Delete button of the mode panel.
	class SelectedControlTransform final : public Selectable
	{
	public:
		/// @brief Smallest half extent of a volume, in metres.
		static constexpr float MinHalfExtent = 0.5f;

	public:
		/// @brief Creates the handle.
		/// @param owner Owner of the control.
		/// @param controlId Id of the placed control.
		/// @param volume Whether the control is a volume (scalable) rather than a point.
		SelectedControlTransform(IControlTransformOwner& owner, uint64 controlId, bool volume);

	public:
		/// @copydoc Selectable::Visit
		void Visit(SelectableVisitor& visitor) override;

		/// @brief Not supported for placed controls.
		void Duplicate() override {}

		/// @brief Moves the control by the world-space offset.
		void Translate(const Vector3& delta) override;

		/// @brief Adds the rotation about +Y contained in the delta to the control's yaw.
		void Rotate(const Quaternion& delta) override;

		/// @brief Multiplies a volume's half extents per axis (no effect on a point).
		void Scale(const Vector3& delta) override;

		/// @brief Does nothing (see the class description).
		void Remove() override {}

		/// @copydoc Selectable::Deselect
		void Deselect() override;

		/// @brief Sets the control's world position.
		void SetPosition(const Vector3& position) const override;

		/// @brief Sets the control's yaw from the rotation about +Y contained in the orientation.
		void SetOrientation(const Quaternion& orientation) const override;

		/// @brief Sets a volume's half extents (each at least MinHalfExtent; no effect on a point).
		void SetScale(const Vector3& scale) const override;

		/// @brief Gets the control's world position (zero when it no longer exists).
		[[nodiscard]] Vector3 GetPosition() const override;

		/// @brief Gets the control's rotation (yaw only).
		[[nodiscard]] Quaternion GetOrientation() const override;

		/// @brief Gets a volume's half extents (one for a point).
		[[nodiscard]] Vector3 GetScale() const override;

		/// @copydoc Selectable::SupportsScale
		bool SupportsScale() const override { return m_volume; }

		/// @copydoc Selectable::SupportsDuplicate
		bool SupportsDuplicate() const override { return false; }

		/// @brief The widget hides Scale for points.
		bool HidesUnsupportedTransformModes() const override { return true; }

	public:
		/// @brief Gets the id of the control.
		[[nodiscard]] uint64 GetControlId() const { return m_controlId; }

		/// @brief Whether the control is a volume.
		[[nodiscard]] bool IsVolume() const { return m_volume; }

		/// @brief Whether the control still exists.
		[[nodiscard]] bool IsValid() const;

		/// @brief Fires the position, rotation and scale changed signals, so the transform widget follows
		///        edits made outside of it (the mode panel).
		void NotifyTransformChanged();

		/// @brief The yaw (rotation about +Y) of an orientation, in radians; pitch and roll are ignored.
		static float ExtractYaw(const Quaternion& orientation);

		/// @brief The rotation about +Y by a yaw in radians.
		static Quaternion FromYaw(float yaw);

	private:
		IControlTransformOwner& m_owner;
		uint64 m_controlId;
		bool m_volume;
	};
}
