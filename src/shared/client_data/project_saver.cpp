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
	namespace proto_client
	{
		static bool saveAndAddManagerToTable(sff::write::Table<char> &fileTable, const std::filesystem::path &directory, ProjectSaver::Manager &manager)
		{
			const String managerRelativeFileName = manager.fileName + ".data";
			const String managerAbsoluteFileName = (directory / managerRelativeFileName).string();

			if (!manager.save(managerAbsoluteFileName))
			{
				ELOG("Failed to save manager " << manager.name << " to absolute filename " << managerAbsoluteFileName);
				return false;
			}

			sff::write::Table table(fileTable, manager.name, sff::write::Comma);
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
				if (!saveAndAddManagerToTable(fileTable, directory, manager))
				{
					ELOG("Failed to save manager " << manager.name << " to file " << manager.fileName);
					success = false;
				}
			}

			return success;
		}

		bool ProjectSaver::save(const std::filesystem::path &directory, Managers &managers)
		{
			const mmo::String projectFileName = (directory / "project.txt").string();

			return
			    sff::save_file(projectFileName, [directory, &managers]<typename T0>(T0&& PH1)
			    {
				    return saveProjectToTable(std::forward<T0>(PH1), directory, managers);
			    });
		}
	}
}
