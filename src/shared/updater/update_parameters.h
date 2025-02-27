// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>

namespace mmo::updating
{
	struct IUpdaterProgressHandler;
	struct IUpdateSource;


	struct UpdateParameters
	{
		std::unique_ptr<IUpdateSource> source;
		bool doUnpackArchives;
		IUpdaterProgressHandler &progressHandler;


		UpdateParameters(
		    std::unique_ptr<IUpdateSource> source,
		    bool doUnpackArchives,
		    IUpdaterProgressHandler &progressHandler
		);
		~UpdateParameters();
	};
}
