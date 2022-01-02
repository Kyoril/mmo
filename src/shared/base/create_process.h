// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <vector>
#include <string>

namespace mmo
{
	void createProcess(
	    std::string executable,
	    std::vector<std::string> arguments);

	void makeExecutable(const std::string &file);
}
