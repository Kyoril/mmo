#pragma once

#include "project_loader.h"
#include "project_saver.h"
#include "proto_template.h"
#include "log/default_log_levels.h"
#include "virtual_dir/file_system_reader.h"
#include "base/clock.h"
#include "base/filesystem.h"
#include "game/spell.h"
#include "shared/proto_data/creature_types.pb.h"
#include "shared/proto_data/units.pb.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/unit_loot.pb.h"
#include "shared/proto_data/object_loot.pb.h"
#include "shared/proto_data/item_loot.pb.h"
#include "shared/proto_data/skinning_loot.pb.h"
#include "shared/proto_data/maps.pb.h"
#include "shared/proto_data/emotes.pb.h"
#include "shared/proto_data/objects.pb.h"
#include "shared/proto_data/skills.pb.h"
#include "shared/proto_data/talents.pb.h"
#include "shared/proto_data/talent_tabs.pb.h"
#include "shared/proto_data/vendors.pb.h"
#include "shared/proto_data/trainers.pb.h"
#include "shared/proto_data/triggers.pb.h"
#include "shared/proto_data/zones.pb.h"
#include "shared/proto_data/quests.pb.h"
#include "shared/proto_data/items.pb.h"
#include "shared/proto_data/item_sets.pb.h"
#include "shared/proto_data/races.pb.h"
#include "shared/proto_data/classes.pb.h"
#include "shared/proto_data/unit_classes.pb.h"
#include "shared/proto_data/levels.pb.h"
#include "shared/proto_data/factions.pb.h"
#include "shared/proto_data/faction_templates.pb.h"
#include "shared/proto_data/area_triggers.pb.h"
#include "shared/proto_data/spell_categories.pb.h"
#include "shared/proto_data/gtvalues.pb.h"
#include "shared/proto_data/variables.pb.h"
#include "shared/proto_data/gossip_menus.pb.h"
#include "shared/proto_data/model_data.pb.h"
#include "shared/proto_data/item_display.pb.h"
#include "shared/proto_data/object_display.pb.h"
#include "shared/proto_data/conditions.pb.h"
#include "shared/proto_data/animations.pb.h"

namespace mmo
{
	namespace proto
	{
		typedef TemplateManager<mmo::proto::Objects, mmo::proto::ObjectEntry> ObjectManager;
		typedef TemplateManager<mmo::proto::Units, mmo::proto::UnitEntry> UnitManager;
		typedef TemplateManager<mmo::proto::Maps, mmo::proto::MapEntry> MapManager;
		typedef TemplateManager<mmo::proto::Emotes, mmo::proto::EmoteEntry> EmoteManager;
		typedef TemplateManager<mmo::proto::UnitLoot, mmo::proto::LootEntry> UnitLootManager;
		typedef TemplateManager<mmo::proto::ObjectLoot, mmo::proto::LootEntry> ObjectLootManager;
		typedef TemplateManager<mmo::proto::ItemLoot, mmo::proto::LootEntry> ItemLootManager;
		typedef TemplateManager<mmo::proto::SkinningLoot, mmo::proto::LootEntry> SkinningLootManager;
		typedef TemplateManager<mmo::proto::Ranges, mmo::proto::RangeType> RangeManager;
		typedef TemplateManager<mmo::proto::Spells, mmo::proto::SpellEntry> SpellManager;
		typedef TemplateManager<mmo::proto::Skills, mmo::proto::SkillEntry> SkillManager;
		typedef TemplateManager<mmo::proto::Trainers, mmo::proto::TrainerEntry> TrainerManager;
		typedef TemplateManager<mmo::proto::Vendors, mmo::proto::VendorEntry> VendorManager;
		typedef TemplateManager<mmo::proto::Talents, mmo::proto::TalentEntry> TalentManager;
		typedef TemplateManager<mmo::proto::TalentTabs, mmo::proto::TalentTabEntry> TalentTabManager;
		typedef TemplateManager<mmo::proto::Items, mmo::proto::ItemEntry> ItemManager;
		typedef TemplateManager<mmo::proto::ItemSets, mmo::proto::ItemSetEntry> ItemSetManager;
		typedef TemplateManager<mmo::proto::Classes, mmo::proto::ClassEntry> ClassManager;
		typedef TemplateManager<mmo::proto::UnitClasses, mmo::proto::UnitClassEntry> UnitClassManager;
		typedef TemplateManager<mmo::proto::Races, mmo::proto::RaceEntry> RaceManager;
		typedef TemplateManager<mmo::proto::Levels, mmo::proto::LevelEntry> LevelManager;
		typedef TemplateManager<mmo::proto::Triggers, mmo::proto::TriggerEntry> TriggerManager;
		typedef TemplateManager<mmo::proto::Zones, mmo::proto::ZoneEntry> ZoneManager;
		typedef TemplateManager<mmo::proto::Quests, mmo::proto::QuestEntry> QuestManager;
		typedef TemplateManager<mmo::proto::Factions, mmo::proto::FactionEntry> FactionManager;
		typedef TemplateManager<mmo::proto::FactionTemplates, mmo::proto::FactionTemplateEntry> FactionTemplateManager;
		typedef TemplateManager<mmo::proto::AreaTriggers, mmo::proto::AreaTriggerEntry> AreaTriggerManager;
		typedef TemplateManager<mmo::proto::SpellCategories, mmo::proto::SpellCategoryEntry> SpellCategoryManager;
		typedef TemplateManager<mmo::proto::CombatRatings, mmo::proto::CombatRatingEntry> CombatRatingsManager;
		typedef TemplateManager<mmo::proto::MeleeCritChance, mmo::proto::MeleeCritChanceEntry> MeleeCritChanceManager;
		typedef TemplateManager<mmo::proto::SpellCritChance, mmo::proto::SpellCritChanceEntry> SpellCritChanceManager;
		typedef TemplateManager<mmo::proto::DodgeChance, mmo::proto::DodgeChanceEntry> DodgeChanceManager;
		typedef TemplateManager<mmo::proto::ResistancePercentage, mmo::proto::ResistancePercentageEntry> ResistancePercentageManager;
		typedef TemplateManager<mmo::proto::Variables, mmo::proto::VariableEntry> VariableManager;
		typedef TemplateManager<mmo::proto::GossipMenus, mmo::proto::GossipMenuEntry> GossipMenuManager;
		typedef TemplateManager<mmo::proto::ModelDatas, mmo::proto::ModelDataEntry> ModelDataManager;
		typedef TemplateManager<mmo::proto::ItemDisplayData, mmo::proto::ItemDisplayEntry> ItemDisplayManager;
		typedef TemplateManager<mmo::proto::ObjectDisplayData, mmo::proto::ObjectDisplayEntry> ObjectDisplayManager;
		typedef TemplateManager<mmo::proto::Conditions, mmo::proto::Condition> ConditionManager;
		typedef TemplateManager<mmo::proto::Animations, mmo::proto::AnimationEntry> AnimationManager;

		/// Determines whether a spell entry has a certain spell effect.
		bool SpellHasEffect(const proto::SpellEntry& spell, mmo::SpellEffect type);

		/// This class contains contains all the static game data like item templates.
		class Project final
		{
		private:

			// Make this class non-copyable

			Project(const Project &Other) = delete;
			Project &operator=(const Project &Other) = delete;

		public:

			// Data managers

			ObjectManager objects;
			UnitManager units;
			MapManager maps;
			EmoteManager emotes;
			UnitLootManager unitLoot;
			ObjectLootManager objectLoot;
			ItemLootManager itemLoot;
			SkinningLootManager skinningLoot;
			SpellManager spells;
			SkillManager skills;
			TrainerManager trainers;
			VendorManager vendors;
			TalentManager talents;
			TalentTabManager talentTabs;
			ItemManager items;
			ItemSetManager itemSets;
			ClassManager classes;
			UnitClassManager unitClasses;
			RaceManager races;
			LevelManager levels;
			TriggerManager triggers;
			ZoneManager zones;
			QuestManager quests;
			FactionManager factions;
			FactionTemplateManager factionTemplates;
			AreaTriggerManager areaTriggers;
			SpellCategoryManager spellCategories;
			CombatRatingsManager combatRatings;
			MeleeCritChanceManager meleeCritChance;
			SpellCritChanceManager spellCritChance;
			DodgeChanceManager dodgeChance;
			ResistancePercentageManager resistancePcts;
			VariableManager variables;
			GossipMenuManager gossipMenus;
			RangeManager ranges;
			ModelDataManager models;
			ItemDisplayManager itemDisplays;
			ObjectDisplayManager objectDisplays;
			ConditionManager conditions;
			AnimationManager animations;

		private:

			String m_lastPath;

		public:

			/// Gets the path that was used to load this project.
			const String &getLastPath() const { return m_lastPath; }

			Project()
			{
			}

			/// Loads the project.
			/// @param directory The path to load this project from.
			bool load(const String &directory)
			{
				// Remember last used path
				m_lastPath = directory;

				ILOG("Loading data...");
				auto loadStart = GetAsyncTimeMs();

				size_t errorCount = 0;
				DataLoadContext context;
				context.onError = [&errorCount](const String & message)
				{
					ELOG(message);
					++errorCount;
				};
				context.onWarning = [](const String & message)
				{
					WLOG(message);
				};

				const std::filesystem::path dataPath = directory;
				const auto realmDataPath = (dataPath);

				typedef ProjectLoader<DataLoadContext> RealmProjectLoader;
				typedef RealmProjectLoader::ManagerEntry ManagerEntry;

				RealmProjectLoader::Managers managers;
				managers.push_back(ManagerEntry("ranges", ranges));
				managers.push_back(ManagerEntry("spells", spells));
				managers.push_back(ManagerEntry("units", units));
				managers.push_back(ManagerEntry("objects", objects));
				managers.push_back(ManagerEntry("maps", maps));
				managers.push_back(ManagerEntry("emotes", emotes));
				managers.push_back(ManagerEntry("unit_loot", unitLoot));
				managers.push_back(ManagerEntry("object_loot", objectLoot));
				managers.push_back(ManagerEntry("item_loot", itemLoot));
				managers.push_back(ManagerEntry("skinning_loot", skinningLoot));
				managers.push_back(ManagerEntry("skills", skills));
				managers.push_back(ManagerEntry("trainers", trainers));
				managers.push_back(ManagerEntry("vendors", vendors));
				managers.push_back(ManagerEntry("talents", talents));
				managers.push_back(ManagerEntry("talent_tabs", talentTabs));
				managers.push_back(ManagerEntry("items", items));
				managers.push_back(ManagerEntry("item_sets", itemSets));
				managers.push_back(ManagerEntry("classes", classes));
				managers.push_back(ManagerEntry("unit_classes", unitClasses));
				managers.push_back(ManagerEntry("races", races));
				managers.push_back(ManagerEntry("levels", levels));
				managers.push_back(ManagerEntry("triggers", triggers));
				managers.push_back(ManagerEntry("zones", zones));
				managers.push_back(ManagerEntry("quests", quests));
				managers.push_back(ManagerEntry("factions", factions));
				managers.push_back(ManagerEntry("faction_templates", factionTemplates));
				managers.push_back(ManagerEntry("area_triggers", areaTriggers));
				managers.push_back(ManagerEntry("spell_categories", spellCategories));
				managers.push_back(ManagerEntry("combat_ratings", combatRatings));
				managers.push_back(ManagerEntry("melee_crit_chance", meleeCritChance));
				managers.push_back(ManagerEntry("spell_crit_chance", spellCritChance));
				managers.push_back(ManagerEntry("dodge_chance", dodgeChance));
				managers.push_back(ManagerEntry("resistance_percentages", resistancePcts));
				managers.push_back(ManagerEntry("variables", variables));
				managers.push_back(ManagerEntry("gossip_menus", gossipMenus));
				managers.push_back(ManagerEntry("model_data", models));
				managers.push_back(ManagerEntry("item_displays", itemDisplays));
				managers.push_back(ManagerEntry("object_displays", objectDisplays));
				managers.push_back(ManagerEntry("conditions", conditions));
				managers.push_back(ManagerEntry("animations", animations));

				virtual_dir::FileSystemReader virtualDirectory(realmDataPath);
				if (!RealmProjectLoader::load(
				            virtualDirectory,
				            managers,
				            context))
				{
					ELOG("Game data error count: " << errorCount << "+");
					return false;
				}

				auto loadEnd = GetAsyncTimeMs();
				ILOG("Loading finished in " << (loadEnd - loadStart) << "ms");

				return true;
			}
			/// Saves the project.
			/// @param directory The path to save this project to.
			bool save(const String &directory)
			{
				// Remember last used path
				m_lastPath = directory;

				ILOG("Saving data...");
				auto saveStart = GetAsyncTimeMs();

				const std::filesystem::path dataPath = directory;
				const auto realmDataPath = (dataPath);

				typedef ProjectSaver RealmProjectSaver;
				typedef ProjectSaver::Manager ManagerEntry;

				RealmProjectSaver::Managers managers;
				managers.push_back(ManagerEntry("ranges", "ranges", ranges));
				managers.push_back(ManagerEntry("spells", "spells", spells));
				managers.push_back(ManagerEntry("units", "units", units));
				managers.push_back(ManagerEntry("objects", "objects", objects));
				managers.push_back(ManagerEntry("maps", "maps", maps));
				managers.push_back(ManagerEntry("emotes", "emotes", emotes));
				managers.push_back(ManagerEntry("unit_loot", "unit_loot", unitLoot));
				managers.push_back(ManagerEntry("object_loot", "object_loot", objectLoot));
				managers.push_back(ManagerEntry("item_loot", "item_loot", itemLoot));
				managers.push_back(ManagerEntry("skinning_loot", "skinning_loot", skinningLoot));
				managers.push_back(ManagerEntry("skills", "skills", skills));
				managers.push_back(ManagerEntry("trainers", "trainers", trainers));
				managers.push_back(ManagerEntry("vendors", "vendors", vendors));
				managers.push_back(ManagerEntry("talents", "talents", talents));
				managers.push_back(ManagerEntry("talent_tabs", "talent_tabs", talentTabs));
				managers.push_back(ManagerEntry("items", "items", items));
				managers.push_back(ManagerEntry("item_sets", "item_sets", itemSets));
				managers.push_back(ManagerEntry("unit_classes", "unit_classes", unitClasses));
				managers.push_back(ManagerEntry("classes", "classes", classes));
				managers.push_back(ManagerEntry("races", "races", races));
				managers.push_back(ManagerEntry("levels", "levels", levels));
				managers.push_back(ManagerEntry("triggers", "triggers", triggers));
				managers.push_back(ManagerEntry("zones", "zones", zones));
				managers.push_back(ManagerEntry("quests", "quests", quests));
				managers.push_back(ManagerEntry("factions", "factions", factions));
				managers.push_back(ManagerEntry("faction_templates", "faction_templates", factionTemplates));
				managers.push_back(ManagerEntry("area_triggers", "area_triggers", areaTriggers));
				managers.push_back(ManagerEntry("spell_categories", "spell_categories", spellCategories));
				managers.push_back(ManagerEntry("combat_ratings", "combat_ratings", combatRatings));
				managers.push_back(ManagerEntry("melee_crit_chance", "melee_crit_chance", meleeCritChance));
				managers.push_back(ManagerEntry("spell_crit_chance", "spell_crit_chance", spellCritChance));
				managers.push_back(ManagerEntry("dodge_chance", "dodge_chance", dodgeChance));
				managers.push_back(ManagerEntry("resistance_percentages", "resistance_percentages", resistancePcts));
				managers.push_back(ManagerEntry("variables", "variables", variables));
				managers.push_back(ManagerEntry("gossip_menus", "gossip_menus", gossipMenus));
				managers.push_back(ManagerEntry("model_data", "model_data", models));
				managers.push_back(ManagerEntry("item_displays", "item_displays", itemDisplays));
				managers.push_back(ManagerEntry("object_displays", "object_displays", objectDisplays));
				managers.push_back(ManagerEntry("conditions", "conditions", conditions));
				managers.push_back(ManagerEntry("animations", "animations", animations));

				if (!RealmProjectSaver::save(realmDataPath, managers))
				{
					ELOG("Could not save data project!");
					return false;
				}

				auto saveEnd = GetAsyncTimeMs();
				ILOG("Saving finished in " << (saveEnd - saveStart) << "ms");
				return true;
			}
		};
	}
}
