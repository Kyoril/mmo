// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_model_frame.h"

#include "client_data/project.h"
#include "game_client/game_player_c.h"
#include "game_client/object_mgr.h"
#include "game_client/unit_handle.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/bone.h"
#include "shared/client_data/proto_client/item_display.pb.h"
#include "shared/client_data/proto_client/model_data.pb.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mmo
{
	UnitModelFrame::UnitModelFrame(const std::string& name)
		: ModelFrame(name)
	{
		AddProperty("Portrait", "false");
	}

	UnitModelFrame::~UnitModelFrame()
	{
		ClearItemAttachments();
	}

	void UnitModelFrame::SetUnit(const std::string& unitName)
	{
		m_unitName = unitName;
		RefreshUnit();
	}

	void UnitModelFrame::CalculatePortraitBounds()
	{
		m_portraitBounds = m_mesh->GetBounds();
		m_portraitBoundsValid = true;
		const auto skeleton = m_mesh->GetSkeleton();
		if (!skeleton)
		{
			return;
		}

		const Bone* head = nullptr;
		for (uint16 i = 0; i < skeleton->GetNumBones(); ++i)
		{
			const Bone* bone = skeleton->GetBone(i);
			String name = bone->GetName();
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (name.find("head") != String::npos && name.find("end") == String::npos &&
				name.find("tip") == String::npos && name.find("nub") == String::npos)
			{
				head = bone;
				break;
			}
		}
		if (!head)
		{
			// A model without a named head still fits safely inside the portrait.
			return;
		}

		std::vector<bool> headBones(skeleton->GetNumBones(), false);
		for (uint16 i = 0; i < skeleton->GetNumBones(); ++i)
		{
			for (const Node* bone = skeleton->GetBone(i); bone; bone = bone->GetParent())
			{
				if (bone == head)
				{
					headBones[i] = true;
					break;
				}
			}
		}

		AABB headBounds;
		bool hasVertices = false;
		const auto includeVertices = [&](const VertexData* data, const Mesh::VertexBoneAssignmentList& assignments)
		{
			if (!data || assignments.empty())
			{
				return;
			}
			const auto* position = data->vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Position);
			if (!position || !data->vertexBufferBinding->IsBufferBound(position->GetSource()))
			{
				return;
			}
			const auto& buffer = data->vertexBufferBinding->GetBuffer(position->GetSource());
			CScopedGxBufferLock<uint8> lock(*buffer, LockOptions::ReadOnly);
			if (!lock.Get())
			{
				return;
			}
			for (const auto& entry : assignments)
			{
				const auto& assignment = entry.second;
				if (assignment.weight < 0.15f || assignment.boneIndex >= headBones.size() ||
					!headBones[assignment.boneIndex] || assignment.vertexIndex >= data->vertexCount)
				{
					continue;
				}
				const auto* vertex = reinterpret_cast<const float*>(lock.Get() +
					(data->vertexStart + assignment.vertexIndex) * buffer->GetVertexSize() + position->GetOffset());
				const Vector3 point(vertex[0], vertex[1], vertex[2]);
				if (!hasVertices)
				{
					headBounds = AABB(point, point);
					hasVertices = true;
				}
				else
				{
					headBounds.Combine(point);
				}
			}
		};
		// Read back vertex positions only once per model, never on each portrait frame.
		includeVertices(m_mesh->sharedVertexData.get(), m_mesh->GetBoneAssignments());
		for (uint16 i = 0; i < m_mesh->GetSubMeshCount(); ++i)
		{
			const auto& subMesh = m_mesh->GetSubMesh(i);
			if (!subMesh.useSharedVertices)
			{
				includeVertices(subMesh.vertexData.get(), subMesh.GetBoneAssignments());
			}
		}
		if (hasVertices && headBounds.GetSize().GetSquaredLength() > 0.0001f)
		{
			m_portraitBounds = headBounds;
		}
	}

	void UnitModelFrame::Update(const float elapsed)
	{
		ModelFrame::Update(elapsed);
		if (!m_mesh || GetProperty("Portrait")->GetValue() != "true")
		{
			return;
		}
		if (!m_portraitBoundsValid)
		{
			CalculatePortraitBounds();
		}

		const auto orientation = m_entityNode->GetDerivedOrientation();
		m_offset = m_entityNode->GetDerivedPosition() + orientation * m_portraitBounds.GetCenter();
		const auto cameraOrientation = m_cameraAnchorNode->GetDerivedOrientation();
		const auto right = cameraOrientation * Vector3::UnitX;
		const auto up = cameraOrientation * Vector3::UnitY;
		const auto towardCamera = cameraOrientation * Vector3::UnitZ;
		const float tanHalfFov = std::tan(m_camera->GetFOVy().GetValueRadians() * 0.5f);
		const float aspect = std::max(m_camera->GetAspectRatio(), 0.01f);
		Vector3 corners[8];
		m_portraitBounds.GetCorners(corners);
		m_zoom = m_camera->GetNearClipDistance();
		for (const auto& corner : corners)
		{
			const Vector3 relative = orientation * (corner - m_portraitBounds.GetCenter());
			const float extent = std::max(std::abs(relative.Dot(up)), std::abs(relative.Dot(right)) / aspect);
			// Padding keeps ears, snouts and idle movement away from the portrait border.
			m_zoom = std::max(m_zoom, relative.Dot(towardCamera) +
				std::max(extent * 1.04f / tanHalfFov, m_camera->GetNearClipDistance()));
		}
		m_cameraAnchorNode->SetPosition(m_offset);
		m_cameraNode->SetPosition(Vector3::UnitZ * m_zoom);
	}

	void UnitModelFrame::RefreshUnit()
	{
		m_equipmentVisualsConnection.disconnect();

		// Mirror future equipment changes of the tracked unit. This also covers the display data of
		// items the client has not cached yet, which arrives asynchronously after the frame was
		// populated for the first time.
		if (const auto unitHandle = ObjectMgr::GetUnitHandleByName(m_unitName))
		{
			if (GameUnitC* unit = unitHandle->Get(); unit && unit->IsPlayer())
			{
				m_equipmentVisualsConnection = unit->AsPlayer().equipmentVisualsChanged.connect(this, &UnitModelFrame::RefreshVisuals);
			}
		}

		RefreshVisuals();
	}

	void UnitModelFrame::RefreshVisuals()
	{
		// The mesh might not change at all (SetModelFile only rebuilds the entity for a different
		// file), so drop whatever the previously displayed unit attached before re-applying.
		ClearItemAttachments();

		const auto unitHandle = ObjectMgr::GetUnitHandleByName(m_unitName);
		if (!unitHandle)
		{
			SetModelFile("");
			return;
		}

		GameUnitC* unit = unitHandle->Get();
		if (!unit)
		{
			SetModelFile("");
			return;
		}

		const proto_client::ModelDataEntry* model = unit->GetDisplayModel();
		const bool customizable = !model || (model->flags() & model_data_flags::IsCustomizable) != 0;

		std::shared_ptr<CustomizableAvatarDefinition> definition;
		if (customizable)
		{
			definition = unit->GetAvatarDefinition();
			if (!definition)
			{
				SetModelFile("");
				return;
			}

			SetModelFile(definition->GetBaseMesh());
		}
		else
		{
			SetModelFile(model->filename());
		}

		// Start from the mesh's default sub entity setup: a previously applied item display may
		// have hidden geometry or overridden materials which no customization group owns. Note
		// that the entity survives a refresh whenever the mesh itself did not change.
		if (m_entity)
		{
			m_entity->ResetSubEntities();
		}

		if (definition)
		{
			unit->GetAvatarConfiguration().Apply(*this, *definition);
		}

		ApplyEquipment(*unit);

		InvalidateModel();
	}

	void UnitModelFrame::ApplyEquipment(GameUnitC& unit)
	{
		if (!m_entity)
		{
			return;
		}

		if (!unit.IsPlayer())
		{
			// NPCs wear the outfit configured on their model, mirroring GameUnitC::ApplyModelItemDisplays.
			const proto_client::ModelDataEntry* model = unit.GetDisplayModel();
			if (!model)
			{
				return;
			}

			const uint32 modelDisplayId = unit.Get<uint32>(object_fields::DisplayId);
			for (const uint32 itemDisplayId : model->item_displays())
			{
				if (const proto_client::ItemDisplayEntry* display = unit.GetProject().itemDisplays.getById(itemDisplayId))
				{
					ApplyItemDisplay(m_scene, *m_entity, modelDisplayId, itemDisplayId, *display, unit.IsWeaponDrawn(), m_itemAttachments);
				}
			}
			return;
		}

		GamePlayerC& player = unit.AsPlayer();

		const uint32 modelDisplayId = player.Get<uint32>(object_fields::DisplayId);
		const bool weaponsDrawn = player.IsWeaponDrawn();

		for (const uint32 itemDisplayId : player.GetEquipmentDisplayIds())
		{
			if (itemDisplayId == 0)
			{
				continue;
			}

			const proto_client::ItemDisplayEntry* display = player.GetProject().itemDisplays.getById(itemDisplayId);
			if (!display)
			{
				continue;
			}

			ApplyItemDisplay(m_scene, *m_entity, modelDisplayId, itemDisplayId, *display, weaponsDrawn, m_itemAttachments);
		}
	}

	void UnitModelFrame::ClearItemAttachments()
	{
		if (!m_entity)
		{
			m_itemAttachments.clear();
			return;
		}

		ClearItemDisplayAttachments(m_scene, *m_entity, m_itemAttachments);
	}

	void UnitModelFrame::OnModelFileChanged(const Property& prop)
	{
		// The base implementation destroys the current entity, which owns the bones our item
		// meshes are attached to.
		ClearItemAttachments();

		ModelFrame::OnModelFileChanged(prop);
		m_portraitBoundsValid = false;
	}

	void UnitModelFrame::Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration)
	{
		// First, hide all sub entities with the given visibility set tag
		if (!group.subEntityTag.empty())
		{
			for (uint16 i = 0; i < m_entity->GetNumSubEntities(); ++i)
			{
				ASSERT(m_entity->GetMesh()->GetSubMeshCount() == m_entity->GetNumSubEntities());

				SubMesh& subMesh = m_entity->GetMesh()->GetSubMesh(i);
				if (subMesh.HasTag(group.subEntityTag))
				{
					SubEntity* subEntity = m_entity->GetSubEntity(i);
					ASSERT(subEntity);
					subEntity->SetVisible(false);
				}
			}

		}

		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto& value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto& subEntityName : value.visibleSubEntities)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(subEntityName))
					{
						subEntity->SetVisible(true);
					}
				}
			}
		}
	}

	void UnitModelFrame::Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration)
	{
		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto& value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto& pair : value.subEntityToMaterial)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(pair.first))
					{
						if (MaterialPtr material = MaterialManager::Get().Load(pair.second))
						{
							subEntity->SetMaterial(material);
						}
					}
				}
			}
		}
	}

	void UnitModelFrame::Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration)
	{

	}
}
