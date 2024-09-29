// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <string>

namespace mmo::updating
{
	struct IPrepareProgressHandler
	{
		virtual ~IPrepareProgressHandler() = default;
		virtual void beginCheckLocalCopy(const std::string &name) = 0;
	};
}
