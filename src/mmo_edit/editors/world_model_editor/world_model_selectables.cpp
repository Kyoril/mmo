// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_selectables.h"

#include "scene_graph/scene_node.h"

#include <algorithm>

namespace mmo
{
	// SelectedWorldModelGroup implementation
	SelectedWorldModelGroup::SelectedWorldModelGroup(WorldModelGroup& group, SceneNode& node)
		: m_group(group)
		, m_node(node)
	{
	}

	Vector3 SelectedWorldModelGroup::GetPosition() const
	{
		return m_node.GetPosition();
	}

	void SelectedWorldModelGroup::SetPosition(const Vector3& position) const
	{
		m_node.SetPosition(position);
	}

	Quaternion SelectedWorldModelGroup::GetOrientation() const
	{
		return m_node.GetOrientation();
	}

	void SelectedWorldModelGroup::SetOrientation(const Quaternion& orientation) const
	{
		m_node.SetOrientation(orientation);
	}

	Vector3 SelectedWorldModelGroup::GetScale() const
	{
		return m_node.GetScale();
	}

	void SelectedWorldModelGroup::SetScale(const Vector3& scale) const
	{
		m_node.SetScale(scale);
	}

	void SelectedWorldModelGroup::Translate(const Vector3& delta)
	{
		m_node.Translate(delta);
	}

	void SelectedWorldModelGroup::Rotate(const Quaternion& delta)
	{
		m_node.Rotate(delta);
	}

	void SelectedWorldModelGroup::Scale(const Vector3& delta)
	{
		Vector3 currentScale = m_node.GetScale();
		m_node.SetScale(currentScale + delta);
	}

	void SelectedWorldModelGroup::Remove()
	{
		removed();
	}

	// SelectedGroupMesh implementation
	SelectedGroupMesh::SelectedGroupMesh(WorldModelGroup& group, size_t meshIndex, SceneNode& node)
		: m_group(group)
		, m_meshIndex(meshIndex)
		, m_node(&node)
	{
	}

	Vector3 SelectedGroupMesh::GetPosition() const
	{
		if (const auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			return meshRef->position;
		}
		return Vector3::Zero;
	}

	void SelectedGroupMesh::SetPosition(const Vector3& position) const
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->position = position;
		}
		m_node->SetPosition(position);
		positionChanged(*this);
	}

	Quaternion SelectedGroupMesh::GetOrientation() const
	{
		if (const auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			return meshRef->rotation;
		}
		return Quaternion::Identity;
	}

	void SelectedGroupMesh::SetOrientation(const Quaternion& orientation) const
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->rotation = orientation;
		}
		m_node->SetOrientation(orientation);
		rotationChanged(*this);
	}

	Vector3 SelectedGroupMesh::GetScale() const
	{
		if (const auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			return meshRef->scale;
		}
		return Vector3::UnitScale;
	}

	void SelectedGroupMesh::SetScale(const Vector3& scale) const
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->scale = scale;
		}
		m_node->SetScale(scale);
		scaleChanged(*this);
	}

	void SelectedGroupMesh::Translate(const Vector3& delta)
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->position += delta;
			m_node->Translate(delta);
			positionChanged(*this);
		}
	}

	void SelectedGroupMesh::Rotate(const Quaternion& delta)
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->rotation = delta * meshRef->rotation;
			m_node->Rotate(delta);
			rotationChanged(*this);
		}
	}

	void SelectedGroupMesh::Scale(const Vector3& delta)
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->scale += delta;
			m_node->SetScale(meshRef->scale);
			scaleChanged(*this);
		}
	}

	void SelectedGroupMesh::Duplicate()
	{
		if (const auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			WorldModelMeshRef newRef = *meshRef;
			
			// Generate a unique name with counter
			// Strip any existing " (N)" suffix to get base name
			String baseName = meshRef->name;
			const auto parenPos = baseName.rfind(" (");
			if (parenPos != String::npos)
			{
				// Check if it ends with " (N)"
				const auto closePos = baseName.rfind(')');
				if (closePos == baseName.length() - 1 && closePos > parenPos)
				{
					baseName = baseName.substr(0, parenPos);
				}
			}
			
			// Find the highest existing counter for this base name
			int maxCounter = 1;
			const auto& meshRefs = m_group.GetMeshRefs();
			for (const auto& ref : meshRefs)
			{
				if (ref.name == baseName)
				{
					// Found base name without counter, so we need at least (2)
					maxCounter = std::max(maxCounter, 1);
				}
				else if (ref.name.find(baseName + " (") == 0)
				{
					// Extract counter from " (N)"
					const auto startPos = baseName.length() + 2; // Skip " ("
					const auto endPos = ref.name.rfind(')');
					if (endPos > startPos)
					{
						try
						{
							const int counter = std::stoi(ref.name.substr(startPos, endPos - startPos));
							maxCounter = std::max(maxCounter, counter);
						}
						catch (...)
						{
							// Ignore parsing errors
						}
					}
				}
			}
			
			newRef.name = baseName + " (" + std::to_string(maxCounter + 1) + ")";
			
			// Don't offset - the copy stays at the current position while original continues moving
			m_group.AddMeshRef(newRef);
			
			// Fire signal with the index of the new mesh
			const size_t newIndex = m_group.GetMeshRefs().size() - 1;
			duplicated(newIndex);
		}
	}

	void SelectedGroupMesh::Remove()
	{
		removed();
	}

	WorldModelMeshRef* SelectedGroupMesh::GetMeshRef()
	{
		return m_group.GetMeshRef(m_meshIndex);
	}

	// SelectedChildWMO implementation
	SelectedChildWMO::SelectedChildWMO(WorldModel& worldModel, size_t childIndex, SceneNode& node)
		: m_worldModel(worldModel)
		, m_childIndex(childIndex)
		, m_node(node)
	{
	}

	Vector3 SelectedChildWMO::GetPosition() const
	{
		if (const auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			return childRef->position;
		}
		return Vector3::Zero;
	}

	void SelectedChildWMO::SetPosition(const Vector3& position) const
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->position = position;
		}
		m_node.SetPosition(position);
	}

	Quaternion SelectedChildWMO::GetOrientation() const
	{
		if (const auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			return childRef->rotation;
		}
		return Quaternion::Identity;
	}

	void SelectedChildWMO::SetOrientation(const Quaternion& orientation) const
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->rotation = orientation;
		}
		m_node.SetOrientation(orientation);
	}

	Vector3 SelectedChildWMO::GetScale() const
	{
		if (const auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			return childRef->scale;
		}
		return Vector3::UnitScale;
	}

	void SelectedChildWMO::SetScale(const Vector3& scale) const
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->scale = scale;
		}
		m_node.SetScale(scale);
	}

	void SelectedChildWMO::Translate(const Vector3& delta)
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->position += delta;
			m_node.Translate(delta);
		}
	}

	void SelectedChildWMO::Rotate(const Quaternion& delta)
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->rotation = delta * childRef->rotation;
			m_node.Rotate(delta);
		}
	}

	void SelectedChildWMO::Scale(const Vector3& delta)
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->scale += delta;
			m_node.SetScale(childRef->scale);
		}
	}

	void SelectedChildWMO::Duplicate()
	{
		if (const auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			WorldModelChildRef newRef = *childRef;
			newRef.name = childRef->name + "_copy";
			newRef.position += Vector3(5.0f, 0.0f, 0.0f);  // Offset the copy
			m_worldModel.AddChildRef(newRef);
		}
	}

	void SelectedChildWMO::Remove()
	{
		removed();
	}

	WorldModelChildRef* SelectedChildWMO::GetChildRef()
	{
		return m_worldModel.GetChildRef(m_childIndex);
	}

	// SelectedWorldModelLight implementation
	SelectedWorldModelLight::SelectedWorldModelLight(WorldModel& worldModel, size_t lightIndex, SceneNode& node)
		: m_worldModel(worldModel)
		, m_lightIndex(lightIndex)
		, m_node(&node)
	{
	}

	Vector3 SelectedWorldModelLight::GetPosition() const
	{
		auto& lights = m_worldModel.GetLights();
		if (m_lightIndex < lights.size())
		{
			return lights[m_lightIndex].position;
		}
		return Vector3::Zero;
	}

	void SelectedWorldModelLight::SetPosition(const Vector3& position) const
	{
		auto& lights = m_worldModel.GetLights();
		if (m_lightIndex < lights.size())
		{
			lights[m_lightIndex].position = position;
		}
		m_node->SetPosition(position);
		positionChanged(*this);
	}

	Quaternion SelectedWorldModelLight::GetOrientation() const
	{
		auto& lights = m_worldModel.GetLights();
		if (m_lightIndex < lights.size())
		{
			return lights[m_lightIndex].rotation;
		}
		return Quaternion::Identity;
	}

	void SelectedWorldModelLight::SetOrientation(const Quaternion& orientation) const
	{
		auto& lights = m_worldModel.GetLights();
		if (m_lightIndex < lights.size())
		{
			lights[m_lightIndex].rotation = orientation;
		}
		m_node->SetOrientation(orientation);
		rotationChanged(*this);
	}

	Vector3 SelectedWorldModelLight::GetScale() const
	{
		return Vector3::UnitScale;
	}

	void SelectedWorldModelLight::SetScale(const Vector3& scale) const
	{
		// Lights don't have scale
	}

	void SelectedWorldModelLight::Translate(const Vector3& delta)
	{
		auto& lights = m_worldModel.GetLights();
		if (m_lightIndex < lights.size())
		{
			lights[m_lightIndex].position += delta;
			m_node->Translate(delta);
			positionChanged(*this);
		}
	}

	void SelectedWorldModelLight::Rotate(const Quaternion& delta)
	{
		auto& lights = m_worldModel.GetLights();
		if (m_lightIndex < lights.size())
		{
			lights[m_lightIndex].rotation = delta * lights[m_lightIndex].rotation;
			m_node->Rotate(delta);
			rotationChanged(*this);
		}
	}

	void SelectedWorldModelLight::Scale(const Vector3& delta)
	{
		// Lights don't support scaling
	}

	void SelectedWorldModelLight::Duplicate()
	{
		auto& lights = m_worldModel.GetLights();
		if (m_lightIndex < lights.size())
		{
			WorldModelLight newLight = lights[m_lightIndex];
			// Don't offset - the copy will be at the same position, and the user will drag it
			lights.push_back(newLight);
			
			// Fire signal with the index of the new light
			const size_t newIndex = lights.size() - 1;
			duplicated(newIndex);
		}
	}

	void SelectedWorldModelLight::Remove()
	{
		removed();
	}

	WorldModelLight* SelectedWorldModelLight::GetLight()
	{
		auto& lights = m_worldModel.GetLights();
		if (m_lightIndex < lights.size())
		{
			return &lights[m_lightIndex];
		}
		return nullptr;
	}

}
