// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "io_poll.h"

using namespace mmo;

TEST_CASE("PollRestarting runs handlers posted after the io object ran out of work", "[bot_io]")
{
	asio::io_service io;

	// No work: this poll leaves the io object stopped, like a bot whose connections all closed.
	io.poll();
	REQUIRE(io.stopped());

	bool ran = false;
	io.post([&ran]() { ran = true; });

	// A plain poll() would skip the handler here.
	PollRestarting(io);
	CHECK(ran);
}
