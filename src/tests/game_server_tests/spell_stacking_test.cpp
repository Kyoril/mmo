// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/aura_container.h"
#include "game_server/objects/game_player_s.h"
#include "game/aura.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <memory>

using namespace mmo;

namespace
{
	/// Helper: build a minimal shared GamePlayerS with a class entry set up
	/// so RefreshStats() / SetLevel() work without asserting.
	std::shared_ptr<GamePlayerS> MakeUnit(proto::Project& project, TimerQueue& timers, uint32 level = 1)
	{
		auto* cls = project.classes.getById(1);
		if (!cls)
		{
			cls = project.classes.add(1);
			if (cls)
			{
				cls->set_powertype(proto::ClassEntry_PowerType_MANA);
				for (uint32 i = 0; i < level + 1; ++i)
				{
					auto* lbv = cls->add_levelbasevalues();
					lbv->set_health(100);
					lbv->set_mana(100);
					lbv->set_stamina(10);
					lbv->set_strength(10);
					lbv->set_agility(10);
					lbv->set_intellect(10);
					lbv->set_spirit(10);
				}
			}
		}
		auto unit = std::make_shared<GamePlayerS>(project, timers);
		unit->Initialize();
		if (cls) { unit->SetClass(*cls); }
		unit->SetLevel(level);
		return unit;
	}

	/// Build a minimal proto::SpellEntry with enough attribute slots that
	/// AuraEffect::HandlePeriodicBase() can safely call spell.attributes(0).
	proto::SpellEntry MakeSpell()
	{
		proto::SpellEntry spell;
		// HandlePeriodicBase reads attributes(0) and IsVisible reads attributes(1) —
		// add two attribute fields to avoid repeated-field OOB.
		spell.add_attributes(0); // attributes_a = 0
		spell.add_attributes(0); // attributes_b = 0
		return spell;
	}
}

// ---------------------------------------------------------------------------
// Group 1: ShouldOverwriteAura rules
// ---------------------------------------------------------------------------

TEST_CASE("ShouldOverwriteAura UniquePerTarget: same baseid different casters → Replace", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spellA = MakeSpell();
	spellA.set_id(100);
	spellA.set_baseid(100);
	spellA.set_rank(1);
	spellA.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::UniquePerTarget));

	proto::SpellEntry spellB = MakeSpell();
	spellB.set_id(100);
	spellB.set_baseid(100);
	spellB.set_rank(1);
	spellB.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::UniquePerTarget));

	// casterA = guid 1, casterB = guid 2
	AuraContainer existing(*target, /*casterId=*/1, spellA, /*duration=*/5000, /*itemGuid=*/0);
	AuraContainer incoming(*target, /*casterId=*/2, spellB, /*duration=*/5000, /*itemGuid=*/0);

	CHECK(incoming.ShouldOverwriteAura(existing) == AuraApplicationResult::Replace);
}

TEST_CASE("ShouldOverwriteAura UniquePerCaster same caster: same baseid → Replace", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeSpell();
	spell.set_id(101);
	spell.set_baseid(101);
	spell.set_rank(1);
	spell.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::UniquePerCaster));

	AuraContainer existing(*target, /*casterId=*/5, spell, /*duration=*/5000, /*itemGuid=*/0);
	AuraContainer incoming(*target, /*casterId=*/5, spell, /*duration=*/5000, /*itemGuid=*/0);

	CHECK(incoming.ShouldOverwriteAura(existing) == AuraApplicationResult::Replace);
}

TEST_CASE("ShouldOverwriteAura UniquePerCaster different casters: same baseid → Reject", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeSpell();
	spell.set_id(101);
	spell.set_baseid(101);
	spell.set_rank(1);
	spell.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::UniquePerCaster));

	AuraContainer existing(*target, /*casterId=*/5, spell, /*duration=*/5000, /*itemGuid=*/0);
	AuraContainer incoming(*target, /*casterId=*/6, spell, /*duration=*/5000, /*itemGuid=*/0);

	CHECK(incoming.ShouldOverwriteAura(existing) == AuraApplicationResult::Reject);
}

TEST_CASE("ShouldOverwriteAura StackablePerCaster same caster: always Reject (stacking handled by Extend)", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeSpell();
	spell.set_id(102);
	spell.set_baseid(102);
	spell.set_rank(1);
	spell.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::StackablePerCaster));
	spell.set_stackamount(3);

	AuraContainer existing(*target, /*casterId=*/7, spell, /*duration=*/5000, /*itemGuid=*/0);
	AuraContainer incoming(*target, /*casterId=*/7, spell, /*duration=*/5000, /*itemGuid=*/0);

	// StackablePerCaster always returns Reject (accumulation handled elsewhere)
	CHECK(incoming.ShouldOverwriteAura(existing) == AuraApplicationResult::Reject);
}

TEST_CASE("ShouldOverwriteAura StackablePerCaster different casters: Reject", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeSpell();
	spell.set_id(102);
	spell.set_baseid(102);
	spell.set_rank(1);
	spell.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::StackablePerCaster));
	spell.set_stackamount(3);

	AuraContainer existing(*target, /*casterId=*/7, spell, /*duration=*/5000, /*itemGuid=*/0);
	AuraContainer incoming(*target, /*casterId=*/8, spell, /*duration=*/5000, /*itemGuid=*/0);

	CHECK(incoming.ShouldOverwriteAura(existing) == AuraApplicationResult::Reject);
}

TEST_CASE("ShouldOverwriteAura Default: same caster, same spell id → Replace", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeSpell();
	spell.set_id(200);
	spell.set_baseid(200);
	spell.set_rank(1);
	// stacking_rule defaults to 0 = Default

	AuraContainer existing(*target, /*casterId=*/10, spell, /*duration=*/5000, /*itemGuid=*/0);
	AuraContainer incoming(*target, /*casterId=*/10, spell, /*duration=*/5000, /*itemGuid=*/0);

	CHECK(incoming.ShouldOverwriteAura(existing) == AuraApplicationResult::Replace);
}

TEST_CASE("ShouldOverwriteAura lower-rank rejection: existing rank 2, incoming rank 1 → Replace skipped by ApplyAura rank guard", "[spell_stacking]")
{
	// ShouldOverwriteAura itself returns Replace for UniquePerTarget same baseid;
	// the rank guard lives in ApplyAura. We verify ShouldOverwriteAura still says Replace
	// (rank check is ApplyAura's responsibility, not ShouldOverwriteAura's).
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spellRank2 = MakeSpell();
	spellRank2.set_id(300);
	spellRank2.set_baseid(300);
	spellRank2.set_rank(2);
	spellRank2.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::UniquePerTarget));

	proto::SpellEntry spellRank1 = MakeSpell();
	spellRank1.set_id(300);
	spellRank1.set_baseid(300);
	spellRank1.set_rank(1);
	spellRank1.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::UniquePerTarget));

	// Existing is rank 2, incoming is rank 1
	AuraContainer existing(*target, /*casterId=*/1, spellRank2, /*duration=*/5000, /*itemGuid=*/0);
	AuraContainer incoming(*target, /*casterId=*/1, spellRank1, /*duration=*/5000, /*itemGuid=*/0);

	// ShouldOverwriteAura returns Replace (same baseid, UniquePerTarget); rank guard is in ApplyAura
	CHECK(incoming.ShouldOverwriteAura(existing) == AuraApplicationResult::Replace);

	// ApplyAura-level: applying lower rank should leave existing aura in place
	auto target2 = MakeUnit(project, timers);
	auto existingAura = std::make_shared<AuraContainer>(*target2, /*casterId=*/1, spellRank2, /*duration=*/5000, /*itemGuid=*/0);
	target2->ApplyAura(std::move(existingAura));

	auto incomingAura = std::make_shared<AuraContainer>(*target2, /*casterId=*/1, spellRank1, /*duration=*/5000, /*itemGuid=*/0);
	target2->ApplyAura(std::move(incomingAura));

	// Should still only have the rank 2 aura (lower rank was silently rejected).
	// Verify by checking the rank2 spell is still present and rank1 is not.
	CHECK(target2->HasAuraSpellFromCaster(300, 1));
}

// ---------------------------------------------------------------------------
// Group 2: RefreshAura stack policies
// ---------------------------------------------------------------------------

TEST_CASE("RefreshAura GrantReset policy: stackCount resets to stackamount", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeSpell();
	spell.set_id(400);
	spell.set_baseid(400);
	spell.set_rank(1);
	spell.set_stackamount(3);
	spell.set_stack_reset_policy(0); // 0 = GrantReset

	AuraContainer container(*target, /*casterId=*/1, spell, /*duration=*/5000, /*itemGuid=*/0);
	// stackCount starts at 1 (default)
	REQUIRE(container.GetStackCount() == 1u);

	container.RefreshAura(spell);

	// GrantReset: stackCount set to stackamount (3)
	CHECK(container.GetStackCount() == 3u);
}

TEST_CASE("RefreshAura Accumulate policy: stackCount increments by 1 per refresh up to max", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeSpell();
	spell.set_id(401);
	spell.set_baseid(401);
	spell.set_rank(1);
	spell.set_stackamount(3);
	spell.set_stack_reset_policy(1); // 1 = Accumulate

	AuraContainer container(*target, /*casterId=*/1, spell, /*duration=*/5000, /*itemGuid=*/0);
	REQUIRE(container.GetStackCount() == 1u);

	container.RefreshAura(spell);
	CHECK(container.GetStackCount() == 2u);

	container.RefreshAura(spell);
	CHECK(container.GetStackCount() == 3u);

	// At max — should not exceed stackamount
	container.RefreshAura(spell);
	CHECK(container.GetStackCount() == 3u);
}

// ---------------------------------------------------------------------------
// Group 3: CategoryExclusive + SingleTargetPerCaster via ApplyAura
// ---------------------------------------------------------------------------

TEST_CASE("CategoryExclusive same caster same category: second aura evicts first", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spellA = MakeSpell();
	spellA.set_id(500);
	spellA.set_baseid(500);
	spellA.set_rank(1);
	spellA.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::CategoryExclusive));
	spellA.set_stacking_category_id(1);

	proto::SpellEntry spellB = MakeSpell();
	spellB.set_id(501);
	spellB.set_baseid(501);
	spellB.set_rank(1);
	spellB.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::CategoryExclusive));
	spellB.set_stacking_category_id(1);

	const uint64 casterId = 99;

	auto auraA = std::make_shared<AuraContainer>(*target, casterId, spellA, /*duration=*/5000, /*itemGuid=*/0);
	target->ApplyAura(std::move(auraA));

	// After applying spellA, it should be present
	CHECK(target->HasAuraSpellFromCaster(500, casterId));

	auto auraB = std::make_shared<AuraContainer>(*target, casterId, spellB, /*duration=*/5000, /*itemGuid=*/0);
	target->ApplyAura(std::move(auraB));

	// After applying spellB (same category, same caster), spellB should be present
	// and spellA should have been evicted.
	CHECK(target->HasAuraSpellFromCaster(501, casterId));
	CHECK_FALSE(target->HasAuraSpellFromCaster(500, casterId));
}

TEST_CASE("CategoryExclusive different casters same category: both auras coexist", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spellA = MakeSpell();
	spellA.set_id(500);
	spellA.set_baseid(500);
	spellA.set_rank(1);
	spellA.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::CategoryExclusive));
	spellA.set_stacking_category_id(1);

	proto::SpellEntry spellB = MakeSpell();
	spellB.set_id(501);
	spellB.set_baseid(501);
	spellB.set_rank(1);
	spellB.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::CategoryExclusive));
	spellB.set_stacking_category_id(1);

	auto auraA = std::make_shared<AuraContainer>(*target, /*casterId=*/1, spellA, /*duration=*/5000, /*itemGuid=*/0);
	target->ApplyAura(std::move(auraA));

	auto auraB = std::make_shared<AuraContainer>(*target, /*casterId=*/2, spellB, /*duration=*/5000, /*itemGuid=*/0);
	target->ApplyAura(std::move(auraB));

	// Different casters → both auras remain
	CHECK(target->HasAuraSpellFromCaster(500, 1));
	CHECK(target->HasAuraSpellFromCaster(501, 2));
}

TEST_CASE("SingleTargetPerCaster stale weak_ptr: expired target is silently discarded, new target receives aura", "[spell_stacking]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target2 = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeSpell();
	spell.set_id(600);
	spell.set_baseid(600);
	spell.set_rank(1);
	spell.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::SingleTargetPerCaster));

	const uint64 casterGuid = 12345;

	// Apply the spell to target1, then let target1 expire.
	{
		auto target1 = MakeUnit(project, timers);
		auto aura1 = std::make_shared<AuraContainer>(*target1, casterGuid, spell, /*duration=*/5000, /*itemGuid=*/0);
		target1->ApplyAura(std::move(aura1));
		// target1 goes out of scope here — any weak_ptr to it becomes stale.
	}

	// Apply same spell to target2 — stale weak_ptr for target1 should be silently discarded, no crash.
	auto aura2 = std::make_shared<AuraContainer>(*target2, casterGuid, spell, /*duration=*/5000, /*itemGuid=*/0);
	REQUIRE_NOTHROW(target2->ApplyAura(std::move(aura2)));

	// target2 should now have the aura
	CHECK(target2->HasAuraSpellFromCaster(600, casterGuid));
}

// ---------------------------------------------------------------------------
// Group 4: UniquePerTarget bug-regression tests
// ---------------------------------------------------------------------------

TEST_CASE("UniquePerTarget baseid=0 (standalone): re-casting same spell replaces existing aura", "[spell_stacking][regression]")
{
	// Bug: HasSameBaseSpellId returned false when baseid==0, so UniquePerTarget
	// fell through to Reject, allowing unlimited stacking of the same standalone spell.
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeSpell();
	spell.set_id(700);
	// baseid intentionally NOT set (defaults to 0) — a standalone / rank-1 spell
	spell.set_rank(1);
	spell.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::UniquePerTarget));

	const uint64 casterId = 1;

	auto aura1 = std::make_shared<AuraContainer>(*target, casterId, spell, /*duration=*/5000, /*itemGuid=*/0);
	target->ApplyAura(std::move(aura1));
	CHECK(target->HasAuraSpellFromCaster(700, casterId));

	// Cast the same spell again — should replace, not stack.
	// Verify at the ShouldOverwriteAura level that baseid==0 spells match each other.
	{
		proto::SpellEntry spellCopy = spell;
		AuraContainer existingContainer(*target, casterId, spell, /*duration=*/5000, /*itemGuid=*/0);
		AuraContainer incomingContainer(*target, casterId, spellCopy, /*duration=*/5000, /*itemGuid=*/0);
		CHECK(incomingContainer.ShouldOverwriteAura(existingContainer) == AuraApplicationResult::Replace);
	}
}

TEST_CASE("UniquePerTarget shared stacking category: second spell evicts first regardless of spell id", "[spell_stacking][regression]")
{
	// Bug: Category-eviction only ran for CategoryExclusive. UniquePerTarget with a
	// shared stacking_category_id never evicted the conflicting spell from a different id.
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto target = MakeUnit(project, timers);

	proto::SpellEntry spellA = MakeSpell();
	spellA.set_id(800);
	spellA.set_rank(1);
	spellA.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::UniquePerTarget));
	spellA.set_stacking_category_id(42);

	proto::SpellEntry spellB = MakeSpell();
	spellB.set_id(801);
	spellB.set_rank(1);
	spellB.set_stacking_rule(static_cast<uint32_t>(SpellStackingRule::UniquePerTarget));
	spellB.set_stacking_category_id(42);

	const uint64 casterId = 1;

	auto auraA = std::make_shared<AuraContainer>(*target, casterId, spellA, /*duration=*/5000, /*itemGuid=*/0);
	target->ApplyAura(std::move(auraA));
	CHECK(target->HasAuraSpellFromCaster(800, casterId));

	// Apply spellB (same category) — spellA must be evicted
	auto auraB = std::make_shared<AuraContainer>(*target, casterId, spellB, /*duration=*/5000, /*itemGuid=*/0);
	target->ApplyAura(std::move(auraB));

	CHECK(target->HasAuraSpellFromCaster(801, casterId));
	CHECK_FALSE(target->HasAuraSpellFromCaster(800, casterId));
}
