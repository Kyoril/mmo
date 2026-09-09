// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "char_create_info.h"
#include "client_data/project.h"
#include "scene_graph/material_manager.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "luabind/function.hpp"
#include "luabind/scope.hpp"
#include "net/realm_connector.h"
#include "ui/model_frame.h"

#include "luabind_lambda.h"

#include "client_data/character_outfit.h"
#include "shared/client_data/proto_client/classes.pb.h"
#include "shared/client_data/proto_client/item_display.pb.h"
#include "scene_graph/entity.h"

namespace mmo
{
	CharCreateInfo::CharCreateInfo(const proto_client::Project& project, RealmConnector& realmConnector)
		: m_project(project)
		, m_realmConnector(realmConnector)
	{
	}

	void CharCreateInfo::RegisterScriptFunctions(lua_State* lua)
	{
		LUABIND_MODULE(lua,
			luabind::def_lambda("CreateCharacter", [this](const String& name) { CreateCharacter(name); }),
			luabind::def_lambda("SetCharCustomizeFrame", [this](Frame* frame) { SetCharacterCreationFrame(frame); }),
			luabind::def_lambda("SetCharacterClass", [this](int32 classId) { SetSelectedClass(classId); }),
			luabind::def_lambda("SetCharacterGender", [this](int32 genderId) { SetSelectedGender(genderId); }),
			luabind::def_lambda("SetCharacterRace", [this](int32 raceId) { SetSelectedRace(raceId); }),
			luabind::def_lambda("GetCharacterRace", [this]() { return GetSelectedRace(); }),
			luabind::def_lambda("GetCharacterGender", [this]() { return GetSelectedGender(); }),
			luabind::def_lambda("GetCharacterClass", [this]() { return GetSelectedClass(); }),
			luabind::def_lambda("SetCharCreateOutfitVisible", [this](bool visible) { SetOutfitVisible(visible); }),
			luabind::def_lambda("IsCharCreateOutfitVisible", [this]() { return IsOutfitVisible(); }),
			luabind::def_lambda("ResetCharCustomize", [this]() { ResetCharacterCreation(); }),
			luabind::def_lambda("GetCustomizationValue", [this](const String& name) { return GetCustomizationValue(name); }),
			luabind::def_lambda("CycleCustomizationProperty", [this](const String& property, bool forward) { CycleCustomizationProperty(property, forward, true); }),
			luabind::def_lambda("GetNumCustomizationProperties", [this]() { return static_cast<int32>(GetPropertyNames().size()); }),
			luabind::def_lambda("GetCustomizationProperty", [this](int32 index) -> const char*
				{
					const auto& names = GetPropertyNames();
					if (index < 0 || index >= names.size())
					{
						return nullptr;
					}

					return names[index].c_str();
				}),
			luabind::def_lambda("IsRaceAvailable", [this](int32 raceId) { return IsRaceAvailable(raceId); }),
			luabind::def_lambda("IsClassAvailable", [this](int32 classId) { return IsClassAvailable(classId); }),
			luabind::def_lambda("IsCharacterCreationAvailable", [this]() { return IsCharacterCreationAvailable(); }),
			luabind::def_lambda("GetNumRaces", [this]() { return GetNumRaces(); }),
			luabind::def_lambda("GetRaceId", [this](int32 index) { return GetRaceId(index); }),
			luabind::def_lambda("GetRaceName", [this](int32 index) -> const char* { return GetRaceName(index); }),
			luabind::def_lambda("GetNumClasses", [this]() { return GetNumClasses(); }),
			luabind::def_lambda("GetClassId", [this](int32 index) { return GetClassId(index); }),
			luabind::def_lambda("GetClassName", [this](int32 index) -> const char* { return GetClassEntryName(index); }),
			luabind::def_lambda("GetClassNameById", [this](int32 classId) -> const char* { return GetClassNameById(classId); }),
			luabind::def_lambda("GetRaceNameById", [this](int32 raceId) -> const char* { return GetRaceNameById(raceId); })
		);
	}

	void CharCreateInfo::ResetCharacterCreation()
	{
		// Randomize gender, class and race
		std::uniform_int_distribution<int32> genderGenerator(0, 1);
		SetSelectedGender(genderGenerator(randomGenerator));

		std::uniform_int_distribution<int32> raceGenerator(0, static_cast<int32>(m_project.races.count()) - 1);
		SetSelectedRace(raceGenerator(randomGenerator));

		// TODO: Get class count
		std::uniform_int_distribution<int32> classGenerator(0, 3);
		SetSelectedClass(classGenerator(randomGenerator));

		m_modelChanged = true;
		RefreshModel();
	}

	void CharCreateInfo::SetCharacterCreationFrame(Frame* frame)
	{
		m_frameConnection.disconnect();

		// Entity pointers in m_itemAttachments belong to the previous frame's scene and are now
		// dangling. Drop them before adopting the new frame.
		m_itemAttachments.clear();

		const auto modelFrame = dynamic_cast<ModelFrame*>(frame);
		m_characterCreationFrame = modelFrame;

		// The frame's configured animation is the idle pose to fall back to whenever no outfit
		// names a stance of its own.
		m_defaultAnimation = modelFrame ? modelFrame->GetAnimation() : String();

		m_modelChanged = true;
	}

	void CharCreateInfo::SetSelectedRace(int32 raceId)
	{
		const proto_client::RaceEntry* race = m_project.races.getById(raceId);
		if (race == nullptr)
		{
			ELOG("Unknown race selected!");
			return;
		}

		if (m_selectedRace == raceId)
		{
			return;
		}

		m_selectedRace = raceId;
		m_modelChanged = true;
		RefreshModel();
	}

	void CharCreateInfo::SetSelectedClass(int32 classId)
	{
		// TODO: Validate class id
		if (classId < 0)
		{
			ELOG("Unknown class selected!");
			return;
		}

		if (m_selectedClass == classId)
		{
			return;
		}

		m_selectedClass = classId;

		// The model itself does not change, but the outfit does, so the preview has to be
		// rebuilt. m_modelChanged stays false so the player's customization choices survive.
		RefreshModel();
	}

	void CharCreateInfo::SetSelectedGender(int32 gender)
	{
		if (gender < 0 || gender > 1)
		{
			ELOG("Invalid gender id selected: Accepted values are 0..1.");
			return;
		}

		if (m_selectedGender == gender)
		{
			return;
		}

		m_selectedGender = gender;
		m_modelChanged = true;
		RefreshModel();
	}

	void CharCreateInfo::SetOutfitVisible(const bool visible)
	{
		if (m_outfitVisible == visible)
		{
			return;
		}

		m_outfitVisible = visible;

		// Item displays hide body parts by sub entity name and by tag, and nothing restores those
		// on its own, so the preview model has to be rebuilt. m_modelChanged stays false, which
		// keeps the player's customization choices.
		RefreshModel();
	}

	int32 CharCreateInfo::GetSelectedRace() const
	{
		return m_selectedRace;
	}

	int32 CharCreateInfo::GetSelectedClass() const
	{
		return m_selectedClass;
	}

	int32 CharCreateInfo::GetSelectedGender() const
	{
		return m_selectedGender;
	}

	void CharCreateInfo::CycleCustomizationProperty(const String& propertyName, bool forward, bool apply)
	{
		if (!m_avatarDefinition)
		{
			return;
		}

		const CustomizationPropertyGroup* propertyDefinition = m_avatarDefinition->GetProperty(propertyName);
		if (!propertyDefinition)
		{
			ELOG("Property named " << propertyName << " not found in avatar definition!");
			return;
		}

		switch (propertyDefinition->GetType())
		{
			case CharacterCustomizationPropertyType::VisibilitySet:
			{
				const auto visibilityProperty = dynamic_cast<const VisibilitySetPropertyGroup*>(propertyDefinition);
				if (!visibilityProperty)
				{
					return;
				}

				// Do we even have multiple values (or values at all?)
				const auto& possibleValues = visibilityProperty->possibleValues;
				if (possibleValues.size() <= 1)
				{
					return;
				}

				auto& currentOption = m_configuration.chosenOptionPerGroup[propertyDefinition->GetId()];
				int32 index = visibilityProperty->GetPropertyValueIndex(currentOption);
				if (index == -1 && !forward)
				{
					index = static_cast<int32>(possibleValues.size()) - 1;
				}

				index = (index + (forward ? 1 : possibleValues.size() - 1)) % possibleValues.size();
				ASSERT(index >= 0 && index < possibleValues.size());

				currentOption = possibleValues[index].valueId;
				break;
			}

			case CharacterCustomizationPropertyType::MaterialOverride:
			{
				const auto materialProperty = dynamic_cast<const MaterialOverridePropertyGroup*>(propertyDefinition);
				if (!materialProperty)
				{
					return;
				}

				// Do we even have multiple values (or values at all?)
				const auto& possibleValues = materialProperty->possibleValues;
				if (possibleValues.size() <= 1)
				{
					return;
				}

				auto& currentOption = m_configuration.chosenOptionPerGroup[propertyDefinition->GetId()];
				int32 index = materialProperty->GetPropertyValueIndex(currentOption);
				if (index == -1 && !forward)
				{
					index = static_cast<int32>(possibleValues.size()) - 1;
				}

				index = (index + (forward ? 1 : possibleValues.size() - 1)) % possibleValues.size();
				ASSERT(index >= 0 && index < possibleValues.size());

				currentOption = possibleValues[index].valueId;
				break;
			}
		}

		if (apply)
		{
			ApplyCustomizations();
		}
	}

	void CharCreateInfo::CreateCharacter(const String& name) const
	{
		m_realmConnector.CreateCharacter(name,
			static_cast<uint8>(m_selectedRace),
			static_cast<uint8>(m_selectedClass),
			static_cast<uint8>(m_selectedGender),
			m_configuration);
	}

	const char* CharCreateInfo::GetCustomizationValue(const String& propertyName) const
	{
		if (!m_avatarDefinition)
		{
			return nullptr;
		}

		const CustomizationPropertyGroup* propertyDefinition = m_avatarDefinition->GetProperty(propertyName);
		if (!propertyDefinition)
		{
			ELOG("Property named " << propertyName << " not found in avatar definition!");
			return nullptr;
		}

		switch (propertyDefinition->GetType())
		{
		case CharacterCustomizationPropertyType::VisibilitySet:
		{
			const auto visibilityProperty = dynamic_cast<const VisibilitySetPropertyGroup*>(propertyDefinition);
			if (!visibilityProperty)
			{
				return nullptr;
			}

			// Do we even have multiple values (or values at all?)
			const auto& possibleValues = visibilityProperty->possibleValues;
			if (possibleValues.empty())
			{
				return nullptr;
			}

            const auto it = m_configuration.chosenOptionPerGroup.find(propertyDefinition->GetId());
            if (it != m_configuration.chosenOptionPerGroup.end())
			{
                const uint32 currentOption = it->second;
				const int32 index = visibilityProperty->GetPropertyValueIndex(currentOption);
				return possibleValues[index].valueName.c_str();
            }
		}
		case CharacterCustomizationPropertyType::MaterialOverride:
		{
			const auto materialProperty = dynamic_cast<const MaterialOverridePropertyGroup*>(propertyDefinition);
			if (!materialProperty)
			{
				return nullptr;
			}

			// Do we even have multiple values (or values at all?)
			const auto& possibleValues = materialProperty->possibleValues;
			if (possibleValues.size() <= 1)
			{
				return nullptr;
			}

			const auto it = m_configuration.chosenOptionPerGroup.find(propertyDefinition->GetId());
			if (it != m_configuration.chosenOptionPerGroup.end())
			{
				const uint32 currentOption = it->second;
				const int32 index = materialProperty->GetPropertyValueIndex(currentOption);
				return possibleValues[index].valueName.c_str();
			}
		}
		}

		return nullptr;
	}

	void CharCreateInfo::RefreshModel()
	{
		m_propertyNameCache.clear();

		if (!m_characterCreationFrame)
		{
			return;
		}

		// Ensure definition is loaded
		const proto_client::RaceEntry* race = m_project.races.getById(m_selectedRace);
		if (!race)
		{
			return;
		}

		const proto_client::ModelDataEntry* model = m_project.models.getById(m_selectedGender == 0 ? race->malemodel() : race->femalemodel());
		if (!model)
		{
			ELOG("No model id set for race " << race->name() << "!");
			return;
		}

		// Only a model change invalidates the avatar configuration. Switching class rebuilds the
		// entity as well, but has to keep the player's customization choices.
		const bool resetCustomization = m_modelChanged;

		// Entity pointers in m_itemAttachments refer to the entity we are about to destroy.
		ClearItemAttachments();

		// Force entity recreation so sub entity visibility and material overrides are reset to
		// their defaults. Without this, body parts hidden by a previous outfit's item displays
		// would stay hidden.
		m_characterCreationFrame->SetModelFile("");

		// The model frame binds its animation state while the model file is applied, so the
		// stance has to be chosen before the entity is created.
		const proto_client::CharacterOutfit* outfit = FindOutfit();
		m_characterCreationFrame->SetAnimation(outfit && !outfit->animation().empty() ? outfit->animation() : m_defaultAnimation);

		m_selectedModel = model;
		if ((m_selectedModel->flags() & model_data_flags::IsCustomizable) == 0)
		{
			// Simple model, no customizations
			m_avatarDefinition.reset();
			m_characterCreationFrame->SetModelFile(m_selectedModel->filename());
		}
		else
		{
			if (resetCustomization)
			{
				// Reset avatar configuration
				m_configuration.chosenOptionPerGroup.clear();
				m_configuration.scalarValues.clear();
			}

			// Load avatar definition
			m_avatarDefinition = AvatarDefinitionManager::Get().Load(model->filename());
			if (m_avatarDefinition)
			{
				m_characterCreationFrame->SetModelFile(m_avatarDefinition->GetBaseMesh());

				for (const auto& property : *m_avatarDefinition)
				{
					m_propertyNameCache.push_back(property->GetName());

					if (resetCustomization)
					{
						CycleCustomizationProperty(property->GetName(), true, false);
					}
				}
			}
		}

		m_modelChanged = false;
		ApplyCustomizations();
	}

	void CharCreateInfo::ApplyCustomizations()
	{
		if (m_avatarDefinition)
		{
			for (const auto& property : *m_avatarDefinition)
			{
				property->Apply(*this, m_configuration);
			}
		}

		// Visibility set properties rewrite the visibility of every sub entity carrying their
		// tag, which would undo the body parts the outfit's item displays hide. Re-applying the
		// outfit afterwards is what keeps armor covering the body.
		ApplyOutfit();
	}

	const proto_client::CharacterOutfit* CharCreateInfo::FindOutfit() const
	{
		if (!m_outfitVisible)
		{
			return nullptr;
		}

		const proto_client::ClassEntry* classEntry = m_project.classes.getById(m_selectedClass);
		if (!classEntry)
		{
			return nullptr;
		}

		return proto_client::SelectCharacterOutfit(*classEntry, m_selectedRace, m_selectedGender);
	}

	void CharCreateInfo::ApplyOutfit()
	{
		if (!m_characterCreationFrame || !m_selectedModel)
		{
			return;
		}

		Entity* entity = m_characterCreationFrame->GetEntity();
		if (!entity)
		{
			return;
		}

		const proto_client::CharacterOutfit* outfit = FindOutfit();
		if (!outfit)
		{
			return;
		}

		for (const uint32 displayId : outfit->item_displays())
		{
			if (displayId == 0)
			{
				continue;
			}

			const proto_client::ItemDisplayEntry* displayData = m_project.itemDisplays.getById(displayId);
			if (!displayData)
			{
				WLOG("Character creation outfit references unknown item display " << displayId << "!");
				continue;
			}

			// The creation preview is a hero shot, so weapons go into the hands rather than into
			// their sheath.
			ApplyItemDisplay(m_characterCreationFrame->GetScene(), *entity, m_selectedModel->id(), displayId, *displayData, true, m_itemAttachments);
		}
	}

	void CharCreateInfo::ClearItemAttachments()
	{
		Entity* entity = m_characterCreationFrame ? m_characterCreationFrame->GetEntity() : nullptr;
		if (!entity)
		{
			m_itemAttachments.clear();
			return;
		}

		ClearItemDisplayAttachments(m_characterCreationFrame->GetScene(), *entity, m_itemAttachments);
	}

	void CharCreateInfo::Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration)
	{
		if (!m_characterCreationFrame)
		{
			return;
		}

		Entity* entity = m_characterCreationFrame->GetEntity();
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

	void CharCreateInfo::Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration)
	{
		if (!m_characterCreationFrame)
		{
			return;
		}

		Entity* entity = m_characterCreationFrame->GetEntity();
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

	void CharCreateInfo::Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration)
	{

	}

	bool CharCreateInfo::IsRaceAvailable(int32 raceId) const
	{
		return !m_realmConnector.IsRaceDisabled(static_cast<uint32>(raceId));
	}

	bool CharCreateInfo::IsClassAvailable(int32 classId) const
	{
		return !m_realmConnector.IsClassDisabled(static_cast<uint32>(classId));
	}

	bool CharCreateInfo::IsCharacterCreationAvailable() const
	{
		bool hasRace = false;
		for (const auto& race : m_project.races.getTemplates().entry())
		{
			if (IsRaceAvailable(static_cast<int32>(race.id())))
			{
				hasRace = true;
				break;
			}
		}

		if (!hasRace)
		{
			return false;
		}

		for (const auto& cls : m_project.classes.getTemplates().entry())
		{
			if (IsClassAvailable(static_cast<int32>(cls.id())))
			{
				return true;
			}
		}

		return false;
	}

	int32 CharCreateInfo::GetNumRaces() const
	{
		return static_cast<int32>(m_project.races.count());
	}

	int32 CharCreateInfo::GetRaceId(int32 index) const
	{
		if (index < 0 || index >= GetNumRaces())
		{
			return -1;
		}

		return static_cast<int32>(m_project.races.getTemplates().entry(index).id());
	}

	const char* CharCreateInfo::GetRaceName(int32 index) const
	{
		if (index < 0 || index >= GetNumRaces())
		{
			return nullptr;
		}

		return m_project.races.getTemplates().entry(index).name().c_str();
	}

	int32 CharCreateInfo::GetNumClasses() const
	{
		return static_cast<int32>(m_project.classes.count());
	}

	int32 CharCreateInfo::GetClassId(int32 index) const
	{
		if (index < 0 || index >= GetNumClasses())
		{
			return -1;
		}

		return static_cast<int32>(m_project.classes.getTemplates().entry(index).id());
	}

	const char* CharCreateInfo::GetClassEntryName(int32 index) const
	{
		if (index < 0 || index >= GetNumClasses())
		{
			return nullptr;
		}

		return m_project.classes.getTemplates().entry(index).name().c_str();
	}

	const char* CharCreateInfo::GetClassNameById(int32 classId) const
	{
		const auto* entry = m_project.classes.getById(static_cast<uint32>(classId));
		if (!entry)
		{
			return nullptr;
		}

		return entry->name().c_str();
	}

	const char* CharCreateInfo::GetRaceNameById(int32 raceId) const
	{
		const auto* entry = m_project.races.getById(static_cast<uint32>(raceId));
		if (!entry)
		{
			return nullptr;
		}

		return entry->name().c_str();
	}
}
