// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "log_exception.h"
#include "default_log_levels.h"

namespace mmo
{
	void defaultLogException(const std::exception &exception)
	{
		ELOG(exception.what());
	}
}
