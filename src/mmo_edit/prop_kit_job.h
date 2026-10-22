// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Runs the unattended prop kit job described by a JSON file: writes (or overwrites) a
	///        prop kit asset, writes a small single-group world model from props and lights
	///        ('worldModel'), places world models in a world ('place'), assigns kits to world model
	///        placements and optionally strips named mesh refs (and lights) from a world model.
	///        Doorway clearance issues of the kit against its preview world model are logged as
	///        warnings and do not fail the job.
	/// @param jsonPath Path of the job description on disk.
	/// @return false if the job file could not be loaded or any asset could not be loaded or written.
	bool ApplyPropKitJob(const String& jsonPath);
}
