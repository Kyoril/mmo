// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "char_create_info.h"
#include "client_data/project.h"
#include "scene_graph/material_manager.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "net/realm_connector.h"
#include "ui/model_frame.h"

namespace mmo
{
	CharCreateInfo::CharCreateInfo(const proto_client::Project& project, RealmConnector& realmConnector)
		: m_project(project)
		, m_realmConnector(realmConnector)
	{
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

		const auto modelFrame = dynamic_cast<ModelFrame*>(frame);
		m_characterCreationFrame = modelFrame;
				
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

		if (!m_modelChanged || !m_characterCreationFrame)
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

		m_selectedModel = model;
		if ((m_selectedModel->flags() & model_data_flags::IsCustomizable) == 0)
		{
			// Simple model, no customizations
			m_characterCreationFrame->SetModelFile(m_selectedModel->filename());
		}
		else
		{
			// Reset avatar configuration
			m_configuration.chosenOptionPerGroup.clear();
			m_configuration.scalarValues.clear();

			// Load avatar definition
			m_avatarDefinition = AvatarDefinitionManager::Get().Load(model->filename());
			if (m_avatarDefinition)
			{
				m_characterCreationFrame->SetModelFile(m_avatarDefinition->GetBaseMesh());

				for (const auto& property : *m_avatarDefinition)
				{
					m_propertyNameCache.push_back(property->GetName());
					CycleCustomizationProperty(property->GetName(), true, false);
				}
			}
		}

		m_modelChanged = false;
		ApplyCustomizations();
	}

	void CharCreateInfo::ApplyCustomizations()
	{
		if (!m_avatarDefinition)
		{
			return;
		}

		for (const auto& property : *m_avatarDefinition)
		{
			property->Apply(*this, m_configuration);
		}
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
}
