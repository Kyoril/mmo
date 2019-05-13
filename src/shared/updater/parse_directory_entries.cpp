#include "parse_directory_entries.h"
#include "parse_entry.h"

#include "base/macros.h"


namespace mmo
{
	namespace updating
	{
		PreparedUpdate parseDirectoryEntries(
		    const PrepareParameters &parameters,
		    const UpdateListProperties &listProperties,
		    const std::string &source,
		    const std::string &destination,
		    const sff::read::tree::Array<std::string::const_iterator> &entries,
		    IFileEntryHandler &handler
		)
		{
			std::vector<PreparedUpdate> updates;

			for (size_t i = 0, c = entries.getSize(); i < c; ++i)
			{
				const auto *const entry = entries.getTable(i);
				if (!entry)
				{
					throw std::runtime_error("Non-table element in entries array");
				}

				updates.push_back(parseEntry(
				                      parameters,
				                      listProperties,
				                      *entry,
				                      source,
				                      destination,
				                      handler
				                  ));
			}

			return accumulate(std::move(updates));
		}
	}
}
