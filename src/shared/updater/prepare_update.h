// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "prepared_update.h"


namespace mmo::updating
{
	struct PrepareParameters;


	PreparedUpdate prepareUpdate(
	    const std::string &outputDir,
	    const PrepareParameters &parameters
	);
}
