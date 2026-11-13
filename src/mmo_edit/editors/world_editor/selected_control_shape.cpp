// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "selected_control_shape.h"

#include "selected_control_transform.h"
#include "world_editor_instance.h"

#include "pcg/graph/control_transform.h"

#include <cmath>

namespace mmo
{
	SelectedControlShape::SelectedControlShape(IControlShapeOwner& owner, const uint64 controlId)
		: m_owner(owner)
		, m_controlId(controlId)
	{
	}

	void SelectedControlShape::Visit(SelectableVisitor& visitor)
	{
		visitor.Visit(*this);
	}

	bool SelectedControlShape::IsValid() const
	{
		return m_owner.FindShapeControl(m_controlId) != nullptr;
	}

	bool SelectedControlShape::IsLoop() const
	{
		const pcg::graph::ControlInstance* control = m_owner.FindShapeControl(m_controlId);
		return control && control->spline.closed;
	}

	void SelectedControlShape::Translate(const Vector3& delta)
	{
		pcg::graph::ControlInstance* control = m_owner.FindShapeControl(m_controlId);
		if (!control || (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f))
		{
			return;
		}

		pcg::graph::TranslateControl(*control, delta);
		m_owner.OnControlShapeEdited(m_controlId);
		positionChanged(*this);
	}

	void SelectedControlShape::Rotate(const Quaternion& delta)
	{
		pcg::graph::ControlInstance* control = m_owner.FindShapeControl(m_controlId);
		if (!control)
		{
			return;
		}

		// Same yaw convention as the gizmo of volumes and points.
		const float deltaYaw = SelectedControlTransform::ExtractYaw(delta);
		if (std::abs(deltaYaw) < 1.0e-6f)
		{
			return;
		}

		pcg::graph::RotateControl(*control, deltaYaw);
		m_owner.OnControlShapeEdited(m_controlId);
		rotationChanged(*this);
	}

	void SelectedControlShape::Scale(const Vector3& delta)
	{
		pcg::graph::ControlInstance* control = m_owner.FindShapeControl(m_controlId);
		if (!control || (delta.x == 1.0f && delta.z == 1.0f))
		{
			return;
		}

		pcg::graph::ScaleControl(*control, delta.x, delta.z);
		m_owner.OnControlShapeEdited(m_controlId);
		scaleChanged(*this);
	}

	void SelectedControlShape::Deselect()
	{
		m_owner.OnControlShapeDeselected(*this);
	}

	void SelectedControlShape::SetPosition(const Vector3& position) const
	{
		pcg::graph::ControlInstance* control = m_owner.FindShapeControl(m_controlId);
		if (!control)
		{
			return;
		}

		const Vector3 delta = position - pcg::graph::ControlPivot(*control);
		if (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f)
		{
			return;
		}

		pcg::graph::TranslateControl(*control, delta);
		m_owner.OnControlShapeEdited(m_controlId);
	}

	Vector3 SelectedControlShape::GetPosition() const
	{
		const pcg::graph::ControlInstance* control = m_owner.FindShapeControl(m_controlId);
		return control ? pcg::graph::ControlPivot(*control) : Vector3::Zero;
	}
}
