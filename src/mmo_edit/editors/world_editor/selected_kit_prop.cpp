// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "selected_kit_prop.h"

#include "world_editor_instance.h"

#include "scene_graph/world_model.h"
#include "scene_graph/scene_node.h"

#include <cmath>

namespace mmo
{
	namespace
	{
		/// Divides component-wise, leaving components of a degenerate divisor undivided.
		Vector3 SafeDivide(const Vector3& value, const Vector3& divisor)
		{
			constexpr float epsilon = 1.0e-6f;
			return Vector3(
				std::abs(divisor.x) > epsilon ? value.x / divisor.x : value.x,
				std::abs(divisor.y) > epsilon ? value.y / divisor.y : value.y,
				std::abs(divisor.z) > epsilon ? value.z / divisor.z : value.z);
		}
	}

	SelectedKitProp::SelectedKitProp(WorldEditorInstance& editor, const uint64 placementId, String kitPath, const size_t propIndex)
		: m_editor(editor)
		, m_placementId(placementId)
		, m_kitPath(std::move(kitPath))
		, m_propIndex(propIndex)
	{
	}

	void SelectedKitProp::Visit(SelectableVisitor& visitor)
	{
		visitor.Visit(*this);
	}

	void SelectedKitProp::Duplicate()
	{
		m_editor.DuplicateKitProp(m_placementId, m_kitPath, m_propIndex);
	}

	void SelectedKitProp::Translate(const Vector3& delta)
	{
		Vector3 position, scale;
		Quaternion orientation;
		if (!GetPlacementTransform(position, orientation, scale))
		{
			return;
		}

		const Vector3 localDelta = SafeDivide(orientation.UnitInverse() * delta, scale);
		m_editor.EditKitProp(m_placementId, m_kitPath, m_propIndex, [&localDelta](WorldModelMeshRef& prop)
		{
			prop.position += localDelta;
		});
		positionChanged(*this);
	}

	void SelectedKitProp::Rotate(const Quaternion& delta)
	{
		Vector3 position, scale;
		Quaternion orientation;
		if (!GetPlacementTransform(position, orientation, scale))
		{
			return;
		}

		// The widget hands out world-space rotation deltas (world' = delta * world); in building-local
		// space that is the delta conjugated by the placement's orientation.
		Quaternion localDelta = orientation.UnitInverse() * delta * orientation;
		localDelta.Normalize();
		m_editor.EditKitProp(m_placementId, m_kitPath, m_propIndex, [&localDelta](WorldModelMeshRef& prop)
		{
			prop.rotation = localDelta * prop.rotation;
			prop.rotation.Normalize();
		});
		rotationChanged(*this);
	}

	void SelectedKitProp::Scale(const Vector3& delta)
	{
		// The widget scales multiplicatively along the prop's own axes.
		m_editor.EditKitProp(m_placementId, m_kitPath, m_propIndex, [&delta](WorldModelMeshRef& prop)
		{
			prop.scale = prop.scale * delta;
		});
		scaleChanged(*this);
	}

	void SelectedKitProp::Remove()
	{
		m_editor.QueueKitPropRemoval(m_placementId, m_kitPath, m_propIndex);
	}

	void SelectedKitProp::SetPosition(const Vector3& position) const
	{
		Vector3 placementPosition, placementScale;
		Quaternion placementOrientation;
		if (!GetPlacementTransform(placementPosition, placementOrientation, placementScale))
		{
			return;
		}

		const Vector3 localPosition = SafeDivide(placementOrientation.UnitInverse() * (position - placementPosition), placementScale);
		m_editor.EditKitProp(m_placementId, m_kitPath, m_propIndex, [&localPosition](WorldModelMeshRef& prop)
		{
			prop.position = localPosition;
		});
		positionChanged(*this);
	}

	void SelectedKitProp::SetOrientation(const Quaternion& orientation) const
	{
		Vector3 placementPosition, placementScale;
		Quaternion placementOrientation;
		if (!GetPlacementTransform(placementPosition, placementOrientation, placementScale))
		{
			return;
		}

		Quaternion localOrientation = placementOrientation.UnitInverse() * orientation;
		localOrientation.Normalize();
		m_editor.EditKitProp(m_placementId, m_kitPath, m_propIndex, [&localOrientation](WorldModelMeshRef& prop)
		{
			prop.rotation = localOrientation;
		});
		rotationChanged(*this);
	}

	void SelectedKitProp::SetScale(const Vector3& scale) const
	{
		Vector3 placementPosition, placementScale;
		Quaternion placementOrientation;
		if (!GetPlacementTransform(placementPosition, placementOrientation, placementScale))
		{
			return;
		}

		const Vector3 localScale = SafeDivide(scale, placementScale);
		m_editor.EditKitProp(m_placementId, m_kitPath, m_propIndex, [&localScale](WorldModelMeshRef& prop)
		{
			prop.scale = localScale;
		});
		scaleChanged(*this);
	}

	Vector3 SelectedKitProp::GetPosition() const
	{
		Vector3 position, scale;
		Quaternion orientation;
		const WorldModelMeshRef* prop = FindProp();
		if (!prop || !GetPlacementTransform(position, orientation, scale))
		{
			return Vector3::Zero;
		}

		return position + orientation * (scale * prop->position);
	}

	Quaternion SelectedKitProp::GetOrientation() const
	{
		Vector3 position, scale;
		Quaternion orientation;
		const WorldModelMeshRef* prop = FindProp();
		if (!prop || !GetPlacementTransform(position, orientation, scale))
		{
			return Quaternion::Identity;
		}

		return orientation * prop->rotation;
	}

	Vector3 SelectedKitProp::GetScale() const
	{
		Vector3 position, scale;
		Quaternion orientation;
		const WorldModelMeshRef* prop = FindProp();
		if (!prop || !GetPlacementTransform(position, orientation, scale))
		{
			return Vector3::UnitScale;
		}

		return scale * prop->scale;
	}

	bool SelectedKitProp::GetPlacementTransform(Vector3& position, Quaternion& orientation, Vector3& scale) const
	{
		const MapEntity* placement = m_editor.FindMapEntity(m_placementId);
		if (!placement || !placement->IsWorldModelEntity())
		{
			return false;
		}

		// Kit props are realized relative to the placement's scene node.
		const SceneNode& node = placement->GetSceneNode();
		position = node.GetDerivedPosition();
		orientation = node.GetDerivedOrientation();
		scale = node.GetDerivedScale();
		return true;
	}

	const WorldModelMeshRef* SelectedKitProp::FindProp() const
	{
		return m_editor.FindKitProp(m_kitPath, m_propIndex);
	}
}
