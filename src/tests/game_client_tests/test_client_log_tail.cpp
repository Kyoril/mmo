// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "systems/client_log_tail.h"

using namespace mmo;

TEST_CASE("ClientLogTail keeps the newest lines in order", "[bug_report]")
{
	ClientLogTail tail(3);
	tail.Add("one");
	tail.Add("two");
	tail.Add("three");
	tail.Add("four");

	CHECK(tail.GetText() == "two\nthree\nfour\n");
}

TEST_CASE("ClientLogTail with no capacity stays empty", "[bug_report]")
{
	ClientLogTail tail(0);
	tail.Add("ignored");
	CHECK(tail.GetText().empty());
}
