// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/spells/spell_effects.h"
#include "game_server/spells/spell_cast_context.h"
#include "game_server/spells/spell_cast.h"
#include "game_server/objects/game_player_s.h"
#include "game/spell_target_map.h"
#include "game/object_type_id.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/classes.pb.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	/// Build a minimal GamePlayerS with a class entry so SetLevel() works.
	std::shared_ptr<GamePlayerS> MakeUnit(proto::Project& project, TimerQueue& timers, uint32 level = 1)
	{
		// Use class id 1 — only add once per project instance.
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

	/// Build a SpellEffectContext wiring caster + effect + pre-populated targets.
	struct TestCtx
	{
		asio::io_service                    io;
		TimerQueue                          timers{ io };
		proto::Project                      project;
		std::shared_ptr<GamePlayerS>        caster;
		std::shared_ptr<GamePlayerS>        target;
		proto::SpellEntry                   spell;
		proto::SpellEffect                  effect;
		SpellTargetMap                      targetMap;
		std::unique_ptr<SpellCast>          cast;
		std::unique_ptr<SpellCastContext>   castCtx;
		std::vector<GameObjectS*>           targets;

		void Init(uint32 casterLevel = 1, uint32 targetLevel = 1)
		{
			caster = MakeUnit(project, timers, casterLevel);
			target = MakeUnit(project, timers, targetLevel);
			cast   = std::make_unique<SpellCast>(timers, *caster);
			castCtx = std::make_unique<SpellCastContext>(*cast, spell, targetMap);
			targets.push_back(target.get());
		}

		SpellEffectContext MakeEffectCtx(int32_t basePoints)
		{
			return SpellEffectContext{
				*castCtx,
				effect,
				basePoints,
				targets,
				[](GameUnitS&) -> AuraContainer& { static_assert(true, ""); throw std::logic_error("getOrCreateAuraContainer not expected"); },
				[](GameObjectS&) {}   // markAffectedTarget — no-op
			};
		}
	};
}

// ---------------------------------------------------------------------------
// CalculateBasePoints
// ---------------------------------------------------------------------------

TEST_CASE("CalculateBasePoints returns basepoints directly when level factors are zero", "[spell_effects]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers, 1);

	proto::SpellEntry spell;
	spell.set_spelllevel(1);
	spell.set_baselevel(1);
	spell.set_maxlevel(0);

	proto::SpellEffect effect;
	effect.set_basepoints(50);
	effect.set_basedice(0);
	effect.set_diesides(0);
	effect.set_pointsperlevel(0.0f);
	effect.set_diceperlevel(0.0f);

	SpellTargetMap targetMap;
	SpellCast cast(timers, *unit);
	SpellCastContext ctx(cast, spell, targetMap);

	const int32_t result = SpellEffects::CalculateBasePoints(ctx, effect);
	// caster level=1, spelllevel=1 → effective level=1-1=0
	// basePoints = 50 + 0*0 = 50, randomValue=0 → 50
	CHECK(result == 50);
}

TEST_CASE("CalculateBasePoints clamps level to maxlevel", "[spell_effects]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers, 20);

	proto::SpellEntry spell;
	spell.set_spelllevel(0);
	spell.set_baselevel(1);
	spell.set_maxlevel(5);

	proto::SpellEffect effect;
	effect.set_basepoints(0);
	effect.set_basedice(0);
	effect.set_diesides(0);
	effect.set_pointsperlevel(2.0f);
	effect.set_diceperlevel(0.0f);

	SpellTargetMap targetMap;
	SpellCast cast(timers, *unit);
	SpellCastContext ctx(cast, spell, targetMap);

	const int32_t result = SpellEffects::CalculateBasePoints(ctx, effect);
	// 20 > 5 && 5>0 → level=5; level-=0 → 5; basePoints=0+5*2=10
	CHECK(result == 10);
}

TEST_CASE("CalculateBasePoints clamps level to baselevel", "[spell_effects]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers, 1);

	proto::SpellEntry spell;
	spell.set_spelllevel(0);
	spell.set_baselevel(10);
	spell.set_maxlevel(0);

	proto::SpellEffect effect;
	effect.set_basepoints(0);
	effect.set_basedice(0);
	effect.set_diesides(0);
	effect.set_pointsperlevel(3.0f);
	effect.set_diceperlevel(0.0f);

	SpellTargetMap targetMap;
	SpellCast cast(timers, *unit);
	SpellCastContext ctx(cast, spell, targetMap);

	const int32_t result = SpellEffects::CalculateBasePoints(ctx, effect);
	// 1 < 10 → level=10; level-=0 → 10; basePoints=0+10*3=30
	CHECK(result == 30);
}

// ---------------------------------------------------------------------------
// HandleHeal — applies healing to target health field
// ---------------------------------------------------------------------------

TEST_CASE("HandleHeal increases target health by basePoints when no modifiers", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	// Start target at 500/1000 HP
	ctx.target->Set<uint32>(object_fields::Health,    500);
	ctx.target->Set<uint32>(object_fields::MaxHealth, 1000);

	// No spell power, no powerbonusfactor → heal = basePoints
	ctx.effect.set_powerbonusfactor(0.0f);

	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/100);
	SpellEffects::HandleHeal(effCtx);

	CHECK(ctx.target->GetHealth() >= 600u);
}

TEST_CASE("HandleHeal does not exceed target max health", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	ctx.target->Set<uint32>(object_fields::Health,    950);
	ctx.target->Set<uint32>(object_fields::MaxHealth, 1000);

	ctx.effect.set_powerbonusfactor(0.0f);

	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/200);
	SpellEffects::HandleHeal(effCtx);

	CHECK(ctx.target->GetHealth() <= ctx.target->GetMaxHealth());
	CHECK(ctx.target->GetHealth() == 1000u);
}

TEST_CASE("HandleHeal does nothing when target list is empty", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	ctx.target->Set<uint32>(object_fields::Health,    500);
	ctx.target->Set<uint32>(object_fields::MaxHealth, 1000);

	// Remove all targets
	ctx.targets.clear();

	ctx.effect.set_powerbonusfactor(0.0f);
	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/200);
	SpellEffects::HandleHeal(effCtx);

	CHECK(ctx.target->GetHealth() == 500u);
}

TEST_CASE("HandleHeal does not reduce health when basePoints is negative", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	ctx.target->Set<uint32>(object_fields::Health,    500);
	ctx.target->Set<uint32>(object_fields::MaxHealth, 1000);

	ctx.effect.set_powerbonusfactor(0.0f);

	// Negative basePoints → clamped to 0 by std::max → no change
	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/-50);
	SpellEffects::HandleHeal(effCtx);

	CHECK(ctx.target->GetHealth() == 500u);
}

// ---------------------------------------------------------------------------
// HandleSchoolDamage — reduces target health
// ---------------------------------------------------------------------------

TEST_CASE("HandleSchoolDamage reduces target health by basePoints when no modifiers", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	ctx.target->Set<uint32>(object_fields::Health,    1000);
	ctx.target->Set<uint32>(object_fields::MaxHealth, 1000);

	ctx.effect.set_powerbonusfactor(0.0f);

	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/150);
	SpellEffects::HandleSchoolDamage(effCtx);

	// Health should have dropped by at least 150 (may vary by crit, but with no
	// spell power and no modifiers the base case should be exactly 150)
	CHECK(ctx.target->GetHealth() <= 850u);
}

TEST_CASE("HandleSchoolDamage does nothing when target list is empty", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	ctx.target->Set<uint32>(object_fields::Health,    1000);
	ctx.target->Set<uint32>(object_fields::MaxHealth, 1000);

	ctx.targets.clear();

	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/150);
	SpellEffects::HandleSchoolDamage(effCtx);

	CHECK(ctx.target->GetHealth() == 1000u);
}

TEST_CASE("HandleSchoolDamage does not reduce health below zero", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	ctx.target->Set<uint32>(object_fields::Health,    10);
	ctx.target->Set<uint32>(object_fields::MaxHealth, 1000);

	ctx.effect.set_powerbonusfactor(0.0f);

	// Overkill — health field should floor at 0
	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/9999);
	SpellEffects::HandleSchoolDamage(effCtx);

	CHECK(ctx.target->GetHealth() == 0u);
}

// ---------------------------------------------------------------------------
// HandleEnergize — adds power to target
// ---------------------------------------------------------------------------

TEST_CASE("HandleEnergize increases target mana by basePoints", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	// Set mana to 200/500
	ctx.target->Set<uint32>(object_fields::Mana,    200);
	ctx.target->Set<uint32>(object_fields::MaxMana, 500);

	ctx.effect.set_miscvaluea(0); // power_type::Mana = 0

	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/100);
	SpellEffects::HandleEnergize(effCtx);

	CHECK(ctx.target->Get<uint32>(object_fields::Mana) == 300u);
}

TEST_CASE("HandleEnergize clamps mana at max", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	ctx.target->Set<uint32>(object_fields::Mana,    450);
	ctx.target->Set<uint32>(object_fields::MaxMana, 500);

	ctx.effect.set_miscvaluea(0); // Mana

	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/200);
	SpellEffects::HandleEnergize(effCtx);

	CHECK(ctx.target->Get<uint32>(object_fields::Mana) == 500u);
}

TEST_CASE("HandleEnergize does nothing when target list is empty", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	ctx.target->Set<uint32>(object_fields::Mana,    100);
	ctx.target->Set<uint32>(object_fields::MaxMana, 500);

	ctx.targets.clear();
	ctx.effect.set_miscvaluea(0);

	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/100);
	SpellEffects::HandleEnergize(effCtx);

	CHECK(ctx.target->Get<uint32>(object_fields::Mana) == 100u);
}

TEST_CASE("HandleEnergize does nothing for invalid power type", "[spell_effects]")
{
	TestCtx ctx;
	ctx.Init();

	ctx.target->Set<uint32>(object_fields::Mana, 100);
	ctx.target->Set<uint32>(object_fields::MaxMana, 500);

	ctx.effect.set_miscvaluea(99); // out of range → early return

	auto effCtx = ctx.MakeEffectCtx(/*basePoints=*/100);
	SpellEffects::HandleEnergize(effCtx);

	CHECK(ctx.target->Get<uint32>(object_fields::Mana) == 100u);
}
