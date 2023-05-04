#include <functional>
#include <fstream>
#include <vector>
#include <type_traits>
#include "base/sha1.h"

#include "project_saver.h"

#include <stduuid/uuid.h>

#include "simple_file_format/sff_write.h"
#include "simple_file_format/sff_save_file.h"

#include "base/filesystem.h"

namespace mmo
{
	namespace proto
	{
		static bool saveAndAddManagerToTable(sff::write::Table<char> &fileTable, const std::filesystem::path &directory, ProjectSaver::Manager &manager)
		{
			const String managerRelativeFileName = manager.fileName + ".data";
			const String managerAbsoluteFileName = (directory / managerRelativeFileName).string();

			if (!manager.save(managerAbsoluteFileName))
			{
				return false;
			}

			sff::write::Table<char> table(fileTable, manager.name, sff::write::Comma);
			table.addKey("file", managerRelativeFileName);
			table.Finish();

			return true;
		}

		static bool saveProjectToTable(sff::write::Table<char> &fileTable, const std::filesystem::path &directory, ProjectSaver::Managers &managers)
		{
			fileTable.addKey("version", 1);

			bool success = true;

			for (ProjectSaver::Manager &manager : managers)
			{
				success =
				    saveAndAddManagerToTable(fileTable, directory, manager) &&
				    success;
			}

			return success;
		}

		bool ProjectSaver::save(const std::filesystem::path &directory, Managers &managers)
		{
			const mmo::String projectFileName = (directory / "project.txt").string();

			return
			    sff::save_file(projectFileName, std::bind(saveProjectToTable, std::placeholders::_1, directory, std::ref(managers)));
		}
	}
}
