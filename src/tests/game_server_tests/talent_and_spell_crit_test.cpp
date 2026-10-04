// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Covers the talent tree redesign's engine pieces: direct damage spells roll crits (they never
// did, which made every "+crit" talent a no-op), CritDamageBonus modifiers scale the crit bonus,
// placeholder talents can not be learned, and saved talents that no longer fit the talent data
// reset instead of silently refunding while the old rank spell stays learned.

#include "catch.hpp"

#include "game_server/spells/spell_effects.h"
#include "game_server/spells/spell_cast_context.h"
#include "game_server/spells/spell_cast.h"
#include "game_server/objects/game_player_s.h"
#include "game/spell_target_map.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <map>
#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 ClassId = 1;
	constexpr uint32 TabId = 1;
	constexpr uint32 SpellId = 900;

	std::shared_ptr<GamePlayerS> MakePlayer(proto::Project& project, TimerQueue& timers)
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

		static uint64 nextGuid = 0x1000;
		auto player = std::make_shared<GamePlayerS>(project, timers);
		player->Initialize();
		// Unlearning spells removes their auras by caster guid, which must not be 0.
		player->Set<uint64>(object_fields::Guid, ++nextGuid);
		player->SetClass(*cls);
		player->SetLevel(2);
		return player;
	}

	/// A spell the casting unit's modifiers can match (by family flags).
	proto::SpellEntry& AddSpell(proto::Project& project, const uint32 id, const uint64 familyFlags)
	{
		auto* spell = project.spells.add(id);
		spell->set_name("Test Spell");
		spell->set_familyflags(familyFlags);
		// Learning a spell reads both attribute words.
		spell->add_attributes(0);
		spell->add_attributes(0);
		return *spell;
	}

	SpellModifier MakeMod(const SpellModOp op, const SpellModType type, const int32 value, const uint64 mask)
	{
		SpellModifier mod{};
		mod.op = op;
		mod.type = type;
		mod.value = value;
		mod.mask = mask;
		return mod;
	}

	struct DamageCtx
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		std::shared_ptr<GamePlayerS> caster;
		std::shared_ptr<GamePlayerS> target;
		proto::SpellEffect effect;
		SpellTargetMap targetMap;
		std::unique_ptr<SpellCast> cast;
		std::unique_ptr<SpellCastContext> castCtx;
		std::vector<GameObjectS*> targets;

		void Init(const float critChance)
		{
			project.combatSettings.set_spell_default_crit_chance(critChance);
			project.combatSettings.set_spell_crit_multiplier(1.5f);
			AddSpell(project, SpellId, 0x1);
			caster = MakePlayer(project, timers);
			target = MakePlayer(project, timers);
			target->Set<uint32>(object_fields::MaxHealth, 1000);
			target->Set<uint32>(object_fields::Health, 1000);
			cast = std::make_unique<SpellCast>(timers, *caster);
			castCtx = std::make_unique<SpellCastContext>(*cast, *project.spells.getById(SpellId), targetMap);
			targets.push_back(target.get());
		}

		void Hit(const int32 basePoints)
		{
			SpellEffectContext ctx{
				*castCtx, effect, basePoints, targets,
				[](GameUnitS&) -> AuraContainer& { throw std::logic_error("no auras expected"); },
				[](GameObjectS&) {}
			};
			SpellEffects::HandleSchoolDamage(ctx);
		}
	};
}

TEST_CASE("Direct damage spells never crit at 0% crit chance", "[spell_crit]")
{
	DamageCtx ctx;
	ctx.Init(0.0f);
	ctx.Hit(100);
	CHECK(ctx.target->GetHealth() == 900u);
}

TEST_CASE("Direct damage spells crit with the configured multiplier", "[spell_crit]")
{
	DamageCtx ctx;
	ctx.Init(100.0f);
	ctx.Hit(100);
	CHECK(ctx.target->GetHealth() == 850u);
}

TEST_CASE("CritChance modifiers make a direct damage spell crit", "[spell_crit]")
{
	DamageCtx ctx;
	ctx.Init(0.0f);
	ctx.caster->ModifySpellMod(MakeMod(spell_mod_op::CritChance, spell_mod_type::Flat, 100, 0x1), true);
	ctx.Hit(100);
	CHECK(ctx.target->GetHealth() == 850u);
}

TEST_CASE("CritChance modifiers of other spell families do not apply", "[spell_crit]")
{
	DamageCtx ctx;
	ctx.Init(0.0f);
	ctx.caster->ModifySpellMod(MakeMod(spell_mod_op::CritChance, spell_mod_type::Flat, 100, 0x2), true);
	ctx.Hit(100);
	CHECK(ctx.target->GetHealth() == 900u);
}

TEST_CASE("CritDamageBonus scales the bonus part of a spell crit", "[spell_crit]")
{
	DamageCtx ctx;
	ctx.Init(100.0f);
	ctx.caster->ModifySpellMod(MakeMod(spell_mod_op::CritDamageBonus, spell_mod_type::Pct, 100, 0x1), true);
	ctx.Hit(100);

	// 1.5x crit, bonus +100% -> 2.0x
	CHECK(ctx.target->GetHealth() == 800u);
}

TEST_CASE("ApplyCritDamageBonus without modifiers keeps the multiplier", "[spell_crit]")
{
	DamageCtx ctx;
	ctx.Init(0.0f);
	CHECK(SpellEffects::ApplyCritDamageBonus(*ctx.caster, SpellId, 1.5f) == Approx(1.5f));
	CHECK(SpellEffects::ApplyCritDamageBonus(*ctx.caster, SpellId, 2.0f) == Approx(2.0f));
}

TEST_CASE("ApplyCritDamageBonus applies percentage and flat modifiers to the bonus", "[spell_crit]")
{
	DamageCtx ctx;
	ctx.Init(0.0f);

	ctx.caster->ModifySpellMod(MakeMod(spell_mod_op::CritDamageBonus, spell_mod_type::Pct, 50, 0x1), true);
	CHECK(SpellEffects::ApplyCritDamageBonus(*ctx.caster, SpellId, 1.5f) == Approx(1.75f));
	CHECK(SpellEffects::ApplyCritDamageBonus(*ctx.caster, SpellId, 2.0f) == Approx(2.5f));

	ctx.caster->ModifySpellMod(MakeMod(spell_mod_op::CritDamageBonus, spell_mod_type::Pct, 50, 0x1), false);
	ctx.caster->ModifySpellMod(MakeMod(spell_mod_op::CritDamageBonus, spell_mod_type::Flat, 25, 0x1), true);
	CHECK(SpellEffects::ApplyCritDamageBonus(*ctx.caster, SpellId, 1.5f) == Approx(1.75f));
}

TEST_CASE("ApplyCritDamageBonus never drops below 1x", "[spell_crit]")
{
	DamageCtx ctx;
	ctx.Init(0.0f);
	ctx.caster->ModifySpellMod(MakeMod(spell_mod_op::CritDamageBonus, spell_mod_type::Flat, -500, 0x1), true);
	CHECK(SpellEffects::ApplyCritDamageBonus(*ctx.caster, SpellId, 1.5f) == Approx(1.0f));
}

namespace
{
	struct TalentCtx
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;
		std::shared_ptr<GamePlayerS> player;

		static constexpr uint32 RealTalent = 10;
		static constexpr uint32 PlaceholderTalent = 11;

		void Init()
		{
			auto* tab = project.talentTabs.add(TabId);
			tab->set_name("Test");
			tab->set_class_id(ClassId);

			AddSpell(project, 500, 0);
			AddSpell(project, 501, 0);

			auto* real = project.talents.add(RealTalent);
			real->set_tab(TabId);
			real->set_row(0);
			real->set_column(0);
			real->add_ranks(500);

			auto* placeholder = project.talents.add(PlaceholderTalent);
			placeholder->set_tab(TabId);
			placeholder->set_row(0);
			placeholder->set_column(1);
			placeholder->add_ranks(501);
			placeholder->set_placeholder(true);

			player = MakePlayer(project, timers);
			player->Set<uint32>(object_fields::TalentPoints, 5);
		}
	};
}

TEST_CASE("A real talent can be learned", "[talents]")
{
	TalentCtx ctx;
	ctx.Init();
	CHECK(ctx.player->LearnTalent(TalentCtx::RealTalent, 0));
	CHECK(ctx.player->HasTalent(TalentCtx::RealTalent));
}

TEST_CASE("A placeholder talent can not be learned", "[talents]")
{
	TalentCtx ctx;
	ctx.Init();
	CHECK_FALSE(ctx.player->LearnTalent(TalentCtx::PlaceholderTalent, 0));
	CHECK_FALSE(ctx.player->HasTalent(TalentCtx::PlaceholderTalent));
}

TEST_CASE("Saved talents that still fit the data are restored", "[talents]")
{
	TalentCtx ctx;
	ctx.Init();
	ctx.player->InitializeTalents({ { TalentCtx::RealTalent, 0 } });
	CHECK(ctx.player->HasTalent(TalentCtx::RealTalent));
}

TEST_CASE("A saved rank the talent no longer has resets the talents", "[talents]")
{
	TalentCtx ctx;
	ctx.Init();
	ctx.player->InitializeTalents({ { TalentCtx::RealTalent, 2 } });
	CHECK_FALSE(ctx.player->HasTalent(TalentCtx::RealTalent));
}

TEST_CASE("A saved talent that became a placeholder resets the talents", "[talents]")
{
	TalentCtx ctx;
	ctx.Init();
	ctx.player->InitializeTalents({ { TalentCtx::RealTalent, 0 }, { TalentCtx::PlaceholderTalent, 0 } });
	CHECK_FALSE(ctx.player->HasTalent(TalentCtx::RealTalent));
	CHECK_FALSE(ctx.player->HasTalent(TalentCtx::PlaceholderTalent));
}
