// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "simple_file_format/sff_read_tree.h"
#include "prepared_update.h"


namespace mmo::updating
{
	struct PrepareParameters;
	struct UpdateListProperties;
	struct IFileEntryHandler;


	PreparedUpdate parseDirectoryEntries(
	    const PrepareParameters &parameters,
	    const UpdateListProperties &listProperties,
	    const std::string &source,
	    const std::string &destination,
	    const sff::read::tree::Array<std::string::const_iterator> &entries,
	    IFileEntryHandler &handler
	);
}
