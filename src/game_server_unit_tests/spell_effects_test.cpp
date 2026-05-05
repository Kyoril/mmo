// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/spells/spell_effects.h"
#include "game_server/spells/spell_cast_context.h"
#include "game_server/spells/spell_cast.h"
#include "game_server/objects/game_player_s.h"
#include "game/spell_target_map.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/spells.pb.h"
#include "asio/io_service.hpp"

using namespace mmo;

TEST_CASE("CalculateBasePoints returns basepoints directly when level factors are zero", "[spell_effects]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	GamePlayerS unit(project, timers);
	unit.Initialize();
	unit.SetLevel(1);

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
	SpellCast cast(timers, unit);
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
	GamePlayerS unit(project, timers);
	unit.Initialize();
	unit.SetLevel(20);

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
	SpellCast cast(timers, unit);
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
	GamePlayerS unit(project, timers);
	unit.Initialize();
	unit.SetLevel(1);

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
	SpellCast cast(timers, unit);
	SpellCastContext ctx(cast, spell, targetMap);

	const int32_t result = SpellEffects::CalculateBasePoints(ctx, effect);
	// 1 < 10 → level=10; level-=0 → 10; basePoints=0+10*3=30
	CHECK(result == 30);
}
