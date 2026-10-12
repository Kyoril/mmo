// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Follow-up ranks of a talent spell (e.g. Fireball rank 2 bought from the trainer while rank 1 is
// the Fireball talent) are only usable while the talent is learned. They survive talent resets in
// the persistent known-spell set and come back when the talent is learned again.

#include "catch.hpp"

#include "game/talent_rank_gate.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 ClassId = 1;
	constexpr uint32 TabId = 1;

	constexpr uint32 GatedTalent = 20;
	constexpr uint32 TwoRankTalent = 21;

	constexpr uint32 TalentRankSpell = 600;     // rank 1, the talent spell
	constexpr uint32 FollowUpRank = 601;        // rank 2, baseid 600, bought from a trainer
	constexpr uint32 TwoRankFirst = 620;        // rank 1 of TwoRankTalent
	constexpr uint32 TwoRankSecond = 621;       // rank 2 of TwoRankTalent (talent rank, not trainer)
	constexpr uint32 TwoRankFollowUp = 622;     // trainer rank, baseid 620
	constexpr uint32 PlainRank1 = 610;          // ordinary chain, not a talent
	constexpr uint32 PlainRank2 = 611;          // baseid 610
	constexpr uint32 DanglingRank = 630;        // baseid points at a missing spell

	proto::SpellEntry& AddSpell(proto::Project& project, const uint32 id, const uint32 baseId, const uint32 rank)
	{
		auto* spell = project.spells.add(id);
		spell->set_name("Test Spell");
		spell->set_baseid(baseId);
		spell->set_rank(rank);
		// Learning a spell reads both attribute words.
		spell->add_attributes(0);
		spell->add_attributes(0);
		return *spell;
	}

	struct GateCtx
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;

		void Init()
		{
			auto* tab = project.talentTabs.add(TabId);
			tab->set_name("Test");
			tab->set_class_id(ClassId);

			AddSpell(project, TalentRankSpell, TalentRankSpell, 1);
			AddSpell(project, FollowUpRank, TalentRankSpell, 2);
			AddSpell(project, TwoRankFirst, TwoRankFirst, 1);
			AddSpell(project, TwoRankSecond, 0, 0);
			AddSpell(project, TwoRankFollowUp, TwoRankFirst, 2);
			AddSpell(project, PlainRank1, PlainRank1, 1);
			AddSpell(project, PlainRank2, PlainRank1, 2);
			AddSpell(project, DanglingRank, 9999, 2);

			auto* gated = project.talents.add(GatedTalent);
			gated->set_tab(TabId);
			gated->set_row(0);
			gated->set_column(0);
			gated->add_ranks(TalentRankSpell);

			auto* twoRank = project.talents.add(TwoRankTalent);
			twoRank->set_tab(TabId);
			twoRank->set_row(0);
			twoRank->set_column(1);
			twoRank->add_ranks(TwoRankFirst);
			twoRank->add_ranks(TwoRankSecond);
		}

		std::shared_ptr<GamePlayerS> MakePlayer()
		{
			auto* cls = project.classes.getById(ClassId);
			if (!cls)
			{
				cls = project.classes.add(ClassId);
				cls->set_powertype(proto::ClassEntry_PowerType_MANA);
				for (uint32 i = 0; i < 3; ++i)
				{
					auto* values = cls->add_levelbasevalues();
					values->set_health(100);
					values->set_mana(100);
					values->set_stamina(10);
					values->set_strength(10);
					values->set_agility(10);
					values->set_intellect(10);
					values->set_spirit(10);
					values->set_talentpoints(3);
				}
			}

			static uint64 nextGuid = 0x2000;
			auto player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			// Unlearning spells removes their auras by caster guid, which must not be 0.
			player->Set<uint64>(object_fields::Guid, ++nextGuid);
			player->SetClass(*cls);
			player->SetLevel(2);
			player->Set<uint32>(object_fields::TalentPoints, 5);
			return player;
		}

		const proto::SpellEntry& Spell(const uint32 id) const
		{
			const auto* spell = project.spells.getById(id);
			REQUIRE(spell != nullptr);
			return *spell;
		}
	};
}

TEST_CASE("A trainer rank whose base spell is a talent rank is gated by that talent", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	const auto* talent = FindGatingTalent(ctx.Spell(FollowUpRank), ctx.project.spells, ctx.project.talents);
	REQUIRE(talent != nullptr);
	CHECK(talent->id() == GatedTalent);
}

TEST_CASE("Spells outside a talent rank chain are not gated", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	CHECK(FindGatingTalent(ctx.Spell(TalentRankSpell), ctx.project.spells, ctx.project.talents) == nullptr);
	CHECK(FindGatingTalent(ctx.Spell(PlainRank1), ctx.project.spells, ctx.project.talents) == nullptr);
	CHECK(FindGatingTalent(ctx.Spell(PlainRank2), ctx.project.spells, ctx.project.talents) == nullptr);
	CHECK(FindGatingTalent(ctx.Spell(DanglingRank), ctx.project.spells, ctx.project.talents) == nullptr);
	CHECK(FindGatingTalent(ctx.Spell(TwoRankSecond), ctx.project.spells, ctx.project.talents) == nullptr);
}

TEST_CASE("A known follow-up rank stays inactive without its talent", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	player->SetKnownSpells({ FollowUpRank });
	CHECK_FALSE(player->HasSpell(FollowUpRank));
	CHECK(player->GetKnownSpellIds().contains(FollowUpRank));
	CHECK_FALSE(player->IsTalentGateSatisfied(ctx.Spell(FollowUpRank)));
}

TEST_CASE("Learning the talent activates a known follow-up rank", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	player->SetKnownSpells({ FollowUpRank });
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	CHECK(player->HasSpell(FollowUpRank));
	CHECK(player->IsTalentGateSatisfied(ctx.Spell(FollowUpRank)));
}

TEST_CASE("A talent reset deactivates follow-up ranks but keeps them known", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	player->AddSpell(FollowUpRank);
	REQUIRE(player->HasSpell(FollowUpRank));

	player->ResetTalents();
	CHECK_FALSE(player->HasSpell(FollowUpRank));
	CHECK(player->GetKnownSpellIds().contains(FollowUpRank));
}

TEST_CASE("Learning the talent again restores follow-up ranks", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	player->AddSpell(FollowUpRank);
	player->ResetTalents();

	REQUIRE(player->LearnTalent(GatedTalent, 0));
	CHECK(player->HasSpell(FollowUpRank));
}

TEST_CASE("Follow-up ranks survive save and load and wait for the talents", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	player->AddSpell(FollowUpRank);
	const auto& known = player->GetKnownSpellIds();
	const std::vector<uint32> saved(known.begin(), known.end());

	// Login order on the world server: known spells first, talents on spawn.
	auto loaded = ctx.MakePlayer();
	loaded->SetKnownSpells(saved);
	CHECK_FALSE(loaded->HasSpell(FollowUpRank));
	loaded->InitializeTalents({ { GatedTalent, 0 } });
	CHECK(loaded->HasSpell(FollowUpRank));
}

TEST_CASE("The class switch sequence re-activates follow-up ranks", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	player->AddSpell(FollowUpRank);

	// ChangeClass resets talents, then restores the target class's saved talents.
	player->ResetTalents();
	player->InitializeTalents({ { GatedTalent, 0 } });
	CHECK(player->HasSpell(FollowUpRank));
}

TEST_CASE("A follow-up rank stays active when its talent advances to a higher rank", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(TwoRankTalent, 0));
	player->AddSpell(TwoRankFollowUp);
	REQUIRE(player->LearnTalent(TwoRankTalent, 1));
	CHECK_FALSE(player->HasSpell(TwoRankFirst));
	CHECK(player->HasSpell(TwoRankFollowUp));
}

TEST_CASE("Saved talents that no longer fit leave follow-up ranks inactive but known", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	player->SetKnownSpells({ FollowUpRank });
	// Rank 5 does not exist: InitializeTalents resets all talents.
	player->InitializeTalents({ { GatedTalent, 5 } });
	CHECK_FALSE(player->HasSpell(FollowUpRank));
	CHECK(player->GetKnownSpellIds().contains(FollowUpRank));
}

TEST_CASE("Ordinary rank chains do not depend on talents", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	player->SetKnownSpells({ PlainRank1, PlainRank2 });
	CHECK(player->HasSpell(PlainRank2));
	CHECK(player->IsTalentGateSatisfied(ctx.Spell(PlainRank2)));
	player->ResetTalents();
	CHECK(player->HasSpell(PlainRank2));
}
