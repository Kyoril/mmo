#include "prepare_update.h"
#include "prepare_parameters.h"
#include "update_source.h"
#include "update_list_properties.h"
#include "file_system_entry_handler.h"
#include "parse_entry.h"
#include "simple_file_format/sff_load_file.h"

#include <exception>


namespace mmo
{
	namespace updating
	{
		PreparedUpdate prepareUpdate(
		    const std::string &outputDir,
		    const PrepareParameters &parameters
		)
		{
			const auto listFile = parameters.source->readFile("list.txt");

			std::string sourceContent;
			sff::read::tree::Table<std::string::const_iterator> sourceTable;
			sff::loadTableFromFile(sourceTable, sourceContent, *listFile.content);

			UpdateListProperties listProperties;
			listProperties.version = sourceTable.getInteger<unsigned>("version", 0);
			if (listProperties.version <= 1)
			{
				const auto *const root = sourceTable.getTable("root");
				if (!root)
				{
					throw std::runtime_error("Root directory entry is missing");
				}

				FileSystemEntryHandler fileSystem;
				return parseEntry(
				           parameters,
				           listProperties,
				           *root,
				           "",
				           outputDir,
				           fileSystem
				       );
			}
			else
			{
				throw std::runtime_error(
				    "Unsupported update list version: " +
				    std::to_string(listProperties.version));
			}
		}
	}
}
