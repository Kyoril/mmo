// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "configuration.h"

#include "simple_file_format/sff_write.h"
#include "simple_file_format/sff_read_tree.h"
#include "simple_file_format/sff_load_file.h"
#include "base/constants.h"
#include "log/default_log_levels.h"
#include "base/filesystem.h"

#include <fstream>


namespace mmo
{
	const uint32 Configuration::ModelEditorConfigVersion = 0x01;

	Configuration::Configuration()
		: assetRegistryPath("")
		, projectPath("")
		, mysqlHost("127.0.0.1")
		, mysqlPort(mmo::constants::DefaultMySQLPort)
		, mysqlUser("mmo")
		, mysqlPassword("")
		, mysqlDatabase("mmo_editor")
	{
	}

	bool Configuration::Load(const String& fileName)
	{
		typedef String::const_iterator Iterator;
		typedef sff::read::tree::Table<Iterator> Table;

		Table global;
		std::string fileContent;

		std::ifstream file(fileName, std::ios::binary);
		if (!file)
		{
			if (Save(fileName))
			{
				ILOG("Saved default settings as " << fileName);
			}
			else
			{
				ELOG("Could not save default settings as " << fileName);
			}

			return false;
		}

		try
		{
			sff::loadTableFromFile(global, fileContent, file);

			// Read config version
			uint32 fileVersion = 0;
			if (!global.tryGetInteger("version", fileVersion) ||
				fileVersion != ModelEditorConfigVersion)
			{
				file.close();

				if (Save(fileName + ".updated"))
				{
					ILOG("Saved updated settings with default values as " << fileName << ".updated");
					ILOG("Please insert values from the old setting file manually and rename the file.");
				}
				else
				{
					ELOG("Could not save updated default settings as " << fileName << ".updated");
				}

				return false;
			}

			if (const Table* const dataTable = global.getTable("data"))
			{
				assetRegistryPath = dataTable->getString("assetRegistryPath", assetRegistryPath);
				projectPath = dataTable->getString("projectPath", projectPath);
			}

			if (const Table* const mysqlDatabaseTable = global.getTable("mysqlDatabase"))
			{
				mysqlPort = mysqlDatabaseTable->getInteger("port", mysqlPort);
				mysqlHost = mysqlDatabaseTable->getString("host", mysqlHost);
				mysqlUser = mysqlDatabaseTable->getString("user", mysqlUser);
				mysqlPassword = mysqlDatabaseTable->getString("password", mysqlPassword);
				mysqlDatabase = mysqlDatabaseTable->getString("database", mysqlDatabase);
			}
		}
		catch (const sff::read::ParseException<Iterator>& e)
		{
			const auto line = std::count<Iterator>(fileContent.begin(), e.position.begin, '\n');
			ELOG("Error in config: " << e.what());
			ELOG("Line " << (line + 1) << ": " << e.position.str());
			return false;
		}

		return true;
	}

	bool Configuration::Save(const String& fileName)
	{
		// Try to create directories
		try
		{
			std::filesystem::create_directories(
				std::filesystem::path(fileName).remove_filename());
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			ELOG("Failed to create config directories: " << e.what() << "\n\tPath 1: " << e.path1() << "\n\tPath 2: " << e.path2());
		}

		std::ofstream file(fileName.c_str());
		if (!file)
		{
			ELOG("Failed to open config file " << fileName << " for writing!");
			return false;
		}

		typedef char Char;
		sff::write::File<Char> global(file, sff::write::MultiLine);

		// Save file version
		global.addKey("version", ModelEditorConfigVersion);
		global.writer.newLine();

		{
			sff::write::Table<Char> dataTable(global, "data", sff::write::MultiLine);
			dataTable.addKey("assetRegistryPath", assetRegistryPath);
			dataTable.addKey("projectPath", projectPath);
			dataTable.Finish();
		}

		global.writer.newLine();

		{
			sff::write::Table<Char> mysqlDatabaseTable(global, "mysqlDatabase", sff::write::MultiLine);
			mysqlDatabaseTable.addKey("port", mysqlPort);
			mysqlDatabaseTable.addKey("host", mysqlHost);
			mysqlDatabaseTable.addKey("user", mysqlUser);
			mysqlDatabaseTable.addKey("password", mysqlPassword);
			mysqlDatabaseTable.addKey("database", mysqlDatabase);
			mysqlDatabaseTable.Finish();
		}

		return true;
	}
}
