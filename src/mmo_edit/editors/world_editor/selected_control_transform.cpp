// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "selected_control_transform.h"

#include "world_editor_instance.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	SelectedControlTransform::SelectedControlTransform(IControlTransformOwner& owner, const uint64 controlId, const bool volume)
		: m_owner(owner)
		, m_controlId(controlId)
		, m_volume(volume)
	{
	}

	float SelectedControlTransform::ExtractYaw(const Quaternion& orientation)
	{
		// Quaternion(Radian(yaw), UnitY) * UnitX == (cos(yaw), 0, -sin(yaw)).
		const Vector3 rotatedX = orientation * Vector3::UnitX;
		if (std::abs(rotatedX.x) < 1.0e-6f && std::abs(rotatedX.z) < 1.0e-6f)
		{
			return 0.0f;
		}

		return std::atan2(-rotatedX.z, rotatedX.x);
	}

	Quaternion SelectedControlTransform::FromYaw(const float yaw)
	{
		return Quaternion(Radian(yaw), Vector3::UnitY);
	}

	void SelectedControlTransform::Visit(SelectableVisitor& visitor)
	{
		visitor.Visit(*this);
	}

	bool SelectedControlTransform::IsValid() const
	{
		return m_owner.FindTransformControl(m_controlId) != nullptr;
	}

	void SelectedControlTransform::Translate(const Vector3& delta)
	{
		pcg::graph::ControlInstance* control = m_owner.FindTransformControl(m_controlId);
		if (!control || (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f))
		{
			return;
		}

		control->position += delta;
		m_owner.OnControlTransformEdited(m_controlId);
		positionChanged(*this);
	}

	void SelectedControlTransform::Rotate(const Quaternion& delta)
	{
		pcg::graph::ControlInstance* control = m_owner.FindTransformControl(m_controlId);
		if (!control)
		{
			return;
		}

		const float deltaYaw = ExtractYaw(delta);
		if (std::abs(deltaYaw) < 1.0e-6f)
		{
			return;
		}

		control->rotation = FromYaw(ExtractYaw(control->rotation) + deltaYaw);
		m_owner.OnControlTransformEdited(m_controlId);
		rotationChanged(*this);
	}

	void SelectedControlTransform::Scale(const Vector3& delta)
	{
		pcg::graph::ControlInstance* control = m_owner.FindTransformControl(m_controlId);
		if (!control || !m_volume)
		{
			return;
		}

		control->halfExtents = Vector3(
			std::max(control->halfExtents.x * delta.x, MinHalfExtent),
			std::max(control->halfExtents.y * delta.y, MinHalfExtent),
			std::max(control->halfExtents.z * delta.z, MinHalfExtent));
		m_owner.OnControlTransformEdited(m_controlId);
		scaleChanged(*this);
	}

	void SelectedControlTransform::Deselect()
	{
		m_owner.OnControlTransformDeselected(*this);
	}

	void SelectedControlTransform::SetPosition(const Vector3& position) const
	{
		pcg::graph::ControlInstance* control = m_owner.FindTransformControl(m_controlId);
		if (!control || control->position == position)
		{
			return;
		}

		control->position = position;
		m_owner.OnControlTransformEdited(m_controlId);
	}

	void SelectedControlTransform::SetOrientation(const Quaternion& orientation) const
	{
		pcg::graph::ControlInstance* control = m_owner.FindTransformControl(m_controlId);
		if (!control)
		{
			return;
		}

		control->rotation = FromYaw(ExtractYaw(orientation));
		m_owner.OnControlTransformEdited(m_controlId);
	}

	void SelectedControlTransform::SetScale(const Vector3& scale) const
	{
		pcg::graph::ControlInstance* control = m_owner.FindTransformControl(m_controlId);
		if (!control || !m_volume)
		{
			return;
		}

		control->halfExtents = Vector3(std::max(scale.x, MinHalfExtent), std::max(scale.y, MinHalfExtent), std::max(scale.z, MinHalfExtent));
		m_owner.OnControlTransformEdited(m_controlId);
	}

	Vector3 SelectedControlTransform::GetPosition() const
	{
		const pcg::graph::ControlInstance* control = m_owner.FindTransformControl(m_controlId);
		return control ? control->position : Vector3::Zero;
	}

	Quaternion SelectedControlTransform::GetOrientation() const
	{
		const pcg::graph::ControlInstance* control = m_owner.FindTransformControl(m_controlId);
		return control ? FromYaw(ExtractYaw(control->rotation)) : Quaternion::Identity;
	}

	Vector3 SelectedControlTransform::GetScale() const
	{
		const pcg::graph::ControlInstance* control = m_owner.FindTransformControl(m_controlId);
		return control && m_volume ? control->halfExtents : Vector3::UnitScale;
	}

	void SelectedControlTransform::NotifyTransformChanged()
	{
		positionChanged(*this);
		rotationChanged(*this);
		scaleChanged(*this);
	}
}
