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
