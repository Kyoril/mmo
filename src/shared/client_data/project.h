#pragma once

#include "project_loader.h"
#include "project_saver.h"
#include "proto_template.h"
#include "log/default_log_levels.h"
#include "virtual_dir/file_system_reader.h"
#include "base/clock.h"
#include "base/filesystem.h"
#include "shared/client_data/spells.pb.h"
#include "shared/client_data/maps.pb.h"
#include "shared/client_data/spell_categories.pb.h"

namespace mmo
{
	namespace proto_client
	{
		typedef TemplateManager<mmo::proto_client::Spells, mmo::proto_client::SpellEntry> SpellManager;
		typedef TemplateManager<mmo::proto_client::SpellCategories, mmo::proto_client::SpellCategoryEntry> SpellCategoryManager;
		//typedef TemplateManager<mmo::proto_client::Maps, mmo::proto_client::MapEntry> MapManager;

		/// This class contains contains all the static game data like item templates.
		class Project final
		{
		private:

			// Make this class non-copyable

			Project(const Project &Other) = delete;
			Project &operator=(const Project &Other) = delete;

		public:

			// Data managers

			//MapManager maps;
			SpellManager spells;
			SpellCategoryManager spellCategories;

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
				//managers.push_back(ManagerEntry("maps", maps));
				managers.push_back(ManagerEntry("spell_categories", spellCategories));

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
				//managers.emplace_back("maps", "maps", maps);
				managers.emplace_back("spell_categories", "spell_categories", spellCategories);

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
