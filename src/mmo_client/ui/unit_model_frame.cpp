
#include "unit_model_frame.h"

#include "game_client/object_mgr.h"
#include "game_client/unit_handle.h"
#include "scene_graph/material_manager.h"
#include "shared/client_data/proto_client/model_data.pb.h"

namespace mmo
{
	UnitModelFrame::UnitModelFrame(const std::string& name)
		: ModelFrame(name)
	{
	}

	void UnitModelFrame::SetUnit(const std::string& unitName)
	{
		const auto unitHandle  = ObjectMgr::GetUnitHandleByName(unitName);
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
		if (model && (model->flags() & model_data_flags::IsCustomizable) == 0)
		{
			SetModelFile(model->filename());
		}
		else
		{
			const std::shared_ptr<CustomizableAvatarDefinition>& definition = unit->GetAvatarDefinition();
			if (!definition)
			{
				SetModelFile("");
				return;
			}

			SetModelFile(definition->GetBaseMesh());
			unit->GetAvatarConfiguration().Apply(*this, *definition);
		}
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
