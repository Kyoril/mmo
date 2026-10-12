// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/world/spell_zone_set.h"

#include <vector>

using namespace mmo;

namespace
{
	SpellZone MakeZone(const GameTime createdAt, const GameTime duration, const GameTime interval)
	{
		SpellZone zone;
		zone.casterGuid = 42;
		zone.spellId = 253;
		zone.triggerSpellId = 254;
		zone.position = Vector3(10.0f, 1.0f, -4.0f);
		zone.radius = 3.0f;
		zone.expiresAt = createdAt + duration;
		zone.tickInterval = interval;
		zone.nextTickAt = createdAt + interval;
		return zone;
	}

	struct Recorder
	{
		std::vector<GameTime> ticks;
		std::vector<uint32> expired;
		GameTime now = 0;

		void Advance(SpellZoneSet& set, const GameTime time)
		{
			now = time;
			set.Advance(time,
				[this](const SpellZone& zone) { ticks.push_back(zone.nextTickAt); },
				[this](const SpellZone& zone) { expired.push_back(zone.id); });
		}
	};
}

// Guttering Candle and Dissonance are built on this: warning for 2 s, then exactly one hit.
TEST_CASE("A zone whose interval equals its duration ticks once, at expiry", "[spell_zone]")
{
	SpellZoneSet set;
	const uint32 id = set.Add(MakeZone(1000, 2000, 2000));
	Recorder recorder;

	recorder.Advance(set, 2000);
	recorder.Advance(set, 2990);
	CHECK(recorder.ticks.empty());
	CHECK(set.Size() == 1);

	recorder.Advance(set, 3010);
	REQUIRE(recorder.ticks.size() == 1);
	CHECK(recorder.ticks[0] == 3000);
	REQUIRE(recorder.expired.size() == 1);
	CHECK(recorder.expired[0] == id);
	CHECK(set.Size() == 0);
}

// Silent Place: a lingering zone pulses on a fixed beat for its whole life.
TEST_CASE("A lingering zone ticks on every interval, including a late frame catching up", "[spell_zone]")
{
	SpellZoneSet set;
	set.Add(MakeZone(0, 5000, 1000));
	Recorder recorder;

	recorder.Advance(set, 1500);
	CHECK(recorder.ticks == std::vector<GameTime>{ 1000 });

	// A hitch: three intervals pass in one advance. None may be skipped.
	recorder.Advance(set, 4200);
	CHECK(recorder.ticks == std::vector<GameTime>{ 1000, 2000, 3000, 4000 });
	CHECK(recorder.expired.empty());

	recorder.Advance(set, 9000);
	CHECK(recorder.ticks == std::vector<GameTime>{ 1000, 2000, 3000, 4000, 5000 });
	CHECK(recorder.expired.size() == 1);
}

TEST_CASE("A zone without an interval never ticks but still expires", "[spell_zone]")
{
	SpellZoneSet set;
	set.Add(MakeZone(0, 1000, 0));
	Recorder recorder;

	recorder.Advance(set, 5000);
	CHECK(recorder.ticks.empty());
	CHECK(recorder.expired.size() == 1);
}

TEST_CASE("RemoveIf drops zones without ticking or expiring them", "[spell_zone]")
{
	SpellZoneSet set;
	SpellZone other = MakeZone(0, 5000, 1000);
	other.casterGuid = 7;
	set.Add(MakeZone(0, 5000, 1000));
	set.Add(other);

	std::vector<uint64> removedCasters;
	set.RemoveIf([](const SpellZone& zone) { return zone.casterGuid == 42; },
		[&removedCasters](const SpellZone& zone) { removedCasters.push_back(zone.casterGuid); });

	CHECK(removedCasters == std::vector<uint64>{ 42 });
	REQUIRE(set.Size() == 1);
	CHECK(set.GetZones()[0].casterGuid == 7);
}

TEST_CASE("A zone created from inside a tick survives the advance that created it", "[spell_zone]")
{
	SpellZoneSet set;
	set.Add(MakeZone(0, 1000, 1000));

	bool spawned = false;
	set.Advance(2000,
		[&](const SpellZone&)
		{
			if (!spawned)
			{
				spawned = true;
				set.Add(MakeZone(2000, 3000, 1000));
			}
		},
		[](const SpellZone&) {});

	REQUIRE(set.Size() == 1);
	CHECK(set.GetZones()[0].expiresAt == 5000);
}

TEST_CASE("IsInsideSpellZone is a planar circle test", "[spell_zone]")
{
	const SpellZone zone = MakeZone(0, 1000, 0);
	CHECK(IsInsideSpellZone(zone, Vector3(12.9f, 1.0f, -4.0f)));
	CHECK(IsInsideSpellZone(zone, Vector3(10.0f, 40.0f, -1.1f)));
	CHECK_FALSE(IsInsideSpellZone(zone, Vector3(13.1f, 1.0f, -4.0f)));
	CHECK(IsInsideSpellZone(zone, Vector3(12.0f, 1.0f, -2.0f)));
	CHECK_FALSE(IsInsideSpellZone(zone, Vector3(12.2f, 1.0f, -1.8f)));
}
