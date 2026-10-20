// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/crash_context.h"

#include <string>
#include <thread>
#include <vector>

using namespace mmo;

TEST_CASE("No crash context is reported outside of any scope", "[crash_context]")
{
	std::vector<std::string> details;
	AppendCrashContext(details);

	CHECK(details.empty());
}

TEST_CASE("Nested crash context scopes are reported outermost first", "[crash_context]")
{
	std::vector<std::string> details;

	{
		const CrashContextScope outer("world instance update: map %u", 1u);
		{
			const CrashContextScope inner("client packet 0x%04x from '%s'", 0x2au, "Kyoril");
			AppendCrashContext(details);
		}
	}

	REQUIRE(details.size() == 2);
	CHECK(details[0] == "Context 0: world instance update: map 1");
	CHECK(details[1] == "Context 1: client packet 0x002a from 'Kyoril'");
}

TEST_CASE("A crash context scope pops its line when it ends", "[crash_context]")
{
	const CrashContextScope outer("outer");
	{
		const CrashContextScope inner("inner");
	}

	std::vector<std::string> details;
	AppendCrashContext(details);

	REQUIRE(details.size() == 1);
	CHECK(details[0] == "Context 0: outer");
}

TEST_CASE("Crash context is per thread", "[crash_context]")
{
	const CrashContextScope mainScope("main thread");

	std::vector<std::string> otherDetails;
	std::thread other([&otherDetails]()
	{
		const CrashContextScope otherScope("other thread");
		AppendCrashContext(otherDetails);
	});
	other.join();

	REQUIRE(otherDetails.size() == 1);
	CHECK(otherDetails[0] == "Context 0: other thread");
}

TEST_CASE("Overlong crash context lines are truncated, not overflowed", "[crash_context]")
{
	const std::string longName(1000, 'x');
	const CrashContextScope scope("name %s", longName.c_str());

	const std::string text = scope.GetText();
	CHECK(text.size() < 300);
	CHECK(text.compare(0, 6, "name x") == 0);
}
