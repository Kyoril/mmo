// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "shared/proto_data/spells.pb.h"
#include "shared/proto_data/classes.pb.h"

#include <memory>

namespace mmo::test
{
	/// Builds a minimal GamePlayerS with a class entry set up so RefreshStats() and SetLevel()
	/// work without asserting.
	///
	/// GamePlayerS is final, so a test cannot reach its protected regeneration members through
	/// a subclass - suites that need to observe regeneration go through the public effective-
	/// value helpers on a unit built here.
	/// @param project The project the unit belongs to. Gains a class entry with id 1 if it has
	///	none yet.
	/// @param timers The timer queue the unit's countdowns run on.
	/// @param level The level to bring the unit to.
	/// @return The constructed player.
	inline std::shared_ptr<GamePlayerS> MakeUnit(proto::Project& project, TimerQueue& timers, const uint32 level = 1)
	{
		auto* cls = project.classes.getById(1);
		if (!cls)
		{
			cls = project.classes.add(1);
			if (cls)
			{
				cls->set_powertype(proto::ClassEntry_PowerType_MANA);

				// Non-zero flat regeneration, so percentage modifiers have something to scale.
				// With both at zero, every regeneration assertion would read 0 == 0 and pass
				// no matter what the modifier did.
				cls->set_healthregenpertick(10.0f);
				cls->set_basemanaregenpertick(20.0f);

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
		if (cls)
		{
			unit->SetClass(*cls);
		}
		unit->SetLevel(level);
		return unit;
	}

	/// Builds a minimal spell with both attribute slots present, which the aura effect handlers
	/// read unguarded.
	/// @return The constructed spell entry.
	inline proto::SpellEntry MakeSpell()
	{
		proto::SpellEntry spell;
		spell.add_attributes(0);	// attributes_a
		spell.add_attributes(0);	// attributes_b
		return spell;
	}
}
