
#include "char_select.h"

#include "client_data/project.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "game_states/game_state_mgr.h"
#include "game_states/world_state.h"
#include "net/realm_connector.h"
#include "scene_graph/material_manager.h"
#include "ui/model_frame.h"

namespace mmo
{
	CharSelect::CharSelect(const proto_client::Project& project, RealmConnector& realmConnector)
		: m_project(project)
		, m_realmConnector(realmConnector)
	{
	}

	void CharSelect::SetModelFrame(Frame* frame)
	{
		const auto modelFrame = dynamic_cast<ModelFrame*>(frame);
		m_modelFrame = modelFrame;
	}

	void CharSelect::SelectCharacter(const int32 index)
	{
		m_selectedCharacter = index;

		if (index < 0 || index >= GetNumCharacters())
		{
			return;
		}

		if (!m_modelFrame)
		{
			return;
		}

		const auto& view = m_realmConnector.GetCharacterViews()[index];

		const proto_client::ModelDataEntry* model = m_project.models.getById(view.GetDisplayId());
		if (!model)
		{
			return;
		}

		if ((model->flags() & model_data_flags::IsCustomizable) == 0)
		{
			// Simple model, no customizations
			m_modelFrame->SetModelFile(model->filename());
		}
		else
		{
			// Load avatar definition
			const auto definition = AvatarDefinitionManager::Get().Load(model->filename());
			if (definition)
			{
				m_modelFrame->SetModelFile(definition->GetBaseMesh());
				view.GetConfiguration().Apply(*this, *definition);
			}
		}
	}

	int32 CharSelect::GetNumCharacters() const
	{
		return static_cast<int32>(m_realmConnector.GetCharacterViews().size());
	}

	const mmo::CharacterView* CharSelect::GetCharacterView(const int32 index) const
	{
		if (index < 0 || index >= GetNumCharacters())
		{
			return nullptr;
		}

		return &m_realmConnector.GetCharacterViews()[index];
	}

	void CharSelect::Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration)
	{
		if (!m_modelFrame)
		{
			return;
		}

		Entity* entity = m_modelFrame->GetEntity();
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

	void CharSelect::Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration)
	{
		if (!m_modelFrame)
		{
			return;
		}

		Entity* entity = m_modelFrame->GetEntity();
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

		// Now make each referenced sub entity visible
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

	void CharSelect::Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration)
	{
		
	}
}
