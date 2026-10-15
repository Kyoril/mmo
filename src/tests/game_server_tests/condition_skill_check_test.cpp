// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/condition_mgr.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/classes.pb.h"
#include "shared/proto_data/conditions.pb.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 SkillId = 1;

	/// Adds a SKILL_CHECK condition to the project.
	proto::Condition* AddSkillCondition(proto::Project& project, uint32 id, uint32 minValue, uint32 maxValue)
	{
		auto* cond = project.conditions.add(id);
		cond->set_name("Skill check");
		cond->set_conditiontype(proto::Condition_ConditionType_SKILL_CHECK);
		cond->set_param1(SkillId);
		cond->set_param2(minValue);
		cond->set_param3(maxValue);
		return cond;
	}

	/// Builds a minimal player (mirrors godmode_test.cpp).
	std::shared_ptr<GamePlayerS> MakePlayer(proto::Project& project, TimerQueue& timers)
	{
		auto* cls = project.classes.add(1);
		cls->set_powertype(proto::ClassEntry_PowerType_MANA);
		auto* lbv = cls->add_levelbasevalues();
		lbv->set_health(100);
		lbv->set_mana(100);
		lbv->set_stamina(10);
		lbv->set_strength(10);
		lbv->set_agility(10);
		lbv->set_intellect(10);
		lbv->set_spirit(10);
		cls->add_levelbasevalues()->CopyFrom(*lbv);

		auto player = std::make_shared<GamePlayerS>(project, timers);
		player->Initialize();
		player->SetClass(*cls);
		player->SetLevel(1);
		return player;
	}
}

TEST_CASE("Skill condition requires the skill to be known", "[condition][skill]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	AddSkillCondition(project, 1, 1, 0);
	ConditionMgr mgr(project.conditions);
	auto player = MakePlayer(project, timers);

	CHECK_FALSE(mgr.PlayerMeetsCondition(*player, 1));

	player->GetTradeSkills().LearnOrRaise(SkillId, 75);
	CHECK(mgr.PlayerMeetsCondition(*player, 1));
}

TEST_CASE("Skill condition with zero minimum only needs the skill to be known", "[condition][skill]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	AddSkillCondition(project, 1, 0, 0);
	ConditionMgr mgr(project.conditions);
	auto player = MakePlayer(project, timers);

	CHECK_FALSE(mgr.PlayerMeetsCondition(*player, 1));

	player->GetTradeSkills().LearnOrRaise(SkillId, 75);
	CHECK(mgr.PlayerMeetsCondition(*player, 1));
}

TEST_CASE("Skill condition respects the minimum value", "[condition][skill]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	AddSkillCondition(project, 1, 50, 0);
	ConditionMgr mgr(project.conditions);
	auto player = MakePlayer(project, timers);

	player->GetTradeSkills().SetAll({ TradeSkillData{ SkillId, 49, 75 } });
	CHECK_FALSE(mgr.PlayerMeetsCondition(*player, 1));

	player->GetTradeSkills().SetAll({ TradeSkillData{ SkillId, 50, 75 } });
	CHECK(mgr.PlayerMeetsCondition(*player, 1));

	player->GetTradeSkills().SetAll({ TradeSkillData{ SkillId, 75, 75 } });
	CHECK(mgr.PlayerMeetsCondition(*player, 1));
}

TEST_CASE("Skill condition respects the maximum value", "[condition][skill]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	AddSkillCondition(project, 1, 10, 60);
	ConditionMgr mgr(project.conditions);
	auto player = MakePlayer(project, timers);

	player->GetTradeSkills().SetAll({ TradeSkillData{ SkillId, 60, 75 } });
	CHECK(mgr.PlayerMeetsCondition(*player, 1));

	player->GetTradeSkills().SetAll({ TradeSkillData{ SkillId, 61, 75 } });
	CHECK_FALSE(mgr.PlayerMeetsCondition(*player, 1));
}

TEST_CASE("Skill condition ignores other skills", "[condition][skill]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	AddSkillCondition(project, 1, 1, 0);
	ConditionMgr mgr(project.conditions);
	auto player = MakePlayer(project, timers);

	player->GetTradeSkills().LearnOrRaise(SkillId + 1, 75);
	CHECK_FALSE(mgr.PlayerMeetsCondition(*player, 1));
}
