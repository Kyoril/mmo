// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "shared/proto_data/trigger_event_filter.h"
#include "shared/proto_data/trigger_helper.h"

using namespace mmo;

namespace
{
	/// Builds a trigger event with the given filter data.
	proto::TriggerEvent MakeEvent(const std::initializer_list<uint32> filter)
	{
		proto::TriggerEvent triggerEvent;
		triggerEvent.set_type(trigger_event::OnPlayerLevelUp);

		for (const uint32 value : filter)
		{
			triggerEvent.add_data(value);
		}

		return triggerEvent;
	}
}

TEST_CASE("An event with no filter data matches any raise", "[trigger_filter]")
{
	const auto triggerEvent = MakeEvent({});

	CHECK(proto::TriggerEventDataMatches(triggerEvent, {}));
	CHECK(proto::TriggerEventDataMatches(triggerEvent, { 1 }));
	CHECK(proto::TriggerEventDataMatches(triggerEvent, { 42, 7 }));
}

TEST_CASE("A configured value must equal the raised value at the same index", "[trigger_filter]")
{
	const auto triggerEvent = MakeEvent({ 10 });

	CHECK(proto::TriggerEventDataMatches(triggerEvent, { 10 }));
	CHECK_FALSE(proto::TriggerEventDataMatches(triggerEvent, { 9 }));
	CHECK_FALSE(proto::TriggerEventDataMatches(triggerEvent, { 11 }));
}

TEST_CASE("A configured zero is a wildcard for its slot", "[trigger_filter]")
{
	// "any slot, state 2" — the shape OnEncounterStateChanged relies on.
	const auto triggerEvent = MakeEvent({ 0, 2 });

	CHECK(proto::TriggerEventDataMatches(triggerEvent, { 1, 2 }));
	CHECK(proto::TriggerEventDataMatches(triggerEvent, { 99, 2 }));
	CHECK_FALSE(proto::TriggerEventDataMatches(triggerEvent, { 1, 3 }));
}

TEST_CASE("A filter longer than the raised data does not match", "[trigger_filter]")
{
	// The trap this guards: a designer filters on a field the raiser never supplies. Reading
	// past the end would be undefined; the trigger must simply not fire.
	const auto triggerEvent = MakeEvent({ 1, 2, 3 });

	CHECK_FALSE(proto::TriggerEventDataMatches(triggerEvent, {}));
	CHECK_FALSE(proto::TriggerEventDataMatches(triggerEvent, { 1 }));
	CHECK_FALSE(proto::TriggerEventDataMatches(triggerEvent, { 1, 2 }));
	CHECK(proto::TriggerEventDataMatches(triggerEvent, { 1, 2, 3 }));
}

TEST_CASE("A trailing wildcard still matches when the raiser supplies nothing for it", "[trigger_filter]")
{
	// A zero slot is never read, so it must not fail the length check either.
	const auto triggerEvent = MakeEvent({ 5, 0 });

	CHECK(proto::TriggerEventDataMatches(triggerEvent, { 5 }));
	CHECK(proto::TriggerEventDataMatches(triggerEvent, { 5, 123 }));
	CHECK_FALSE(proto::TriggerEventDataMatches(triggerEvent, { 6 }));
}

TEST_CASE("Raised data beyond the filter is ignored", "[trigger_filter]")
{
	const auto triggerEvent = MakeEvent({ 10 });

	CHECK(proto::TriggerEventDataMatches(triggerEvent, { 10, 20, 30 }));
}

TEST_CASE("Level filter behaves as the level-up trigger relies on", "[trigger_filter]")
{
	// The two authoring cases documented for OnPlayerLevelUp.
	const auto anyLevel = MakeEvent({});
	const auto levelTen = MakeEvent({ 10 });

	for (uint32 level = 1; level <= 20; ++level)
	{
		CHECK(proto::TriggerEventDataMatches(anyLevel, { level }));
		CHECK(proto::TriggerEventDataMatches(levelTen, { level }) == (level == 10));
	}
}
