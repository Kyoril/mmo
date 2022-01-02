// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "simple_file_format/sff_read_tree.h"
#include "base/sha1.h"
#include "prepared_update.h"


namespace mmo::updating
{
	struct PrepareParameters;
	struct UpdateListProperties;


	struct IFileEntryHandler
	{
		virtual ~IFileEntryHandler() = default;

		virtual PreparedUpdate handleDirectory(
		    const PrepareParameters &parameters,
		    const UpdateListProperties &listProperties,
		    const sff::read::tree::Array<std::string::const_iterator> &entries,
		    const std::string &type,
		    const std::string &source,
		    const std::string &destination
		) = 0;

		virtual PreparedUpdate handleFile(
		    const PrepareParameters &parameters,
		    const sff::read::tree::Table<std::string::const_iterator> &entryDescription,
		    const std::string &source,
		    const std::string &destination,
		    std::uintmax_t originalSize,
		    const SHA1Hash &sha1,
		    const std::string &compression,
		    std::uintmax_t compressedSize
		) = 0;

		virtual PreparedUpdate finish(const PrepareParameters &parameters) = 0;
	};
}
