// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "prepared_update.h"


namespace mmo::updating
{
	PreparedUpdateStep::PreparedUpdateStep() = default;

	PreparedUpdateStep::PreparedUpdateStep(std::string destinationPath, StepFunction step)
		: destinationPath(std::move(destinationPath))
		, step(std::move(step))
	{
	}


	PreparedUpdate::Estimates::Estimates()
		: downloadSize(0)
		, updateSize(0)
	{
	}

	PreparedUpdate::PreparedUpdate() = default;


	PreparedUpdate accumulate(std::vector<PreparedUpdate> updates)
	{
		PreparedUpdate sum;

		for (PreparedUpdate & part : updates)
		{
			sum.estimates.downloadSize += part.estimates.downloadSize;
			sum.estimates.updateSize += part.estimates.updateSize;

			sum.steps.insert(
			    sum.steps.end(),
			    std::make_move_iterator(part.steps.begin()),
			    std::make_move_iterator(part.steps.end())
			);
		}

		return sum;
	}
}
