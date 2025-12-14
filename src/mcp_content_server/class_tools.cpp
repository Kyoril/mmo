// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "class_tools.h"

#include "log/default_log_levels.h"

namespace mmo
{
	ClassTools::ClassTools(proto::Project &project)
		: m_project(project)
	{
	}

	nlohmann::json ClassTools::ListClasses(const nlohmann::json &args)
	{
		nlohmann::json result = nlohmann::json::array();

		const uint32 limit = args.contains("limit") ? args["limit"].get<uint32>() : 100;
		const uint32 offset = args.contains("offset") ? args["offset"].get<uint32>() : 0;
		const bool hasPowerTypeFilter = args.contains("powerType");
		const int32 powerTypeFilter = hasPowerTypeFilter ? args["powerType"].get<int32>() : -1;

		uint32 count = 0;
		uint32 index = 0;

		for (int i = 0; i < m_project.classes.getTemplates().entry_size(); ++i)
		{
			const auto &entry = m_project.classes.getTemplates().entry(i);

			if (hasPowerTypeFilter && entry.powertype() != powerTypeFilter)
			{
				continue;
			}

			if (index < offset)
			{
				index++;
				continue;
			}

			if (count >= limit)
			{
				break;
			}

			result.push_back(ClassEntryToJson(entry, false));
			count++;
			index++;
		}

		ILOG("Listed " << count << " classes (offset: " << offset << ", limit: " << limit << ")");
		return result;
	}

	nlohmann::json ClassTools::GetClassDetails(const nlohmann::json &args)
	{
		if (!args.contains("id"))
		{
			throw std::runtime_error("Missing required parameter: id");
		}

		const uint32 id = args["id"].get<uint32>();
		const auto *entry = m_project.classes.getById(id);

		if (!entry)
		{
			throw std::runtime_error("Class not found: " + std::to_string(id));
		}

		ILOG("Retrieved details for class " << id << " (" << entry->name() << ")");
		return ClassEntryToJson(*entry, true);
	}

	nlohmann::json ClassTools::CreateClass(const nlohmann::json &args)
	{
		if (!args.contains("name"))
		{
			throw std::runtime_error("Missing required parameter: name");
		}

		auto *newEntry = m_project.classes.add();
		newEntry->set_name(args["name"].get<String>());
		newEntry->set_internalname(args.contains("internalName") ? args["internalName"].get<String>() : args["name"].get<String>());
		newEntry->set_powertype(args.contains("powerType") ? static_cast<proto::ClassEntry_PowerType>(args["powerType"].get<int32>()) : proto::ClassEntry_PowerType_MANA);
		newEntry->set_spellfamily(args.contains("spellFamily") ? args["spellFamily"].get<uint32>() : 0);
		newEntry->set_flags(args.contains("flags") ? args["flags"].get<uint32>() : 0);
		newEntry->set_attackpowerperlevel(args.contains("attackPowerPerLevel") ? args["attackPowerPerLevel"].get<float>() : 2.0f);
		newEntry->set_attackpoweroffset(args.contains("attackPowerOffset") ? args["attackPowerOffset"].get<float>() : 0.0f);
		newEntry->set_basemanaregenpertick(args.contains("baseManaRegenPerTick") ? args["baseManaRegenPerTick"].get<float>() : 0.0f);
		newEntry->set_spiritpermanaregen(args.contains("spiritPerManaRegen") ? args["spiritPerManaRegen"].get<float>() : 0.0f);
		newEntry->set_healthregenpertick(args.contains("healthRegenPerTick") ? args["healthRegenPerTick"].get<float>() : 0.0f);
		newEntry->set_spiritperhealthregen(args.contains("spiritPerHealthRegen") ? args["spiritPerHealthRegen"].get<float>() : 0.0f);

		// Add default base values for level 1 if provided
		if (args.contains("baseValues") && args["baseValues"].is_array())
		{
			for (const auto &baseValue : args["baseValues"])
			{
				auto *values = newEntry->add_levelbasevalues();
				values->set_health(baseValue.contains("health") ? baseValue["health"].get<uint32>() : 100);
				values->set_mana(baseValue.contains("mana") ? baseValue["mana"].get<uint32>() : 100);
				values->set_stamina(baseValue.contains("stamina") ? baseValue["stamina"].get<uint32>() : 20);
				values->set_strength(baseValue.contains("strength") ? baseValue["strength"].get<uint32>() : 20);
				values->set_agility(baseValue.contains("agility") ? baseValue["agility"].get<uint32>() : 20);
				values->set_intellect(baseValue.contains("intellect") ? baseValue["intellect"].get<uint32>() : 20);
				values->set_spirit(baseValue.contains("spirit") ? baseValue["spirit"].get<uint32>() : 20);
				values->set_attributepoints(baseValue.contains("attributePoints") ? baseValue["attributePoints"].get<uint32>() : 0);
				values->set_talentpoints(baseValue.contains("talentPoints") ? baseValue["talentPoints"].get<uint32>() : 0);
			}
		}
		else
		{
			// Add default level 1 base values
			auto *baseValues = newEntry->add_levelbasevalues();
			baseValues->set_health(100);
			baseValues->set_mana(100);
			baseValues->set_stamina(20);
			baseValues->set_strength(20);
			baseValues->set_agility(20);
			baseValues->set_intellect(20);
			baseValues->set_spirit(20);
			baseValues->set_attributepoints(0);
			baseValues->set_talentpoints(0);
		}

		ILOG("Created new class: " << newEntry->name() << " (ID: " << newEntry->id() << ")");

		nlohmann::json result;
		result["id"] = newEntry->id();
		result["message"] = "Class created successfully";
		return result;
	}

	nlohmann::json ClassTools::UpdateClass(const nlohmann::json &args)
	{
		if (!args.contains("id"))
		{
			throw std::runtime_error("Missing required parameter: id");
		}

		const uint32 id = args["id"].get<uint32>();
		auto *entry = m_project.classes.getById(id);

		if (!entry)
		{
			throw std::runtime_error("Class not found: " + std::to_string(id));
		}

		if (args.contains("name"))
		{
			entry->set_name(args["name"].get<String>());
		}

		if (args.contains("internalName"))
		{
			entry->set_internalname(args["internalName"].get<String>());
		}

		if (args.contains("powerType"))
		{
			entry->set_powertype(static_cast<proto::ClassEntry_PowerType>(args["powerType"].get<int32>()));
		}

		if (args.contains("spellFamily"))
		{
			entry->set_spellfamily(args["spellFamily"].get<uint32>());
		}

		if (args.contains("flags"))
		{
			entry->set_flags(args["flags"].get<uint32>());
		}

		if (args.contains("attackPowerPerLevel"))
		{
			entry->set_attackpowerperlevel(args["attackPowerPerLevel"].get<float>());
		}

		if (args.contains("attackPowerOffset"))
		{
			entry->set_attackpoweroffset(args["attackPowerOffset"].get<float>());
		}

		if (args.contains("baseManaRegenPerTick"))
		{
			entry->set_basemanaregenpertick(args["baseManaRegenPerTick"].get<float>());
		}

		if (args.contains("spiritPerManaRegen"))
		{
			entry->set_spiritpermanaregen(args["spiritPerManaRegen"].get<float>());
		}

		if (args.contains("healthRegenPerTick"))
		{
			entry->set_healthregenpertick(args["healthRegenPerTick"].get<float>());
		}

		if (args.contains("spiritPerHealthRegen"))
		{
			entry->set_spiritperhealthregen(args["spiritPerHealthRegen"].get<float>());
		}

		// Update base values for specific level
		if (args.contains("updateBaseValues"))
		{
			const auto &update = args["updateBaseValues"];
			if (!update.contains("level"))
			{
				throw std::runtime_error("Missing level in updateBaseValues");
			}

			const uint32 level = update["level"].get<uint32>();
			if (level < 1 || level > static_cast<uint32>(entry->levelbasevalues_size()))
			{
				throw std::runtime_error("Invalid level: " + std::to_string(level));
			}

			auto *values = entry->mutable_levelbasevalues(level - 1);

			if (update.contains("health")) values->set_health(update["health"].get<uint32>());
			if (update.contains("mana")) values->set_mana(update["mana"].get<uint32>());
			if (update.contains("stamina")) values->set_stamina(update["stamina"].get<uint32>());
			if (update.contains("strength")) values->set_strength(update["strength"].get<uint32>());
			if (update.contains("agility")) values->set_agility(update["agility"].get<uint32>());
			if (update.contains("intellect")) values->set_intellect(update["intellect"].get<uint32>());
			if (update.contains("spirit")) values->set_spirit(update["spirit"].get<uint32>());
			if (update.contains("attributePoints")) values->set_attributepoints(update["attributePoints"].get<uint32>());
			if (update.contains("talentPoints")) values->set_talentpoints(update["talentPoints"].get<uint32>());
		}

		// Add new level base values
		if (args.contains("addBaseValues"))
		{
			const auto &newValues = args["addBaseValues"];
			auto *values = entry->add_levelbasevalues();

			values->set_health(newValues.contains("health") ? newValues["health"].get<uint32>() : 100);
			values->set_mana(newValues.contains("mana") ? newValues["mana"].get<uint32>() : 100);
			values->set_stamina(newValues.contains("stamina") ? newValues["stamina"].get<uint32>() : 20);
			values->set_strength(newValues.contains("strength") ? newValues["strength"].get<uint32>() : 20);
			values->set_agility(newValues.contains("agility") ? newValues["agility"].get<uint32>() : 20);
			values->set_intellect(newValues.contains("intellect") ? newValues["intellect"].get<uint32>() : 20);
			values->set_spirit(newValues.contains("spirit") ? newValues["spirit"].get<uint32>() : 20);
			values->set_attributepoints(newValues.contains("attributePoints") ? newValues["attributePoints"].get<uint32>() : 0);
			values->set_talentpoints(newValues.contains("talentPoints") ? newValues["talentPoints"].get<uint32>() : 0);
		}

		// Update XP values
		if (args.contains("updateXpToNextLevel"))
		{
			const auto &update = args["updateXpToNextLevel"];
			if (!update.contains("level"))
			{
				throw std::runtime_error("Missing level in updateXpToNextLevel");
			}

			const uint32 level = update["level"].get<uint32>();
			if (level < 1 || level > static_cast<uint32>(entry->xptonextlevel_size()))
			{
				throw std::runtime_error("Invalid level: " + std::to_string(level));
			}

			if (update.contains("xp"))
			{
				entry->set_xptonextlevel(level - 1, update["xp"].get<uint32>());
			}
		}

		// Add XP for new level
		if (args.contains("addXpToNextLevel"))
		{
			entry->add_xptonextlevel(args["addXpToNextLevel"].get<uint32>());
		}

		ILOG("Updated class: " << entry->name() << " (ID: " << id << ")");

		nlohmann::json result;
		result["message"] = "Class updated successfully";
		return result;
	}

	nlohmann::json ClassTools::DeleteClass(const nlohmann::json &args)
	{
		if (!args.contains("id"))
		{
			throw std::runtime_error("Missing required parameter: id");
		}

		const uint32 id = args["id"].get<uint32>();

		const auto *entry = m_project.classes.getById(id);
		if (!entry)
		{
			throw std::runtime_error("Class not found: " + std::to_string(id));
		}

		m_project.classes.remove(id);

		ILOG("Deleted class with ID: " << id);

		nlohmann::json result;
		result["message"] = "Class deleted successfully";
		return result;
	}

	nlohmann::json ClassTools::SearchClasses(const nlohmann::json &args)
	{
		if (!args.contains("query"))
		{
			throw std::runtime_error("Missing required parameter: query");
		}

		const String query = args["query"].get<String>();
		const uint32 limit = args.contains("limit") ? args["limit"].get<uint32>() : 50;

		nlohmann::json result = nlohmann::json::array();
		uint32 count = 0;

		String lowerQuery = query;
		std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

		for (int i = 0; i < m_project.classes.getTemplates().entry_size(); ++i)
		{
			const auto &entry = m_project.classes.getTemplates().entry(i);

			String lowerName = entry.name();
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

			String lowerInternalName = entry.internalname();
			std::transform(lowerInternalName.begin(), lowerInternalName.end(), lowerInternalName.begin(), ::tolower);

			if (lowerName.find(lowerQuery) != String::npos || lowerInternalName.find(lowerQuery) != String::npos)
			{
				result.push_back(ClassEntryToJson(entry, false));
				count++;

				if (count >= limit)
				{
					break;
				}
			}
		}

		ILOG("Found " << count << " classes matching query: " << query);
		return result;
	}

	nlohmann::json ClassTools::AddClassSpell(const nlohmann::json &args)
	{
		if (!args.contains("classId"))
		{
			throw std::runtime_error("Missing required parameter: classId");
		}

		if (!args.contains("spellId"))
		{
			throw std::runtime_error("Missing required parameter: spellId");
		}

		if (!args.contains("level"))
		{
			throw std::runtime_error("Missing required parameter: level");
		}

		const uint32 classId = args["classId"].get<uint32>();
		const uint32 spellId = args["spellId"].get<uint32>();
		const uint32 level = args["level"].get<uint32>();

		auto *entry = m_project.classes.getById(classId);
		if (!entry)
		{
			throw std::runtime_error("Class not found: " + std::to_string(classId));
		}

		// Verify spell exists
		const auto *spell = m_project.spells.getById(spellId);
		if (!spell)
		{
			throw std::runtime_error("Spell not found: " + std::to_string(spellId));
		}

		// Check if spell already exists for this class
		for (int i = 0; i < entry->spells_size(); ++i)
		{
			if (entry->spells(i).spell() == spellId)
			{
				throw std::runtime_error("Spell already exists for this class");
			}
		}

		auto *classSpell = entry->add_spells();
		classSpell->set_spell(spellId);
		classSpell->set_level(level);

		ILOG("Added spell " << spellId << " (" << spell->name() << ") to class " << classId << " (" << entry->name() << ") at level " << level);

		nlohmann::json result;
		result["message"] = "Spell added to class successfully";
		return result;
	}

	nlohmann::json ClassTools::RemoveClassSpell(const nlohmann::json &args)
	{
		if (!args.contains("classId"))
		{
			throw std::runtime_error("Missing required parameter: classId");
		}

		if (!args.contains("spellId"))
		{
			throw std::runtime_error("Missing required parameter: spellId");
		}

		const uint32 classId = args["classId"].get<uint32>();
		const uint32 spellId = args["spellId"].get<uint32>();

		auto *entry = m_project.classes.getById(classId);
		if (!entry)
		{
			throw std::runtime_error("Class not found: " + std::to_string(classId));
		}

		bool found = false;
		for (int i = 0; i < entry->spells_size(); ++i)
		{
			if (entry->spells(i).spell() == spellId)
			{
				entry->mutable_spells()->erase(entry->mutable_spells()->begin() + i);
				found = true;
				break;
			}
		}

		if (!found)
		{
			throw std::runtime_error("Spell not found in class");
		}

		ILOG("Removed spell " << spellId << " from class " << classId << " (" << entry->name() << ")");

		nlohmann::json result;
		result["message"] = "Spell removed from class successfully";
		return result;
	}

	nlohmann::json ClassTools::ClassEntryToJson(const proto::ClassEntry &entry, bool detailed)
	{
		nlohmann::json json;

		json["id"] = entry.id();
		json["name"] = entry.name();
		json["internalName"] = entry.internalname();
		json["powerType"] = entry.powertype();
		json["powerTypeName"] = PowerTypeToString(entry.powertype());
		json["spellFamily"] = entry.spellfamily();

		if (detailed)
		{
			json["flags"] = entry.flags();
			json["attackPowerPerLevel"] = entry.attackpowerperlevel();
			json["attackPowerOffset"] = entry.attackpoweroffset();
			json["baseManaRegenPerTick"] = entry.basemanaregenpertick();
			json["spiritPerManaRegen"] = entry.spiritpermanaregen();
			json["healthRegenPerTick"] = entry.healthregenpertick();
			json["spiritPerHealthRegen"] = entry.spiritperhealthregen();

			// Base values per level
			nlohmann::json baseValues = nlohmann::json::array();
			for (int i = 0; i < entry.levelbasevalues_size(); ++i)
			{
				const auto &values = entry.levelbasevalues(i);
				nlohmann::json value;
				value["level"] = i + 1;
				value["health"] = values.health();
				value["mana"] = values.mana();
				value["stamina"] = values.stamina();
				value["strength"] = values.strength();
				value["agility"] = values.agility();
				value["intellect"] = values.intellect();
				value["spirit"] = values.spirit();
				value["attributePoints"] = values.attributepoints();
				value["talentPoints"] = values.talentpoints();
				baseValues.push_back(value);
			}
			json["baseValues"] = baseValues;

			// XP to next level
			nlohmann::json xpValues = nlohmann::json::array();
			for (int i = 0; i < entry.xptonextlevel_size(); ++i)
			{
				nlohmann::json xp;
				xp["level"] = i + 1;
				xp["xp"] = entry.xptonextlevel(i);
				xpValues.push_back(xp);
			}
			json["xpToNextLevel"] = xpValues;

			// Spells
			nlohmann::json spells = nlohmann::json::array();
			for (int i = 0; i < entry.spells_size(); ++i)
			{
				const auto &spell = entry.spells(i);
				nlohmann::json spellJson;
				spellJson["spellId"] = spell.spell();
				spellJson["level"] = spell.level();

				// Try to get spell name
				const auto *spellEntry = m_project.spells.getById(spell.spell());
				if (spellEntry)
				{
					spellJson["spellName"] = spellEntry->name();
				}

				spells.push_back(spellJson);
			}
			json["spells"] = spells;

			// Attack power stat sources
			nlohmann::json attackPowerSources = nlohmann::json::array();
			for (int i = 0; i < entry.attackpowerstatsources_size(); ++i)
			{
				const auto &source = entry.attackpowerstatsources(i);
				nlohmann::json sourceJson;
				sourceJson["statId"] = source.statid();
				sourceJson["factor"] = source.factor();
				attackPowerSources.push_back(sourceJson);
			}
			json["attackPowerStatSources"] = attackPowerSources;

			// Health stat sources
			nlohmann::json healthSources = nlohmann::json::array();
			for (int i = 0; i < entry.healthstatsources_size(); ++i)
			{
				const auto &source = entry.healthstatsources(i);
				nlohmann::json sourceJson;
				sourceJson["statId"] = source.statid();
				sourceJson["factor"] = source.factor();
				healthSources.push_back(sourceJson);
			}
			json["healthStatSources"] = healthSources;

			// Mana stat sources
			nlohmann::json manaSources = nlohmann::json::array();
			for (int i = 0; i < entry.manastatsources_size(); ++i)
			{
				const auto &source = entry.manastatsources(i);
				nlohmann::json sourceJson;
				sourceJson["statId"] = source.statid();
				sourceJson["factor"] = source.factor();
				manaSources.push_back(sourceJson);
			}
			json["manaStatSources"] = manaSources;

			// Armor stat sources
			nlohmann::json armorSources = nlohmann::json::array();
			for (int i = 0; i < entry.armorstatsources_size(); ++i)
			{
				const auto &source = entry.armorstatsources(i);
				nlohmann::json sourceJson;
				sourceJson["statId"] = source.statid();
				sourceJson["factor"] = source.factor();
				armorSources.push_back(sourceJson);
			}
			json["armorStatSources"] = armorSources;
		}

		return json;
	}

	void ClassTools::JsonToClassEntry(const nlohmann::json &json, proto::ClassEntry &entry)
	{
		// This method is currently not used but could be useful for future bulk updates
		if (json.contains("name"))
		{
			entry.set_name(json["name"].get<String>());
		}

		if (json.contains("internalName"))
		{
			entry.set_internalname(json["internalName"].get<String>());
		}

		if (json.contains("powerType"))
		{
			entry.set_powertype(static_cast<proto::ClassEntry_PowerType>(json["powerType"].get<int32>()));
		}
	}

	String ClassTools::PowerTypeToString(proto::ClassEntry_PowerType powerType)
	{
		switch (powerType)
		{
		case proto::ClassEntry_PowerType_MANA:
			return "Mana";
		case proto::ClassEntry_PowerType_RAGE:
			return "Rage";
		case proto::ClassEntry_PowerType_ENERGY:
			return "Energy";
		default:
			return "Unknown";
		}
	}

	proto::ClassEntry_PowerType ClassTools::StringToPowerType(const String &str)
	{
		if (str == "Mana")
		{
			return proto::ClassEntry_PowerType_MANA;
		}
		if (str == "Rage")
		{
			return proto::ClassEntry_PowerType_RAGE;
		}
		if (str == "Energy")
		{
			return proto::ClassEntry_PowerType_ENERGY;
		}

		return proto::ClassEntry_PowerType_MANA;
	}
}
