// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "parse_entry.h"
#include "update_list_properties.h"
#include "file_entry_handler.h"
#include "prepare_parameters.h"
#include "virtual_dir/path.h"
#include "base/sha1.h"

#include <sstream>


namespace mmo::updating
{
	namespace
	{
		PreparedUpdate makeIf(
		    const PrepareParameters &parameters,
		    const UpdateListProperties &listProperties,
		    const sff::read::tree::Table<std::string::const_iterator> &entryDescription,
		    const std::string &source,
		    const std::string &destination,
		    IFileEntryHandler &handler)
		{
			const auto condition = entryDescription.getString("condition");
			if (!parameters.conditionsSet.count(condition))
			{
				return PreparedUpdate();
			}

			const auto *const value = entryDescription.getTable("value");
			if (!value)
			{
				throw std::runtime_error("'if' value is missing");
			}

			return parseEntry(
			           parameters,
			           listProperties,
			           *value,
			           source,
			           destination,
			           handler
			       );
		}
	}


	PreparedUpdate parseEntry(
	    const PrepareParameters &parameters,
	    const UpdateListProperties &listProperties,
	    const sff::read::tree::Table<std::string::const_iterator> &entryDescription,
	    const std::string &source,
	    const std::string &destination,
	    IFileEntryHandler &handler
	)
	{
		const auto type = entryDescription.getString("type");
		if (type == "if")
		{
			return makeIf(
			           parameters,
			           listProperties,
			           entryDescription,
			           source,
			           destination,
			           handler);
		}

		const auto name = entryDescription.getString("name");
		std::string compressedName;
		const auto subSource =
			virtual_dir::joinPaths(source,
		        entryDescription.tryGetString("compressedName", compressedName) ? compressedName : name);
		const auto subDestination = virtual_dir::joinPaths(destination, name);

		if (const auto *const entries = entryDescription.getArray("entries"))
		{
			return handler.handleDirectory(
			           parameters,
			           listProperties,
			           *entries,
			           type,
			           subSource,
			           subDestination
			       );
		}
		else
		{
			std::uintmax_t newSize = 0;
			if (!entryDescription.tryGetInteger(
			            (listProperties.version >= 1 ? "originalSize" : "size"),
			            newSize))
			{
				throw std::runtime_error("Entry file size is missing");
			}

			SHA1Hash newSHA1;
			{
				std::string sha1Hex;
				if (!entryDescription.tryGetString("sha1", sha1Hex))
				{
					throw std::runtime_error("SHA-1 digest is missing");
				}
				std::istringstream sha1HexStream(sha1Hex);
				newSHA1 = sha1ParseHex(sha1HexStream);
				if (sha1HexStream.bad())
				{
					throw std::runtime_error("Invalid SHA-1 digest");
				}
			}

			std::uintmax_t compressedSize = newSize;
			std::string compression;
			if (listProperties.version >= 1)
			{
				compression = entryDescription.getString("compression");
				if (!compression.empty() &&
				    !entryDescription.tryGetInteger("compressedSize", compressedSize))
				{
					throw std::runtime_error(
					    "Compressed file size is missing");
				}
			}

			return handler.handleFile(
			           parameters,
			           entryDescription,
			           subSource,
			           subDestination,
			           newSize,
			           newSHA1,
			           compression,
			           compressedSize
			       );
		}
	}
}
