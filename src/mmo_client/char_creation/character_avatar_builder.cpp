// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "character_avatar_builder.h"

#include "client_data/project.h"
#include "game/character_view.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	CharacterAvatarBuilder::CharacterAvatarBuilder(const proto_client::Project& project)
		: m_project(project)
	{
	}

	Entity* CharacterAvatarBuilder::Build(Scene& scene, SceneNode& node, const CharacterView& view, const String& entityName, ItemDisplayAttachmentMap& attachments)
	{
		const proto_client::ModelDataEntry* model = m_project.models.getById(view.GetDisplayId());
		if (!model)
		{
			return nullptr;
		}

		if ((model->flags() & model_data_flags::IsCustomizable) == 0)
		{
			// Simple model, no customizations
			const MeshPtr mesh = MeshManager::Get().Load(model->filename());
			if (!mesh)
			{
				return nullptr;
			}

			Entity* entity = scene.CreateEntity(entityName, mesh);
			node.AttachObject(*entity);
			return entity;
		}

		// Load avatar definition
		const auto definition = AvatarDefinitionManager::Get().Load(model->filename());
		if (!definition)
		{
			return nullptr;
		}

		const MeshPtr mesh = MeshManager::Get().Load(definition->GetBaseMesh());
		if (!mesh)
		{
			return nullptr;
		}

		Entity* entity = scene.CreateEntity(entityName, mesh);
		node.AttachObject(*entity);

		m_entity = entity;
		view.GetConfiguration().Apply(*this, *definition);
		m_entity = nullptr;

		// Apply item display IDs for equipped gear
		for (const uint32 displayId : view.GetEquipmentDisplayIds())
		{
			if (displayId == 0)
			{
				continue;
			}

			const auto* displayData = m_project.itemDisplays.getById(displayId);
			if (!displayData)
			{
				continue;
			}

			// Characters are shown out of combat here, so weapons use their sheathed bone.
			ApplyItemDisplay(scene, *entity, view.GetDisplayId(), displayId, *displayData, false, attachments);
		}

		return entity;
	}

	void CharacterAvatarBuilder::Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration)
	{
		Entity* entity = m_entity;
		if (!entity || !entity->GetMesh())
		{
			return;
		}

		// First, hide all sub entities with the given visibility set tag
		if (!group.subEntityTag.empty())
		{
			for (uint16 i = 0; i < entity->GetNumSubEntities(); ++i)
			{
				ASSERT(entity->GetMesh()->GetSubMeshCount() == entity->GetNumSubEntities());

				SubMesh& subMesh = entity->GetMesh()->GetSubMesh(i);
				if (subMesh.HasTag(group.subEntityTag))
				{
					SubEntity* subEntity = entity->GetSubEntity(i);
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
					if (SubEntity* subEntity = entity->GetSubEntity(subEntityName))
					{
						subEntity->SetVisible(true);
					}
				}
			}
		}
	}

	void CharacterAvatarBuilder::Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration)
	{
		Entity* entity = m_entity;
		if (!entity || !entity->GetMesh())
		{
			return;
		}

		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		for (const auto& value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto& pair : value.subEntityToMaterial)
				{
					if (SubEntity* subEntity = entity->GetSubEntity(pair.first))
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

	void CharacterAvatarBuilder::Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration)
	{
	}
}
