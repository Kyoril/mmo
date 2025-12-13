// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_tools.h"

#include "game/spell.h"
#include "log/default_log_levels.h"

#include <algorithm>

namespace mmo
{
	SpellTools::SpellTools(proto::Project &project)
		: m_project(project)
	{
	}

	nlohmann::json SpellTools::ListSpells(const nlohmann::json &args)
	{
		nlohmann::json result = nlohmann::json::array();

		// Optional filters
		uint32 minLevel = 0;
		uint32 maxLevel = 1000;
		int32 spellSchool = -1;
		int32 powerType = -1;
		uint32 limit = 100;
		uint32 offset = 0;

		if (args.contains("minLevel"))
		{
			minLevel = args["minLevel"];
		}
		if (args.contains("maxLevel"))
		{
			maxLevel = args["maxLevel"];
		}
		if (args.contains("spellSchool"))
		{
			spellSchool = args["spellSchool"];
		}
		if (args.contains("powerType"))
		{
			powerType = args["powerType"];
		}
		if (args.contains("limit"))
		{
			limit = args["limit"];
		}
		if (args.contains("offset"))
		{
			offset = args["offset"];
		}

		const auto &spells = m_project.spells.getTemplates();
		uint32 count = 0;
		uint32 skipped = 0;

		for (int i = 0; i < spells.entry_size() && count < limit; ++i)
		{
			const auto &spell = spells.entry(i);

			// Apply filters
			if (spell.has_spelllevel())
			{
				if (static_cast<uint32>(spell.spelllevel()) < minLevel || static_cast<uint32>(spell.spelllevel()) > maxLevel)
				{
					continue;
				}
			}

			if (spellSchool >= 0 && spell.has_spellschool())
			{
				if (static_cast<int32>(spell.spellschool()) != spellSchool)
				{
					continue;
				}
			}

			if (powerType >= 0 && spell.has_powertype())
			{
				if (spell.powertype() != powerType)
				{
					continue;
				}
			}

			// Apply offset
			if (skipped < offset)
			{
				++skipped;
				continue;
			}

			result.push_back(SpellEntryToJson(spell, false));
			++count;
		}

		return result;
	}

	nlohmann::json SpellTools::GetSpellDetails(const nlohmann::json &args)
	{
		if (!args.contains("id"))
		{
			throw std::runtime_error("Missing required parameter: id");
		}

		const uint32 spellId = args["id"];
		const auto *spell = m_project.spells.getById(spellId);

		if (!spell)
		{
			throw std::runtime_error("Spell not found: " + std::to_string(spellId));
		}

		return SpellEntryToJson(*spell, true);
	}

	nlohmann::json SpellTools::CreateSpell(const nlohmann::json &args)
	{
		if (!args.contains("name"))
		{
			throw std::runtime_error("Missing required parameter: name");
		}

		// Create new spell
		auto *newSpell = m_project.spells.add();
		if (!newSpell)
		{
			throw std::runtime_error("Failed to create new spell");
		}

		// Set basic required fields
		newSpell->set_name(args["name"].get<std::string>());

		// Ensure at least 2 attribute sets (as per editor requirements)
		while (newSpell->attributes_size() < 2)
		{
			newSpell->add_attributes(0);
		}

		// Apply all provided properties
		JsonToSpellEntry(args, *newSpell);

		ILOG("Created new spell: " << newSpell->name() << " (ID: " << newSpell->id() << ")");

		return SpellEntryToJson(*newSpell, true);
	}

	nlohmann::json SpellTools::UpdateSpell(const nlohmann::json &args)
	{
		if (!args.contains("id"))
		{
			throw std::runtime_error("Missing required parameter: id");
		}

		const uint32 spellId = args["id"];
		auto *spell = m_project.spells.getById(spellId);

		if (!spell)
		{
			throw std::runtime_error("Spell not found: " + std::to_string(spellId));
		}

		// Update spell properties
		JsonToSpellEntry(args, *spell);

		ILOG("Updated spell: " << spell->name() << " (ID: " << spell->id() << ")");

		return SpellEntryToJson(*spell, true);
	}

	nlohmann::json SpellTools::DeleteSpell(const nlohmann::json &args)
	{
		if (!args.contains("id"))
		{
			throw std::runtime_error("Missing required parameter: id");
		}

		const uint32 spellId = args["id"];
		const auto *spell = m_project.spells.getById(spellId);

		if (!spell)
		{
			throw std::runtime_error("Spell not found: " + std::to_string(spellId));
		}

		const std::string spellName = spell->name();
		m_project.spells.remove(spellId);

		ILOG("Deleted spell: " << spellName << " (ID: " << spellId << ")");

		nlohmann::json result;
		result["success"] = true;
		result["id"] = spellId;
		result["message"] = "Spell deleted successfully";

		return result;
	}

	nlohmann::json SpellTools::SearchSpells(const nlohmann::json &args)
	{
		nlohmann::json result = nlohmann::json::array();

		std::string searchQuery;
		if (args.contains("query"))
		{
			searchQuery = args["query"].get<std::string>();
			// Convert to lowercase for case-insensitive search
			std::transform(searchQuery.begin(), searchQuery.end(), searchQuery.begin(), ::tolower);
		}

		uint32 limit = 50;
		if (args.contains("limit"))
		{
			limit = args["limit"];
		}

		const auto &spells = m_project.spells.getTemplates();
		uint32 count = 0;

		for (int i = 0; i < spells.entry_size() && count < limit; ++i)
		{
			const auto &spell = spells.entry(i);

			// Search in name
			if (!searchQuery.empty() && spell.has_name())
			{
				std::string spellName = spell.name();
				std::transform(spellName.begin(), spellName.end(), spellName.begin(), ::tolower);

				if (spellName.find(searchQuery) == std::string::npos)
				{
					// Also check description if available
					if (spell.has_description())
					{
						std::string desc = spell.description();
						std::transform(desc.begin(), desc.end(), desc.begin(), ::tolower);
						if (desc.find(searchQuery) == std::string::npos)
						{
							continue;
						}
					}
					else
					{
						continue;
					}
				}
			}

			result.push_back(SpellEntryToJson(spell, false));
			++count;
		}

		return result;
	}

	nlohmann::json SpellTools::SpellEntryToJson(const proto::SpellEntry &entry, bool includeDetails) const
	{
		nlohmann::json json;

		json["id"] = entry.id();
		json["name"] = entry.name();

		if (entry.has_description())
		{
			json["description"] = entry.description();
		}

		if (entry.has_spellschool())
		{
			json["spellSchool"] = entry.spellschool();
			json["spellSchoolName"] = GetSpellSchoolName(entry.spellschool());
		}

		if (entry.has_powertype())
		{
			json["powerType"] = entry.powertype();
			json["powerTypeName"] = GetPowerTypeName(entry.powertype());
		}

		if (entry.has_cost())
		{
			json["cost"] = entry.cost();
		}

		if (entry.has_spelllevel())
		{
			json["spellLevel"] = entry.spelllevel();
		}

		if (entry.has_baselevel())
		{
			json["baseLevel"] = entry.baselevel();
		}

		if (entry.has_rank())
		{
			json["rank"] = entry.rank();
		}

		if (entry.has_baseid())
		{
			json["baseId"] = entry.baseid();
		}

		if (includeDetails)
		{
			// Add detailed information
			if (entry.has_casttime())
			{
				json["castTime"] = entry.casttime();
			}

			if (entry.has_cooldown())
			{
				json["cooldown"] = static_cast<uint64>(entry.cooldown());
			}

			if (entry.has_speed())
			{
				json["speed"] = entry.speed();
			}

			if (entry.has_duration())
			{
				json["duration"] = entry.duration();
			}

			if (entry.has_maxduration())
			{
				json["maxDuration"] = entry.maxduration();
			}

			if (entry.has_maxlevel())
			{
				json["maxLevel"] = entry.maxlevel();
			}

			if (entry.has_maxtargets())
			{
				json["maxTargets"] = entry.maxtargets();
			}

			if (entry.has_mechanic())
			{
				json["mechanic"] = entry.mechanic();
				json["mechanicName"] = GetSpellMechanicName(entry.mechanic());
			}

			if (entry.has_dmgclass())
			{
				json["damageClass"] = entry.dmgclass();
			}

			if (entry.has_rangetype())
			{
				json["rangeType"] = entry.rangetype();
			}

			if (entry.has_facing())
			{
				json["facing"] = entry.facing();
			}

			if (entry.has_interruptflags())
			{
				json["interruptFlags"] = entry.interruptflags();
			}

			if (entry.has_aurainterruptflags())
			{
				json["auraInterruptFlags"] = entry.aurainterruptflags();
			}

			if (entry.has_familyflags())
			{
				json["familyFlags"] = static_cast<uint64>(entry.familyflags());
			}

			if (entry.has_procflags())
			{
				json["procFlags"] = entry.procflags();
			}

			if (entry.has_procchance())
			{
				json["procChance"] = entry.procchance();
			}

			if (entry.has_racemask())
			{
				json["raceMask"] = entry.racemask();
			}

			if (entry.has_classmask())
			{
				json["classMask"] = entry.classmask();
			}

			if (entry.has_auratext())
			{
				json["auraText"] = entry.auratext();
			}

			if (entry.has_icon())
			{
				json["icon"] = entry.icon();
			}

			if (entry.has_threat_multiplier())
			{
				json["threatMultiplier"] = entry.threat_multiplier();
			}

			if (entry.has_visualization_id())
			{
				json["visualizationId"] = entry.visualization_id();
			}

			// Attributes
			if (entry.attributes_size() > 0)
			{
				nlohmann::json attributes = nlohmann::json::array();
				for (int i = 0; i < entry.attributes_size(); ++i)
				{
					attributes.push_back(entry.attributes(i));
				}
				json["attributes"] = attributes;
			}

			// Effects
			if (entry.effects_size() > 0)
			{
				nlohmann::json effects = nlohmann::json::array();
				for (int i = 0; i < entry.effects_size(); ++i)
				{
					const auto &effect = entry.effects(i);
					nlohmann::json effectJson;
					effectJson["index"] = effect.index();
					effectJson["type"] = effect.type();
					effectJson["typeName"] = GetSpellEffectName(effect.type());

					if (effect.has_basepoints())
					{
						effectJson["basePoints"] = effect.basepoints();
					}
					if (effect.has_diesides())
					{
						effectJson["dieSides"] = effect.diesides();
					}
					if (effect.has_basedice())
					{
						effectJson["baseDice"] = effect.basedice();
					}
					if (effect.has_targeta())
					{
						effectJson["targetA"] = effect.targeta();
					}
					if (effect.has_targetb())
					{
						effectJson["targetB"] = effect.targetb();
					}
					if (effect.has_radius())
					{
						effectJson["radius"] = effect.radius();
					}
					if (effect.has_aura())
					{
						effectJson["aura"] = effect.aura();
					}
					if (effect.has_amplitude())
					{
						effectJson["amplitude"] = effect.amplitude();
					}
					if (effect.has_triggerspell())
					{
						effectJson["triggerSpell"] = effect.triggerspell();
					}
					if (effect.has_miscvaluea())
					{
						effectJson["miscValueA"] = effect.miscvaluea();
					}
					if (effect.has_miscvalueb())
					{
						effectJson["miscValueB"] = effect.miscvalueb();
					}

					effects.push_back(effectJson);
				}
				json["effects"] = effects;
			}

			// Reagents
			if (entry.reagents_size() > 0)
			{
				nlohmann::json reagents = nlohmann::json::array();
				for (int i = 0; i < entry.reagents_size(); ++i)
				{
					const auto &reagent = entry.reagents(i);
					nlohmann::json reagentJson;
					reagentJson["item"] = reagent.item();
					if (reagent.has_count())
					{
						reagentJson["count"] = reagent.count();
					}
					reagents.push_back(reagentJson);
				}
				json["reagents"] = reagents;
			}
		}

		return json;
	}

	void SpellTools::JsonToSpellEntry(const nlohmann::json &json, proto::SpellEntry &entry)
	{
		// Update fields from JSON if they exist
		if (json.contains("name"))
		{
			entry.set_name(json["name"].get<std::string>());
		}
		if (json.contains("description"))
		{
			entry.set_description(json["description"].get<std::string>());
		}
		if (json.contains("spellSchool"))
		{
			entry.set_spellschool(json["spellSchool"]);
		}
		if (json.contains("powerType"))
		{
			entry.set_powertype(json["powerType"]);
		}
		if (json.contains("cost"))
		{
			entry.set_cost(json["cost"]);
		}
		if (json.contains("costPct"))
		{
			entry.set_costpct(json["costPct"]);
		}
		if (json.contains("castTime"))
		{
			entry.set_casttime(json["castTime"]);
		}
		if (json.contains("cooldown"))
		{
			entry.set_cooldown(json["cooldown"]);
		}
		if (json.contains("speed"))
		{
			entry.set_speed(json["speed"]);
		}
		if (json.contains("duration"))
		{
			entry.set_duration(json["duration"]);
		}
		if (json.contains("maxDuration"))
		{
			entry.set_maxduration(json["maxDuration"]);
		}
		if (json.contains("baseLevel"))
		{
			entry.set_baselevel(json["baseLevel"]);
		}
		if (json.contains("spellLevel"))
		{
			entry.set_spelllevel(json["spellLevel"]);
		}
		if (json.contains("maxLevel"))
		{
			entry.set_maxlevel(json["maxLevel"]);
		}
		if (json.contains("maxTargets"))
		{
			entry.set_maxtargets(json["maxTargets"]);
		}
		if (json.contains("mechanic"))
		{
			entry.set_mechanic(json["mechanic"]);
		}
		if (json.contains("damageClass"))
		{
			entry.set_dmgclass(json["damageClass"]);
		}
		if (json.contains("rangeType"))
		{
			entry.set_rangetype(json["rangeType"]);
		}
		if (json.contains("facing"))
		{
			entry.set_facing(json["facing"]);
		}
		if (json.contains("interruptFlags"))
		{
			entry.set_interruptflags(json["interruptFlags"]);
		}
		if (json.contains("auraInterruptFlags"))
		{
			entry.set_aurainterruptflags(json["auraInterruptFlags"]);
		}
		if (json.contains("familyFlags"))
		{
			entry.set_familyflags(json["familyFlags"]);
		}
		if (json.contains("procFlags"))
		{
			entry.set_procflags(json["procFlags"]);
		}
		if (json.contains("procChance"))
		{
			entry.set_procchance(json["procChance"]);
		}
		if (json.contains("raceMask"))
		{
			entry.set_racemask(json["raceMask"]);
		}
		if (json.contains("classMask"))
		{
			entry.set_classmask(json["classMask"]);
		}
		if (json.contains("rank"))
		{
			entry.set_rank(json["rank"]);
		}
		if (json.contains("baseId"))
		{
			entry.set_baseid(json["baseId"]);
		}
		if (json.contains("auraText"))
		{
			entry.set_auratext(json["auraText"].get<std::string>());
		}
		if (json.contains("icon"))
		{
			entry.set_icon(json["icon"].get<std::string>());
		}
		if (json.contains("threatMultiplier"))
		{
			entry.set_threat_multiplier(json["threatMultiplier"]);
		}
		if (json.contains("visualizationId"))
		{
			entry.set_visualization_id(json["visualizationId"]);
		}

		// Handle attributes
		if (json.contains("attributes") && json["attributes"].is_array())
		{
			entry.clear_attributes();
			for (const auto &attr : json["attributes"])
			{
				entry.add_attributes(attr);
			}
		}

		// Handle effects
		if (json.contains("effects") && json["effects"].is_array())
		{
			entry.clear_effects();
			for (const auto &effectJson : json["effects"])
			{
				auto *effect = entry.add_effects();
				if (effectJson.contains("index"))
				{
					effect->set_index(effectJson["index"]);
				}
				if (effectJson.contains("type"))
				{
					effect->set_type(effectJson["type"]);
				}
				if (effectJson.contains("basePoints"))
				{
					effect->set_basepoints(effectJson["basePoints"]);
				}
				if (effectJson.contains("dieSides"))
				{
					effect->set_diesides(effectJson["dieSides"]);
				}
				if (effectJson.contains("baseDice"))
				{
					effect->set_basedice(effectJson["baseDice"]);
				}
				if (effectJson.contains("targetA"))
				{
					effect->set_targeta(effectJson["targetA"]);
				}
				if (effectJson.contains("targetB"))
				{
					effect->set_targetb(effectJson["targetB"]);
				}
				if (effectJson.contains("radius"))
				{
					effect->set_radius(effectJson["radius"]);
				}
				if (effectJson.contains("aura"))
				{
					effect->set_aura(effectJson["aura"]);
				}
				if (effectJson.contains("amplitude"))
				{
					effect->set_amplitude(effectJson["amplitude"]);
				}
				if (effectJson.contains("triggerSpell"))
				{
					effect->set_triggerspell(effectJson["triggerSpell"]);
				}
				if (effectJson.contains("miscValueA"))
				{
					effect->set_miscvaluea(effectJson["miscValueA"]);
				}
				if (effectJson.contains("miscValueB"))
				{
					effect->set_miscvalueb(effectJson["miscValueB"]);
				}
			}
		}

		// Handle reagents
		if (json.contains("reagents") && json["reagents"].is_array())
		{
			entry.clear_reagents();
			for (const auto &reagentJson : json["reagents"])
			{
				auto *reagent = entry.add_reagents();
				if (reagentJson.contains("item"))
				{
					reagent->set_item(reagentJson["item"]);
				}
				if (reagentJson.contains("count"))
				{
					reagent->set_count(reagentJson["count"]);
				}
			}
		}
	}

	String SpellTools::GetSpellSchoolName(uint32 spellSchool) const
	{
		static const std::vector<String> schoolNames = {
			"Physical", "Holy", "Fire", "Nature", "Frost", "Shadow", "Arcane"
		};

		if (spellSchool < schoolNames.size())
		{
			return schoolNames[spellSchool];
		}

		return "Unknown";
	}

	String SpellTools::GetPowerTypeName(int32 powerType) const
	{
		static const std::vector<String> powerTypeNames = {
			"Mana", "Rage", "Energy", "Health"
		};

		if (powerType >= 0 && static_cast<uint32>(powerType) < powerTypeNames.size())
		{
			return powerTypeNames[powerType];
		}

		return "Unknown";
	}

	String SpellTools::GetSpellEffectName(uint32 effectType) const
	{
		static const std::vector<String> effectNames = {
			"None", "InstantKill", "SchoolDamage", "Dummy", "PortalTeleport",
			"TeleportUnits", "ApplyAura", "EnvironmentalDamage", "PowerDrain",
			"HealthLeech", "Heal", "Bind", "Portal", "QuestComplete",
			"WeaponDamageNoSchool", "Resurrect", "AddExtraAttacks", "Dodge",
			"Evade", "Parry", "Block", "CreateItem", "Weapon", "Defense",
			"PersistentAreaAura", "Summon", "Leap", "Energize",
			"WeaponPercentDamage", "TriggerMissile", "OpenLock", "LearnSpell",
			"SpellDefense", "Dispel", "Language", "DualWield",
			"TeleportUnitsFaceCaster", "SkillStep", "Spawn", "TradeSkill",
			"Stealth", "Detect", "TameCreature", "SummonPet", "LearnPetSpell",
			"WeaponDamage", "ResetAttributePoints", "HealPct", "Charge",
			"ApplyAreaAura", "InterruptSpellCast", "ResetTalents", "Proficiency"
		};

		if (effectType < effectNames.size())
		{
			return effectNames[effectType];
		}

		return "Unknown";
	}

	String SpellTools::GetSpellMechanicName(uint32 mechanic) const
	{
		static const std::vector<String> mechanicNames = {
			"None", "Charm", "Disorient", "Disarm", "Distract", "Fear",
			"Root", "Silence", "Sleep", "Snare", "Stun", "Freeze",
			"Knockout", "Bleed", "Polymorph", "Banish", "Shield", "Mount", "Daze"
		};

		if (mechanic < mechanicNames.size())
		{
			return mechanicNames[mechanic];
		}

		return "Unknown";
	}
}
