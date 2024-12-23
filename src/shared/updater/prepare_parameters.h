// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>
#include <string>
#include <set>

namespace mmo::updating
{
	struct IPrepareProgressHandler;
	struct IUpdateSource;


	struct PrepareParameters
	{
		std::unique_ptr<IUpdateSource> source;
		std::set<std::string> conditionsSet;
		bool doUnpackArchives;
		IPrepareProgressHandler &progressHandler;


		PrepareParameters(
		    std::unique_ptr<IUpdateSource> source,
		    std::set<std::string> conditionsSet,
		    bool doUnpackArchives,
		    IPrepareProgressHandler &progressHandler);
		~PrepareParameters();
	};
}
