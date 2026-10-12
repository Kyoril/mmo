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
