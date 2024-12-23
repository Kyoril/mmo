// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
