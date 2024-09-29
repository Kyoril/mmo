// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "prepared_update.h"
#include "simple_file_format/sff_read_tree.h"


namespace mmo::updating
{
	struct PrepareParameters;
	struct UpdateListProperties;
	struct IFileEntryHandler;


	PreparedUpdate parseEntry(
	    const PrepareParameters &parameters,
	    const UpdateListProperties &listProperties,
	    const sff::read::tree::Table<std::string::const_iterator> &entryDescription,
	    const std::string &source,
	    const std::string &destination,
	    IFileEntryHandler &handler
	);

}
