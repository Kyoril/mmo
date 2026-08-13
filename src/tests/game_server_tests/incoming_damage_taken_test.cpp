// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/aura_container.h"
#include "game_server/objects/game_player_s.h"
#include "game/aura.h"
#include "game/spell.h"
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
	std::shared_ptr<GamePlayerS> MakeDamageTakenUnit(proto::Project& project, TimerQueue& timers, uint32 level = 1)
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

	/// Build a spell carrying a single ModDamageTakenPct effect. Defaults to the shape of
	/// Rite of Rising (spell 240): a self-buff on the caster with no damage class
	/// restriction. Pass targetA = TargetEnemy for the Mark Weakness shape (spell 169),
	/// which makes the aura caster-scoped via AuraContainer::IsHostileTargetAura().
	proto::SpellEntry MakeDamageTakenSpell(uint32 id, int32 percent, uint32 dmgClass,
		uint32 targetA = spell_effect_targets::Caster)
	{
		proto::SpellEntry spell;
		spell.add_attributes(0);
		spell.add_attributes(0);
		spell.set_id(id);
		spell.set_baseid(id);
		spell.set_rank(1);
		spell.set_dmgclass(dmgClass);

		auto* effect = spell.add_effects();
		effect->set_type(spell_effects::ApplyAura);
		effect->set_aura(static_cast<uint32>(aura_type::ModDamageTakenPct));
		effect->set_basepoints(percent);
		effect->set_targeta(targetA);
		return spell;
	}

	/// Apply the spell's single aura effect to the unit and return the live container.
	std::shared_ptr<AuraContainer> ApplyDamageTakenAura(GameUnitS& unit,
		const proto::SpellEntry& spell, uint64 casterId)
	{
		auto container = std::make_shared<AuraContainer>(unit, casterId, spell, /*duration=*/12000, /*itemGuid=*/0);
		container->AddAuraEffect(spell.effects(0), spell.effects(0).basepoints());
		{
			auto handle = container;
			unit.ApplyAura(std::move(handle));
		}
		return container;
	}
}

TEST_CASE("GetIncomingDamageTakenMultiplier is 1.0 with no auras applied", "[damage_taken]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto victim = MakeDamageTakenUnit(project, timers);

	CHECK(victim->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Melee) == Approx(1.0f));
	CHECK(victim->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Magic) == Approx(1.0f));
}

// This is the contract the legacy auto-attack path in GameUnitS::ExecuteAutoAttackSwing()
// relies on: a Rite of Rising style aura (no dmgclass restriction) must reduce melee
// auto-attack damage exactly as it reduces spell and weapon-ability damage.
TEST_CASE("GetIncomingDamageTakenMultiplier applies an unrestricted ModDamageTakenPct aura to melee damage", "[damage_taken]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto victim = MakeDamageTakenUnit(project, timers);

	// -90% damage taken, no dmgclass set → applies to every damage class.
	const proto::SpellEntry spell = MakeDamageTakenSpell(240, /*percent=*/-90, spell_dmg_class::None);
	auto container = ApplyDamageTakenAura(*victim, spell, /*casterId=*/victim->GetGuid());
	REQUIRE(victim->HasAuraSpellFromCaster(240, victim->GetGuid()));

	CHECK(victim->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Melee) == Approx(0.1f));
	CHECK(victim->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Magic) == Approx(0.1f));
}

TEST_CASE("GetIncomingDamageTakenMultiplier ignores a ModDamageTakenPct aura restricted to another damage class", "[damage_taken]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto victim = MakeDamageTakenUnit(project, timers);

	// dmgclass = Magic → the aura must not touch melee damage.
	const proto::SpellEntry spell = MakeDamageTakenSpell(241, /*percent=*/-90, spell_dmg_class::Magic);
	auto container = ApplyDamageTakenAura(*victim, spell, /*casterId=*/victim->GetGuid());
	REQUIRE(victim->HasAuraSpellFromCaster(241, victim->GetGuid()));

	CHECK(victim->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Melee) == Approx(1.0f));
	CHECK(victim->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Magic) == Approx(0.1f));
}

TEST_CASE("GetIncomingDamageTakenMultiplier clamps at zero and never inverts damage", "[damage_taken]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto victim = MakeDamageTakenUnit(project, timers);

	// A basepoints value beyond -100 must clamp to 0, not produce healing.
	const proto::SpellEntry spell = MakeDamageTakenSpell(242, /*percent=*/-150, spell_dmg_class::None);
	auto container = ApplyDamageTakenAura(*victim, spell, /*casterId=*/victim->GetGuid());
	REQUIRE(victim->HasAuraSpellFromCaster(242, victim->GetGuid()));

	CHECK(victim->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Melee) == Approx(0.0f));
}

TEST_CASE("GetIncomingDamageTakenMultiplier applies a positive ModDamageTakenPct aura as increased damage", "[damage_taken]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto victim = MakeDamageTakenUnit(project, timers);

	const proto::SpellEntry spell = MakeDamageTakenSpell(243, /*percent=*/+50, spell_dmg_class::None);
	auto container = ApplyDamageTakenAura(*victim, spell, /*casterId=*/victim->GetGuid());
	REQUIRE(victim->HasAuraSpellFromCaster(243, victim->GetGuid()));

	CHECK(victim->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Melee) == Approx(1.5f));
}

// Regression guard for the call site in AuraEffect::HandlePeriodicDamage(): a DoT tick must
// be mitigated by ModDamageTakenPct like every other damage source. Unlike an auto-attack
// swing this is fully deterministic — periodic ticks involve no combat-table roll — so the
// resulting health is an exact number.
TEST_CASE("Periodic damage ticks are reduced by a ModDamageTakenPct aura", "[damage_taken]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto victim = MakeDamageTakenUnit(project, timers);
	victim->Set<uint32>(object_fields::MaxHealth, 1000, false);
	victim->Set<uint32>(object_fields::Health, 1000, false);

	// Shield of Faith's shape: -50% damage taken, every damage class.
	const proto::SpellEntry shield = MakeDamageTakenSpell(56, /*percent=*/-50, spell_dmg_class::None);
	auto shieldContainer = ApplyDamageTakenAura(*victim, shield, /*casterId=*/victim->GetGuid());
	REQUIRE(victim->HasAuraSpellFromCaster(56, victim->GetGuid()));

	// A magic DoT dealing 100 per tick, with exactly one tick.
	proto::SpellEntry dot;
	dot.add_attributes(0);
	dot.add_attributes(0);
	dot.set_id(300);
	dot.set_baseid(300);
	dot.set_rank(1);
	dot.set_dmgclass(spell_dmg_class::Magic);

	auto* dotEffect = dot.add_effects();
	dotEffect->set_type(spell_effects::ApplyAura);
	dotEffect->set_aura(static_cast<uint32>(aura_type::PeriodicDamage));
	dotEffect->set_basepoints(100);
	dotEffect->set_amplitude(100);
	dotEffect->set_targeta(spell_effect_targets::TargetEnemy);

	auto dotContainer = std::make_shared<AuraContainer>(*victim, /*casterId=*/0, dot, /*duration=*/100, /*itemGuid=*/0);
	dotContainer->AddAuraEffect(dot.effects(0), /*basePoints=*/100);
	{
		auto handle = dotContainer;
		victim->ApplyAura(std::move(handle));
	}

	// Drive the timer queue until the tick has fired and the auras have expired.
	io.run();

	// 100 raw damage halved to 50 — not the unmitigated 100.
	CHECK(victim->Get<uint32>(object_fields::Health) == 950u);
}

// Mark Weakness (spell 169) is a hostile-target aura, so it only applies to damage from the
// unit that cast it. Every damage call site must therefore pass the real attacker: a caller
// passing nullptr silently disables the aura instead of applying it.
TEST_CASE("GetIncomingDamageTakenMultiplier applies a hostile-target aura only to its own caster's damage", "[damage_taken]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto victim = MakeDamageTakenUnit(project, timers);
	auto caster = MakeDamageTakenUnit(project, timers);
	auto bystander = MakeDamageTakenUnit(project, timers);

	// MakeDamageTakenUnit leaves the guid at 0, so give the two attackers distinct ones.
	caster->Set<uint64>(object_fields::Guid, 0x10, false);
	bystander->Set<uint64>(object_fields::Guid, 0x20, false);
	REQUIRE(caster->GetGuid() != bystander->GetGuid());

	// +10% melee damage taken, applied by an enemy — the Mark Weakness shape.
	const proto::SpellEntry spell = MakeDamageTakenSpell(169, /*percent=*/+10, spell_dmg_class::Melee,
		spell_effect_targets::TargetEnemy);
	auto container = ApplyDamageTakenAura(*victim, spell, /*casterId=*/caster->GetGuid());
	REQUIRE(victim->HasAuraSpellFromCaster(169, caster->GetGuid()));
	REQUIRE(container->IsHostileTargetAura());

	// The caster's own melee damage is amplified...
	CHECK(victim->GetIncomingDamageTakenMultiplier(caster.get(), spell_dmg_class::Melee) == Approx(1.1f));

	// ...but nobody else's is, and an unknown attacker must not benefit either.
	CHECK(victim->GetIncomingDamageTakenMultiplier(bystander.get(), spell_dmg_class::Melee) == Approx(1.0f));
	CHECK(victim->GetIncomingDamageTakenMultiplier(nullptr, spell_dmg_class::Melee) == Approx(1.0f));
}
