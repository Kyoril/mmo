// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "update_source_file.h"


namespace mmo::updating
{
	struct IUpdateSource
	{
		virtual ~IUpdateSource() = default;
		virtual UpdateSourceFile readFile(
		    const std::string &path
		) = 0;
	};
}
