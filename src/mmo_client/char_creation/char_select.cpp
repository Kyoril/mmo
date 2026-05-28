
#include "char_select.h"

#include "client_data/project.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "game_states/game_state_mgr.h"
#include "game_states/world_state.h"
#include "net/realm_connector.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/tag_point.h"
#include "ui/model_frame.h"

#include "luabind_lambda.h"

namespace mmo
{
	CharSelect::CharSelect(const proto_client::Project& project, RealmConnector& realmConnector)
		: m_project(project)
		, m_realmConnector(realmConnector)
	{
	}

	void CharSelect::RegisterScriptFunctions(lua_State* lua)
	{
		LUABIND_MODULE(lua,
			luabind::def_lambda("SetCharSelectModelFrame", [this](Frame* frame) { SetModelFrame(frame); }),
			luabind::def_lambda("GetNumCharacters", [this]() { return GetNumCharacters(); }),
			luabind::def_lambda("GetCharacterInfo", [this](int32 index) { return GetCharacterView(index); }),
			luabind::def_lambda("SelectCharacter", [this](int32 index) { return SelectCharacter(index); }),
			luabind::def_lambda("IsCharacterDisabled", [this](int32 index) { return IsCharacterDisabled(index); })
		);
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

		// Detach and destroy any item entities attached from the previous selection
		ClearItemAttachments();

		// Force entity recreation even when switching between same-race characters,
		// so sub-entity visibility and material overrides are always reset to defaults
		m_modelFrame->SetModelFile("");

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

				// Apply item display IDs for equipped gear
				Entity* entity = m_modelFrame->GetEntity();
				if (entity)
				{
					const auto& equipDisplayIds = view.GetEquipmentDisplayIds();
					for (size_t slot = 0; slot < equipDisplayIds.size(); ++slot)
					{
						const uint32 displayId = equipDisplayIds[slot];
						if (displayId == 0)
						{
							continue;
						}

						const auto* displayData = m_project.itemDisplays.getById(displayId);
						if (!displayData)
						{
							continue;
						}

						for (const auto& variant : displayData->variants())
						{
							if (variant.model() != 0 && variant.model() != view.GetDisplayId())
							{
								continue;
							}

							for (const auto& subEntityName : variant.hidden_by_name())
							{
								if (SubEntity* sub = entity->GetSubEntity(subEntityName))
								{
									sub->SetVisible(false);
								}
							}

							for (const auto& tag : variant.hidden_by_tag())
							{
								for (uint16 j = 0; j < entity->GetNumSubEntities(); ++j)
								{
									SubMesh& subMesh = entity->GetMesh()->GetSubMesh(j);
									if (subMesh.HasTag(tag))
									{
										if (SubEntity* sub = entity->GetSubEntity(j))
										{
											sub->SetVisible(false);
										}
									}
								}
							}

							for (const auto& subEntityName : variant.shown_by_name())
							{
								if (SubEntity* sub = entity->GetSubEntity(subEntityName))
								{
									sub->SetVisible(true);
								}
							}

							for (const auto& tag : variant.shown_by_tag())
							{
								for (uint16 j = 0; j < entity->GetNumSubEntities(); ++j)
								{
									SubMesh& subMesh = entity->GetMesh()->GetSubMesh(j);
									if (subMesh.HasTag(tag))
									{
										if (SubEntity* sub = entity->GetSubEntity(j))
										{
											sub->SetVisible(true);
										}
									}
								}
							}

							for (const auto& [subEntityName, materialName] : variant.material_overrides())
							{
								if (SubEntity* sub = entity->GetSubEntity(subEntityName))
								{
									if (const MaterialPtr mat = MaterialManager::Get().Load(materialName))
									{
										sub->SetMaterial(mat);
									}
								}
							}

							if (!m_itemAttachments.contains(displayId) && variant.has_mesh() && !variant.mesh().empty())
							{
								const auto skeleton = entity->GetSkeleton();
								if (skeleton && skeleton->HasBone(variant.attached_bone_default().bone_name()))
								{
									ItemAttachment attachment;
									attachment.entity = m_modelFrame->GetScene().CreateEntity(
										"Preview_ITEM_" + std::to_string(displayId), variant.mesh());
									attachment.attachment = entity->AttachObjectToBone(
										variant.attached_bone_default().bone_name(), *attachment.entity);
									if (attachment.attachment)
									{
										attachment.attachment->SetPosition(Vector3(
											variant.attached_bone_default().offset_x(),
											variant.attached_bone_default().offset_y(),
											variant.attached_bone_default().offset_z()));
										attachment.attachment->SetOrientation(Quaternion(
											variant.attached_bone_default().rotation_w(),
											variant.attached_bone_default().rotation_x(),
											variant.attached_bone_default().rotation_y(),
											variant.attached_bone_default().rotation_z()));
										attachment.attachment->SetScale(Vector3(
											variant.attached_bone_default().scale_x(),
											variant.attached_bone_default().scale_y(),
											variant.attached_bone_default().scale_z()));
									}
									m_itemAttachments[displayId] = attachment;
								}
							}
						}
					}
				}
			}
		}
	}

	void CharSelect::ClearItemAttachments()
	{
		if (m_itemAttachments.empty())
		{
			return;
		}

		Entity* entity = m_modelFrame ? m_modelFrame->GetEntity() : nullptr;

		for (auto& [displayId, attachment] : m_itemAttachments)
		{
			if (entity && attachment.entity)
			{
				entity->DetachObjectFromBone(*attachment.entity);
			}
			if (attachment.entity)
			{
				m_modelFrame->GetScene().DestroyEntity(*attachment.entity);
				attachment.entity = nullptr;
			}
		}

		m_itemAttachments.clear();
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

	bool CharSelect::IsCharacterDisabled(int32 index) const
	{
		if (index < 0 || index >= GetNumCharacters())
		{
			return false;
		}

		const auto& view = m_realmConnector.GetCharacterViews()[index];
		return m_realmConnector.IsRaceDisabled(view.GetRaceId()) ||
			   m_realmConnector.IsClassDisabled(view.GetClassId());
	}
}
