// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
