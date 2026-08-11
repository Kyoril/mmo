// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "updater/update_parameters.h"
#include "updater/updater_progress_handler.h"
// unique_ptr<IUpdateSource> needs the complete type wherever one is destroyed.
#include "updater/update_source.h"

#include "updater/prepared_update.h"

#include <vector>

using namespace mmo::updating;

namespace
{
	struct NullProgressHandler final : IUpdaterProgressHandler
	{
		void updateFile(const std::string&, std::uintmax_t, std::uintmax_t) override {}
	};

	PreparedUpdate MakeUpdate(std::uintmax_t downloadSize, std::uintmax_t updateSize,
		std::initializer_list<const char*> stepPaths)
	{
		PreparedUpdate update;
		update.estimates.downloadSize = downloadSize;
		update.estimates.updateSize = updateSize;

		for (const char* path : stepPaths)
		{
			update.steps.emplace_back(path, PreparedUpdateStep::StepFunction());
		}

		return update;
	}
}

TEST_CASE("PreparedUpdate estimates start at zero", "[updater][prepared_update]")
{
	const PreparedUpdate update;

	CHECK(update.estimates.downloadSize == 0);
	CHECK(update.estimates.updateSize == 0);
	CHECK(update.steps.empty());
}

TEST_CASE("accumulate sums both size estimates", "[updater][prepared_update]")
{
	std::vector<PreparedUpdate> parts;
	parts.push_back(MakeUpdate(100, 250, {}));
	parts.push_back(MakeUpdate(50, 75, {}));

	const PreparedUpdate sum = accumulate(std::move(parts));

	// The two are tracked separately on purpose: downloadSize drives the network progress
	// bar, updateSize the uncompressed-write one.
	CHECK(sum.estimates.downloadSize == 150);
	CHECK(sum.estimates.updateSize == 325);
}

TEST_CASE("accumulate concatenates steps in order", "[updater][prepared_update]")
{
	std::vector<PreparedUpdate> parts;
	parts.push_back(MakeUpdate(0, 0, { "first.txt", "second.txt" }));
	parts.push_back(MakeUpdate(0, 0, { "third.txt" }));

	const PreparedUpdate sum = accumulate(std::move(parts));

	REQUIRE(sum.steps.size() == 3);
	CHECK(sum.steps[0].destinationPath == "first.txt");
	CHECK(sum.steps[1].destinationPath == "second.txt");
	CHECK(sum.steps[2].destinationPath == "third.txt");
}

TEST_CASE("accumulate of nothing is an empty update", "[updater][prepared_update]")
{
	const PreparedUpdate sum = accumulate({});

	CHECK(sum.steps.empty());
	CHECK(sum.estimates.downloadSize == 0);
	CHECK(sum.estimates.updateSize == 0);
}

TEST_CASE("accumulate keeps the step callables, not just the paths",
	"[updater][prepared_update]")
{
	bool called = false;

	PreparedUpdate part;
	part.steps.emplace_back("file.txt",
		[&called](const UpdateParameters&) { called = true; return true; });

	std::vector<PreparedUpdate> parts;
	parts.push_back(std::move(part));

	const PreparedUpdate sum = accumulate(std::move(parts));

	REQUIRE(sum.steps.size() == 1);
	REQUIRE(sum.steps[0].step);

	NullProgressHandler progressHandler;
	const UpdateParameters parameters(nullptr, false, progressHandler);
	CHECK(sum.steps[0].step(parameters));
	CHECK(called);
}
