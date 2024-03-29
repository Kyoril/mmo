// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <string>
#include <cstdint>

namespace mmo::updating
{
	struct IUpdaterProgressHandler
	{
		virtual ~IUpdaterProgressHandler() = default;
		virtual void updateFile(const std::string &name, std::uintmax_t size, std::uintmax_t loaded) = 0;
	};
}
