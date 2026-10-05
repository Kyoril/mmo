// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "update_source.h"

#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace mmo::updating
{
	struct UpdateParameters;


	struct PreparedUpdateStep
	{
		typedef std::function<bool (const UpdateParameters &)> StepFunction;


		std::string destinationPath;
		StepFunction step;
		/// The files this step downloads, in the order it reads them.
		std::vector<RemoteFile> downloads;


		PreparedUpdateStep();
		PreparedUpdateStep(std::string destinationPath, StepFunction step);
		//TODO: move operations
	};


	struct PreparedUpdate
	{
		struct Estimates
		{
			std::uintmax_t downloadSize;
			std::uintmax_t updateSize;		// Uncompressed update size for better progress!
			std::uintmax_t fileCount;		// Number of files that have to be downloaded.

			Estimates();
		};


		std::vector<PreparedUpdateStep> steps;
		Estimates estimates;


		PreparedUpdate();
		//TODO: move operations
	};


	PreparedUpdate accumulate(std::vector<PreparedUpdate> updates);
}
