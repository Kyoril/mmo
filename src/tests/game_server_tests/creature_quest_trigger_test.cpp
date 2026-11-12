// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// A quest giver's OnQuestAccept / OnQuestReward triggers name the quest they react to. The
// creature has to honour that filter: Malrik Duskwright binds an acolyte's shade when quest 77
// is turned in at him, and without the filter every other quest he ends granted the shade too.

#include "catch.hpp"

#include "game_server/objects/game_creature_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/trigger_helper.h"
#include "asio/io_service.hpp"

#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	struct QuestTriggerFixture
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		proto::UnitEntry* entry = nullptr;
		std::shared_ptr<GameCreatureS> creature;
		std::vector<uint32> fired;
		scoped_connection connection;

		QuestTriggerFixture()
		{
			entry = project.units.add(1);
			entry->set_name("Quest Giver");
			entry->set_minlevel(1);
			entry->set_maxlevel(1);
		}

		/// Gives the quest giver a trigger reacting to `event`, filtered to `questId` (0 = any quest).
		void AddTrigger(const uint32 id, const trigger_event::Type event, const uint32 questId)
		{
			auto* trigger = project.triggers.add(id);
			trigger->set_id(id);
			auto* triggerEvent = trigger->add_newevents();
			triggerEvent->set_type(event);
			triggerEvent->add_data(questId);
			entry->add_triggers(id);
		}

		void Spawn()
		{
			creature = std::make_shared<GameCreatureS>(project, timers, *entry);
			creature->Initialize();
			creature->SetEntry(*entry);
			connection = creature->unitTrigger.connect([this](const proto::TriggerEntry& trigger, GameUnitS&, GameUnitS*)
			{
				fired.push_back(trigger.id());
			});
		}
	};
}

TEST_CASE_METHOD(QuestTriggerFixture, "A quest reward trigger fires only for its own quest", "[trigger_filter]")
{
	AddTrigger(1, trigger_event::OnQuestReward, 77);
	Spawn();

	creature->RaiseTrigger(trigger_event::OnQuestReward, { 72 }, nullptr);
	CHECK(fired.empty());

	creature->RaiseTrigger(trigger_event::OnQuestReward, { 77 }, nullptr);
	CHECK(fired == std::vector<uint32>{ 1 });
}

TEST_CASE_METHOD(QuestTriggerFixture, "A quest accept trigger fires only for its own quest", "[trigger_filter]")
{
	AddTrigger(1, trigger_event::OnQuestAccept, 75);
	Spawn();

	creature->RaiseTrigger(trigger_event::OnQuestAccept, { 76 }, nullptr);
	CHECK(fired.empty());

	creature->RaiseTrigger(trigger_event::OnQuestAccept, { 75 }, nullptr);
	CHECK(fired == std::vector<uint32>{ 1 });
}

TEST_CASE_METHOD(QuestTriggerFixture, "A quest trigger without a quest id reacts to every quest", "[trigger_filter]")
{
	AddTrigger(1, trigger_event::OnQuestReward, 0);
	Spawn();

	creature->RaiseTrigger(trigger_event::OnQuestReward, { 72 }, nullptr);
	creature->RaiseTrigger(trigger_event::OnQuestReward, { 77 }, nullptr);
	CHECK(fired.size() == 2);
}
