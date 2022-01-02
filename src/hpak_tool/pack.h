// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <functional>
#include "base/filesystem.h"

namespace mmo
{
	typedef std::function<void (double totalProgress, const std::filesystem::path &currentFile)>
	PackProgressCallback;
	typedef std::function<bool (const std::filesystem::path &)> PathFilter;


	bool Pack(
	    std::ostream &archive,
	    const std::filesystem::path &source,
	    bool isCompressionEnabled,
	    const PathFilter &inclusionFilter,
	    const PackProgressCallback &callback
	);
}
