// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "game_client/unit_cast_info.h"

using namespace mmo;

namespace
{
	// UnitCastInfo stores the spell pointer for identity only and never dereferences
	// it, so the tests can use the address of an unrelated dummy object.
	const proto_client::SpellEntry* DummySpell()
	{
		static int dummy = 0;
		return reinterpret_cast<const proto_client::SpellEntry*>(&dummy);
	}
}

TEST_CASE("UnitCastInfo is idle by default", "[unit_cast_info]")
{
	const UnitCastInfo info;
	CHECK_FALSE(info.IsActive());
	CHECK_FALSE(info.IsInterruptFlashActive(123456));
}

TEST_CASE("BeginCast activates and progress fills over time", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 7, 1000, 2000);

	REQUIRE(info.IsActive());
	CHECK(info.spell == DummySpell());
	CHECK(info.spellId == 7);
	CHECK_FALSE(info.channeling);
	CHECK(info.GetProgress(1000) == Approx(0.0f));
	CHECK(info.GetProgress(2000) == Approx(0.5f));
	CHECK(info.GetProgress(3000) == Approx(1.0f));
	// Clamped past the end.
	CHECK(info.GetProgress(9000) == Approx(1.0f));
}

TEST_CASE("BeginChannel drains from full to empty", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginChannel(*DummySpell(), 7, 1000, 4000);

	REQUIRE(info.IsActive());
	CHECK(info.channeling);
	CHECK(info.spellId == 7);
	CHECK(info.GetProgress(1000) == Approx(1.0f));
	CHECK(info.GetProgress(3000) == Approx(0.5f));
	CHECK(info.GetProgress(5000) == Approx(0.0f));
	CHECK(info.GetProgress(9000) == Approx(0.0f));
}

TEST_CASE("UpdateChannel rebases the end time (pushback) and zero ends the channel", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginChannel(*DummySpell(), 7, 1000, 4000);

	// Pushback: at t=2000 the server says only 1000ms remain.
	info.UpdateChannel(2000, 1000);
	CHECK(info.endTime == 3000);
	REQUIRE(info.IsActive());

	// timeLeft == 0 is the server's normal channel-end signal: clears without flash.
	info.UpdateChannel(2500, 0);
	CHECK_FALSE(info.IsActive());
	CHECK_FALSE(info.IsInterruptFlashActive(2500));
}

TEST_CASE("UpdateChannel on an idle unit is a no-op", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.UpdateChannel(2000, 1000);
	CHECK_FALSE(info.IsActive());
}

TEST_CASE("FinishSucceeded clears without an interrupt flash", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 7, 1000, 2000);
	info.FinishSucceeded(7);

	CHECK_FALSE(info.IsActive());
	CHECK(info.spell == nullptr);
	CHECK_FALSE(info.IsInterruptFlashActive(1500));
}

TEST_CASE("SpellGo at channel start does not clear the channel", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginChannel(*DummySpell(), 7, 1000, 4000);

	// SpellGo for the same spell id arrives right as the channel starts (same
	// flush as ChannelStart); it must not clear the just-started channel.
	info.FinishSucceeded(7);
	REQUIRE(info.IsActive());
	CHECK(info.channeling);
	CHECK(info.GetProgress(3000) == Approx(0.5f));

	// The channel only ends via the regular UpdateChannel(0) end-of-channel signal.
	info.UpdateChannel(3000, 0);
	CHECK_FALSE(info.IsActive());
	CHECK_FALSE(info.IsInterruptFlashActive(3000));
}

TEST_CASE("SpellGo for a different spell does not clear a tracked cast", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 7, 1000, 2000);

	// SpellGo broadcast for an unrelated instant/proc cast by the same unit.
	info.FinishSucceeded(9);

	REQUIRE(info.IsActive());
	CHECK(info.spellId == 7);
}

TEST_CASE("SpellFailure for a different spell neither clears nor flashes", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 7, 1000, 2000);

	info.FinishFailed(1500, 9);

	REQUIRE(info.IsActive());
	CHECK(info.spellId == 7);
	CHECK_FALSE(info.IsInterruptFlashActive(1500));
}

TEST_CASE("FinishFailed on an active cast triggers a time-limited interrupt flash", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 7, 1000, 2000);
	info.FinishFailed(1500, 7);

	CHECK_FALSE(info.IsActive());
	CHECK(info.IsInterruptFlashActive(1500));
	CHECK(info.IsInterruptFlashActive(1500 + UnitCastInfo::InterruptFlashDurationMs - 1));
	CHECK_FALSE(info.IsInterruptFlashActive(1500 + UnitCastInfo::InterruptFlashDurationMs));
}

TEST_CASE("FinishFailed without an active cast does not flash", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.FinishFailed(1500, 7);
	CHECK_FALSE(info.IsInterruptFlashActive(1500));
}

TEST_CASE("Starting a new cast clears a pending interrupt flash", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 7, 1000, 2000);
	info.FinishFailed(1500, 7);
	info.BeginCast(*DummySpell(), 8, 1600, 2000);

	REQUIRE(info.IsActive());
	CHECK_FALSE(info.IsInterruptFlashActive(1600));
}

TEST_CASE("Zero cast time yields full progress instead of dividing by zero", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 7, 1000, 0);
	CHECK(info.GetProgress(1000) == Approx(1.0f));
}

TEST_CASE("GetProgress before startTime returns the start-of-bar value", "[unit_cast_info]")
{
	UnitCastInfo cast;
	cast.BeginCast(*DummySpell(), 7, 1000, 2000);
	CHECK(cast.GetProgress(500) == Approx(0.0f));

	UnitCastInfo channel;
	channel.BeginChannel(*DummySpell(), 7, 1000, 4000);
	CHECK(channel.GetProgress(500) == Approx(1.0f));
}
