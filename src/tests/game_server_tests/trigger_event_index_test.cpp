// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "shared/proto_data/project.h"
#include "shared/proto_data/trigger_helper.h"
#include "shared/proto_data/trigger_event_index.h"

using namespace mmo;

namespace
{
	/// Adds a trigger listening for the given events. Passing no flags leaves it a plain
	/// creature/object trigger, which the player index must ignore.
	proto::TriggerEntry& AddTrigger(proto::Project& project, const uint32 id, const uint32 flags,
		const std::initializer_list<uint32> events)
	{
		auto* entry = project.triggers.add(id);
		REQUIRE(entry != nullptr);
		entry->set_name("Trigger " + std::to_string(id));
		entry->set_flags(flags);

		for (const uint32 eventType : events)
		{
			entry->add_newevents()->set_type(eventType);
		}

		return *entry;
	}
}

TEST_CASE("Player trigger index only contains triggers flagged as player triggers", "[player_triggers]")
{
	proto::Project project;
	AddTrigger(project, 1, trigger_flags::PlayerTrigger, { trigger_event::OnPlayerLevelUp });
	AddTrigger(project, 2, trigger_flags::None, { trigger_event::OnPlayerLevelUp });
	AddTrigger(project, 3, trigger_flags::AbortOnOwnerDeath, { trigger_event::OnPlayerLevelUp });
	project.RebuildPlayerTriggerIndex();

	const auto& triggers = project.GetPlayerTriggers(trigger_event::OnPlayerLevelUp);
	REQUIRE(triggers.size() == 1);
	CHECK(triggers[0]->id() == 1);
}

TEST_CASE("Player trigger index buckets triggers by the event they listen for", "[player_triggers]")
{
	proto::Project project;
	AddTrigger(project, 1, trigger_flags::PlayerTrigger, { trigger_event::OnPlayerLevelUp });
	AddTrigger(project, 2, trigger_flags::PlayerTrigger, { trigger_event::OnKilled });
	// A trigger may listen for several different events and belongs in each bucket.
	AddTrigger(project, 3, trigger_flags::PlayerTrigger, { trigger_event::OnPlayerLevelUp, trigger_event::OnKilled });
	project.RebuildPlayerTriggerIndex();

	const auto& levelUp = project.GetPlayerTriggers(trigger_event::OnPlayerLevelUp);
	REQUIRE(levelUp.size() == 2);
	CHECK(levelUp[0]->id() == 1);
	CHECK(levelUp[1]->id() == 3);

	const auto& killed = project.GetPlayerTriggers(trigger_event::OnKilled);
	REQUIRE(killed.size() == 2);
	CHECK(killed[0]->id() == 2);
	CHECK(killed[1]->id() == 3);
}

TEST_CASE("Player trigger index lists a trigger once per event type", "[player_triggers]")
{
	// Two events of the same type is how a designer expresses "fire at level 10 or level 20".
	// The trigger still runs at most once per raise, so it must appear in the bucket once.
	proto::Project project;
	AddTrigger(project, 1, trigger_flags::PlayerTrigger,
		{ trigger_event::OnPlayerLevelUp, trigger_event::OnPlayerLevelUp });
	project.RebuildPlayerTriggerIndex();

	CHECK(project.GetPlayerTriggers(trigger_event::OnPlayerLevelUp).size() == 1);
}

TEST_CASE("Player trigger index returns an empty bucket for events nothing listens for", "[player_triggers]")
{
	proto::Project project;
	AddTrigger(project, 1, trigger_flags::PlayerTrigger, { trigger_event::OnPlayerLevelUp });
	project.RebuildPlayerTriggerIndex();

	CHECK(project.GetPlayerTriggers(trigger_event::OnKilled).empty());
	CHECK(project.GetPlayerTriggers(trigger_event::OnSpawn).empty());
}

TEST_CASE("Player trigger index ignores out of range and unknown events", "[player_triggers]")
{
	proto::Project project;
	// An event value the enum does not cover must not index past the end of the bucket array.
	AddTrigger(project, 1, trigger_flags::PlayerTrigger, { static_cast<uint32>(trigger_event::Count_) + 5 });
	AddTrigger(project, 2, trigger_flags::PlayerTrigger, { trigger_event::OnPlayerLevelUp });
	project.RebuildPlayerTriggerIndex();

	REQUIRE(project.GetPlayerTriggers(trigger_event::OnPlayerLevelUp).size() == 1);
	CHECK(project.GetPlayerTriggers(trigger_event::Invalid).empty());
	CHECK(project.GetPlayerTriggers(static_cast<trigger_event::Type>(trigger_event::Count_ + 5)).empty());
}

TEST_CASE("Rebuilding the player trigger index replaces the previous contents", "[player_triggers]")
{
	proto::Project project;
	AddTrigger(project, 1, trigger_flags::PlayerTrigger, { trigger_event::OnPlayerLevelUp });
	project.RebuildPlayerTriggerIndex();
	REQUIRE(project.GetPlayerTriggers(trigger_event::OnPlayerLevelUp).size() == 1);

	// Dropping the flag must remove the trigger from the index, not just fail to add it again.
	project.triggers.getTemplates().mutable_entry(0)->set_flags(trigger_flags::None);
	project.RebuildPlayerTriggerIndex();
	CHECK(project.GetPlayerTriggers(trigger_event::OnPlayerLevelUp).empty());
}

// ---------------------------------------------------------------------------------------------
// TriggerEventIndex itself. Both the player index and a world instance's own index are built on
// it, so its bucketing rules are tested once here rather than through each owner.
// ---------------------------------------------------------------------------------------------

TEST_CASE("TriggerEventIndex buckets a trigger under every event it listens for", "[trigger_index]")
{
	proto::Project project;
	auto& a = AddTrigger(project, 1, trigger_flags::None, { trigger_event::OnSpawn, trigger_event::OnKilled });
	auto& b = AddTrigger(project, 2, trigger_flags::None, { trigger_event::OnKilled });

	proto::TriggerEventIndex index;
	index.Add(a);
	index.Add(b);

	REQUIRE(index.Get(trigger_event::OnSpawn).size() == 1);
	CHECK(index.Get(trigger_event::OnSpawn)[0]->id() == 1);
	REQUIRE(index.Get(trigger_event::OnKilled).size() == 2);
	CHECK(index.Get(trigger_event::OnDamaged).empty());
	CHECK_FALSE(index.IsEmpty());
}

TEST_CASE("TriggerEventIndex lists a trigger once per event type", "[trigger_index]")
{
	proto::Project project;
	auto& entry = AddTrigger(project, 1, trigger_flags::None, { trigger_event::OnTimer, trigger_event::OnTimer });

	proto::TriggerEventIndex index;
	index.Add(entry);

	CHECK(index.Get(trigger_event::OnTimer).size() == 1);
}

TEST_CASE("TriggerEventIndex ignores events outside the enum", "[trigger_index]")
{
	proto::Project project;
	auto& entry = AddTrigger(project, 1, trigger_flags::None, { static_cast<uint32>(trigger_event::Count_) + 3 });

	proto::TriggerEventIndex index;
	index.Add(entry);

	CHECK(index.IsEmpty());
	CHECK(index.Get(static_cast<trigger_event::Type>(trigger_event::Count_ + 3)).empty());
}

TEST_CASE("TriggerEventIndex Clear empties every bucket", "[trigger_index]")
{
	proto::Project project;
	auto& entry = AddTrigger(project, 1, trigger_flags::None, { trigger_event::OnSpawn });

	proto::TriggerEventIndex index;
	index.Add(entry);
	REQUIRE_FALSE(index.IsEmpty());

	index.Clear();
	CHECK(index.IsEmpty());
	CHECK(index.Get(trigger_event::OnSpawn).empty());
}
