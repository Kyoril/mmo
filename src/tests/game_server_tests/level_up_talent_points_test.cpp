// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/objects/game_player_s.h"
#include "test_unit_factory.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <vector>

using namespace mmo;

// Talent points are granted by the CLASS level curve (see proto_data/classes.proto:
// ClassLevelEntry.talentpoints and GamePlayerS::UpdateTotalTalentPoints), not by the character
// level. The level-up notification still reported the character level's levelbasevalues
// talentPoints, so the client printed "You have gained 1 talent point." on a character level up
// that granted none, while the class level up that actually granted them said nothing.

namespace
{
	/// Records the talent point counts the character level-up notification carries.
	struct LevelUpWatcher final : NetUnitWatcherS
	{
		std::vector<int32> talentPoints;

		void OnLevelUp(uint32, int32, int32, const std::array<int32, MaxAttributes>&, const int32 talentPointsGained, int32) override
		{
			talentPoints.push_back(talentPointsGained);
		}

		// Pure virtuals from NetUnitWatcherS we don't need — stub them out.
		void OnTeleport(uint32, const Vector3&, const Radian&) override {}
		void OnAttackSwingEvent(AttackSwingEvent) override {}
		void OnXpLog(uint32) override {}
		void OnSpellDamageLog(uint64, uint32, uint8, DamageFlags, const proto::SpellEntry&, uint32) override {}
		void OnNonSpellDamageLog(uint64, uint32, DamageFlags) override {}
		void OnEnvironmentalDamageLog(uint64, uint32, EnvironmentalDamageType) override {}
		void OnSpeedChangeApplied(MovementType, float, uint32) override {}
		void OnRootChanged(bool, uint32) override {}
		void OnStunChanged(bool, uint32) override {}
		void OnSleepChanged(bool, uint32) override {}
		void OnFearChanged(bool, uint32) override {}
		void OnDisorientChanged(bool, uint32) override {}
		void OnPendingCharge(float, uint32) override {}
		void OnSpellModChanged(SpellModType, uint8, SpellModOp, int32) override {}
		void OnProficiencyChanged(uint32, bool) override {}
		void OnReviveOffer(uint64, uint32, uint32, uint32, const Vector3&, const Radian&) override {}
	};

	/// Records the class XP notifications, which announce a class rank up.
	struct ClassXpWatcher final : NetPlayerWatcher
	{
		struct Entry
		{
			uint8 classLevel;
			bool leveledUp;
			uint32 talentPointsGained;
		};

		std::vector<Entry> entries;

		void OnClassXpGained(uint32, uint32, const uint8 classLevel, uint32, uint32, const bool leveledUp, const uint32 talentPointsGained) override
		{
			entries.push_back(Entry{ classLevel, leveledUp, talentPointsGained });
		}

		// Pure virtuals from NetPlayerWatcher we don't need — stub them out.
		void OnQuestKillCredit(const proto::QuestEntry&, uint64, uint32, uint32, uint32) override {}
		void OnQuestItemCredit(const proto::QuestEntry&, uint32, uint32, uint32) override {}
		void OnQuestObjectRequirementMet(uint32) override {}
		void OnQuestDataChanged(uint32, const QuestStatusData&) override {}
		void OnQuestCompleted(uint64, uint32, uint32, uint32) override {}
		void OnItemAdded(uint16, uint16, bool, bool) override {}
		void OnObjectLoot() override {}
	};

	/// Builds a class that grants talent points on BOTH curves, so a wrong source is visible:
	/// one per character level (legacy data) and two per class level (the live source).
	proto::ClassEntry* MakeTalentPointClass(proto::Project& project)
	{
		proto::ClassEntry* cls = project.classes.add(1);
		cls->set_powertype(proto::ClassEntry_PowerType_MANA);

		for (uint32 i = 0; i < 3; ++i)
		{
			auto* levelValues = cls->add_levelbasevalues();
			levelValues->set_health(100);
			levelValues->set_mana(100);
			levelValues->add_attributes(10);
			levelValues->add_attributes(10);
			levelValues->add_attributes(10);
			levelValues->add_attributes(10);
			levelValues->add_attributes(10);
			levelValues->set_attributepoints(5);
			levelValues->set_talentpoints(1);

			cls->add_xptonextlevel(100);

			auto* classLevel = cls->add_classlevels();
			classLevel->set_xptonextlevel(100);
			classLevel->set_talentpoints(2);
		}

		return cls;
	}
}

TEST_CASE("A character level up does not claim talent points the class level curve grants", "[level_up_talent_points]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	MakeTalentPointClass(project);

	const auto player = test::MakeUnit(project, timers);

	LevelUpWatcher watcher;
	player->SetNetUnitWatcher(&watcher);

	player->RewardExperience(100);

	REQUIRE(player->GetLevel() == 2);
	REQUIRE(watcher.talentPoints.size() == 1);
	CHECK(watcher.talentPoints.front() == 0);
}

TEST_CASE("A class rank up reports the talent points it granted", "[level_up_talent_points]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	MakeTalentPointClass(project);

	const auto player = test::MakeUnit(project, timers);

	ClassXpWatcher watcher;
	player->SetPlayerWatcher(&watcher);

	// Not enough class XP for a rank up yet: nothing was granted.
	player->RewardClassExperience(50);
	REQUIRE(watcher.entries.size() == 1);
	CHECK_FALSE(watcher.entries.front().leveledUp);
	CHECK(watcher.entries.front().talentPointsGained == 0);

	// This crosses the class rank 1 -> 2 threshold, which grants two talent points.
	player->RewardClassExperience(50);
	REQUIRE(watcher.entries.size() == 2);
	CHECK(watcher.entries.back().classLevel == 2);
	CHECK(watcher.entries.back().leveledUp);
	CHECK(watcher.entries.back().talentPointsGained == 2);
}

TEST_CASE("Without a class level curve the character level keeps granting talent points", "[level_up_talent_points]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;

	// Legacy class data: no classlevels, so UpdateTotalTalentPoints falls back to the character
	// level base values and the character level up is the correct place for the notice.
	proto::ClassEntry* cls = MakeTalentPointClass(project);
	cls->clear_classlevels();

	const auto player = test::MakeUnit(project, timers);

	LevelUpWatcher watcher;
	player->SetNetUnitWatcher(&watcher);

	player->RewardExperience(100);

	REQUIRE(player->GetLevel() == 2);
	REQUIRE(watcher.talentPoints.size() == 1);
	CHECK(watcher.talentPoints.front() == 1);
}
