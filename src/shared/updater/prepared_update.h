// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

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

			Estimates();
		};


		std::vector<PreparedUpdateStep> steps;
		Estimates estimates;


		PreparedUpdate();
		//TODO: move operations
	};


	PreparedUpdate accumulate(std::vector<PreparedUpdate> updates);
}
