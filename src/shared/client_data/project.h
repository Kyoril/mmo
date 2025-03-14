// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "project_loader.h"
#include "project_saver.h"
#include "proto_template.h"
#include "log/default_log_levels.h"
#include "virtual_dir/file_system_reader.h"
#include "base/clock.h"
#include "base/filesystem.h"
#include "shared/client_data/proto_client/factions.pb.h"
#include "shared/client_data/proto_client/faction_templates.pb.h"
#include "shared/client_data/proto_client/spells.pb.h"
#include "shared/client_data/proto_client/zones.pb.h"
#include "shared/client_data/proto_client/maps.pb.h"
#include "shared/client_data/proto_client/spell_categories.pb.h"
#include "shared/client_data/proto_client/model_data.pb.h"
#include "shared/client_data/proto_client/races.pb.h"

namespace mmo
{
	namespace proto_client
	{
		typedef TemplateManager<mmo::proto_client::Races, mmo::proto_client::RaceEntry> RaceManager;
		typedef TemplateManager<mmo::proto_client::Zones, mmo::proto_client::ZoneEntry> ZoneManager;
		typedef TemplateManager<mmo::proto_client::Ranges, mmo::proto_client::RangeType> RangeManager;
		typedef TemplateManager<mmo::proto_client::Spells, mmo::proto_client::SpellEntry> SpellManager;
		typedef TemplateManager<mmo::proto_client::SpellCategories, mmo::proto_client::SpellCategoryEntry> SpellCategoryManager;
		typedef TemplateManager<mmo::proto_client::ModelDatas, mmo::proto_client::ModelDataEntry> ModelDataManager;
		typedef TemplateManager<mmo::proto_client::Factions, mmo::proto_client::FactionEntry> FactionManager;
		typedef TemplateManager<mmo::proto_client::FactionTemplates, mmo::proto_client::FactionTemplateEntry> FactionTemplateManager;
		typedef TemplateManager<mmo::proto_client::Maps, mmo::proto_client::MapEntry> MapManager;

		/// This class contains contains all the static game data like item templates.
		class Project final
		{
		private:

			// Make this class non-copyable

			Project(const Project &Other) = delete;
			Project &operator=(const Project &Other) = delete;

		public:

			// Data managers

			MapManager maps;
			ZoneManager zones;
			SpellManager spells;
			RangeManager ranges;
			SpellCategoryManager spellCategories;
			RaceManager races;
			ModelDataManager models;
			FactionManager factions;
			FactionTemplateManager factionTemplates;

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

				typedef ProjectLoader<DataLoadContext> ClientProjectLoader;
				typedef ClientProjectLoader::ManagerEntry ManagerEntry;

				ClientProjectLoader::Managers managers;
				managers.push_back(ManagerEntry("spells", spells));
				managers.push_back(ManagerEntry("ranges", ranges));
				managers.push_back(ManagerEntry("maps", maps));
				managers.push_back(ManagerEntry("spell_categories", spellCategories));
				managers.push_back(ManagerEntry("model_data", models));
				managers.push_back(ManagerEntry("races", races));
				managers.push_back(ManagerEntry("factions", factions));
				managers.push_back(ManagerEntry("faction_templates", factionTemplates));
				managers.push_back(ManagerEntry("zones", zones));

				if (!ClientProjectLoader::load(
				            directory,
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

				typedef ProjectSaver ClientProjectSaver;

				ClientProjectSaver::Managers managers;
				managers.emplace_back("spells", "spells", spells);
				managers.emplace_back("ranges", "ranges", ranges);
				managers.emplace_back("maps", "maps", maps);
				managers.emplace_back("spell_categories", "spell_categories", spellCategories);
				managers.emplace_back("model_data", "model_data", models);
				managers.emplace_back("races", "races", races);
				managers.emplace_back("zones", "zones", zones);

				if (!ClientProjectSaver::save(realmDataPath, managers))
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
