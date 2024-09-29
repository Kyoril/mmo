// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "update_parameters.h"
#include "update_source.h"


namespace mmo::updating
{
	UpdateParameters::UpdateParameters(
	    std::unique_ptr<IUpdateSource> source,
	    bool doUnpackArchives,
	    IUpdaterProgressHandler &progressHandler
	)
		: source(std::move(source))
		, doUnpackArchives(doUnpackArchives)
		, progressHandler(progressHandler)
	{
	}

	UpdateParameters::~UpdateParameters() = default;
}
