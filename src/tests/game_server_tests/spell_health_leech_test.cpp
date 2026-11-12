// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// HealthLeech: deals school damage exactly like SchoolDamage and heals the caster for the damage
// dealt times the effect's multiplevalue (0 counts as 1.0).

#include "catch.hpp"

#include "game_server/spells/spell_effects.h"
#include "game_server/spells/spell_cast_context.h"
#include "game_server/spells/spell_cast.h"
#include "game_server/objects/game_player_s.h"
#include "game/spell_target_map.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 ClassId = 1;
	constexpr uint32 SpellId = 910;

	std::shared_ptr<GamePlayerS> MakeUnit(proto::Project& project, TimerQueue& timers)
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
				for (int a = 0; a < 5; ++a)
				{
					values->add_attributes(10);
				}
				values->set_talentpoints(3);
			}
		}

		static uint64 nextGuid = 0x2000;
		auto player = std::make_shared<GamePlayerS>(project, timers);
		player->Initialize();
		player->Set<uint64>(object_fields::Guid, ++nextGuid);
		player->SetClass(*cls);
		player->SetLevel(2);
		return player;
	}

	struct LeechCtx
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

		explicit LeechCtx(const float ratio)
		{
			auto* spell = project.spells.add(SpellId);
			spell->set_name("Test Leech");
			spell->add_attributes(0);
			spell->add_attributes(0);
			effect.set_type(spell_effects::HealthLeech);
			effect.set_multiplevalue(ratio);

			caster = MakeUnit(project, timers);
			caster->Set<uint32>(object_fields::MaxHealth, 100);
			caster->Set<uint32>(object_fields::Health, 50);
			target = MakeUnit(project, timers);
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
				[](GameUnitS&) -> AuraContainer&
				{
					FAIL("no auras expected");
					std::abort();
				},
				[](GameObjectS&) {}
			};
			SpellEffects::HandleHealthLeech(ctx);
		}
	};
}

TEST_CASE("HealthLeech heals the caster for the damage dealt", "[health_leech]")
{
	LeechCtx ctx(1.0f);
	ctx.Hit(10);
	const uint32 dealt = 1000u - ctx.target->GetHealth();
	CHECK(dealt > 0u);
	CHECK(ctx.caster->GetHealth() == 50u + dealt);
}

TEST_CASE("HealthLeech scales the heal by multiplevalue", "[health_leech]")
{
	LeechCtx ctx(0.5f);
	ctx.Hit(20);
	const uint32 dealt = 1000u - ctx.target->GetHealth();
	CHECK(dealt > 0u);
	CHECK(ctx.caster->GetHealth() == 50u + dealt / 2u);
}

TEST_CASE("HealthLeech treats an unset multiplevalue as 1.0", "[health_leech]")
{
	LeechCtx ctx(0.0f);
	ctx.Hit(10);
	const uint32 dealt = 1000u - ctx.target->GetHealth();
	CHECK(dealt > 0u);
	CHECK(ctx.caster->GetHealth() == 50u + dealt);
}

TEST_CASE("HealthLeech does not heal when the target is immune", "[health_leech]")
{
	LeechCtx ctx(1.0f);
	ctx.target->AddSchoolImmunity(ctx.project.spells.getById(SpellId)->spellschool());
	ctx.Hit(10);
	CHECK(ctx.target->GetHealth() == 1000u);
	CHECK(ctx.caster->GetHealth() == 50u);
}
