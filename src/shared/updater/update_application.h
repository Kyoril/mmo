// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/filesystem.h"

#include <functional>

namespace mmo::updating
{
	struct PreparedUpdate;
	struct UpdateParameters;


	struct ApplicationUpdate
	{
		std::function<void (const UpdateParameters &, const char *const *, size_t)> perform;
	};


	ApplicationUpdate updateApplication(
	    const std::filesystem::path &applicationPath,
	    const PreparedUpdate &preparedUpdate
	);
}
