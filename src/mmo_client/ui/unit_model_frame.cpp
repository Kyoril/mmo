// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_model_frame.h"

#include "client_data/project.h"
#include "game_client/game_player_c.h"
#include "game_client/object_mgr.h"
#include "game_client/unit_handle.h"
#include "scene_graph/material_manager.h"
#include "shared/client_data/proto_client/item_display.pb.h"
#include "shared/client_data/proto_client/model_data.pb.h"

namespace mmo
{
	UnitModelFrame::UnitModelFrame(const std::string& name)
		: ModelFrame(name)
	{
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
		if (!m_entity || !unit.IsPlayer())
		{
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
