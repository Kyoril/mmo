// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/aura_container.h"
#include "game_server/objects/game_player_s.h"
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
	std::shared_ptr<GamePlayerS> MakeProcUnit(proto::Project& project, TimerQueue& timers, uint32 level = 1)
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

	/// Build a spell whose aura procs like Wounding Strike's buff: one charge,
	/// triggered by melee-class ability damage, restricted to a spell family.
	proto::SpellEntry MakeProcSpell(uint32 id, uint64 procFamily)
	{
		proto::SpellEntry spell;
		spell.add_attributes(0);
		spell.add_attributes(0);
		spell.set_id(id);
		spell.set_baseid(id);
		spell.set_rank(1);
		spell.set_procchance(100);
		spell.set_proccharges(1);
		spell.set_procflags(spell_proc_flags::DoneSpellMeleeDmgClass);
		spell.set_procfamily(procFamily);
		return spell;
	}
}

// Regression: Wounding Strike's buff (procfamily = Ruthless Strike's family flag)
// must not be consumed by family-less abilities like Quick Cut.
TEST_CASE("Proc aura with procfamily ignores spells without matching family flags", "[aura_proc]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto owner = MakeProcUnit(project, timers);

	const uint64 casterId = 42;
	proto::SpellEntry spell = MakeProcSpell(700, /*procFamily=*/1);

	auto container = std::make_shared<AuraContainer>(*owner, casterId, spell, /*duration=*/8000, /*itemGuid=*/0);
	{ auto handle = container; owner->ApplyAura(std::move(handle)); }
	REQUIRE(owner->HasAuraSpellFromCaster(700, casterId));

	// A spell with no family flags (e.g. Quick Cut) must not proc or consume the charge.
	const bool procced = container->HandleProc(
		spell_proc_flags::DoneSpellMeleeDmgClass, spell_proc_flags_ex::NormalHit,
		nullptr, 0, 0, false, /*familyFlags=*/0);
	CHECK_FALSE(procced);
	CHECK(owner->HasAuraSpellFromCaster(700, casterId));

	// A spell with a non-matching family flag (e.g. Wounding Strike itself) must not proc either.
	const bool proccedOther = container->HandleProc(
		spell_proc_flags::DoneSpellMeleeDmgClass, spell_proc_flags_ex::NormalHit,
		nullptr, 0, 0, false, /*familyFlags=*/2);
	CHECK_FALSE(proccedOther);
	CHECK(owner->HasAuraSpellFromCaster(700, casterId));
}

TEST_CASE("Proc aura with procfamily consumes its charge on a matching family spell", "[aura_proc]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto owner = MakeProcUnit(project, timers);

	const uint64 casterId = 42;
	proto::SpellEntry spell = MakeProcSpell(701, /*procFamily=*/1);

	auto container = std::make_shared<AuraContainer>(*owner, casterId, spell, /*duration=*/8000, /*itemGuid=*/0);
	{ auto handle = container; owner->ApplyAura(std::move(handle)); }
	REQUIRE(owner->HasAuraSpellFromCaster(701, casterId));

	// A spell with a matching family flag (e.g. Ruthless Strike) consumes the single charge.
	const bool procced = container->HandleProc(
		spell_proc_flags::DoneSpellMeleeDmgClass, spell_proc_flags_ex::NormalHit,
		nullptr, 0, 0, false, /*familyFlags=*/1);
	CHECK(procced);
	CHECK_FALSE(owner->HasAuraSpellFromCaster(701, casterId));
}

TEST_CASE("Proc aura without procfamily still procs from family-less spells", "[aura_proc]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto owner = MakeProcUnit(project, timers);

	const uint64 casterId = 42;
	proto::SpellEntry spell = MakeProcSpell(702, /*procFamily=*/0);

	auto container = std::make_shared<AuraContainer>(*owner, casterId, spell, /*duration=*/8000, /*itemGuid=*/0);
	{ auto handle = container; owner->ApplyAura(std::move(handle)); }
	REQUIRE(owner->HasAuraSpellFromCaster(702, casterId));

	// Unrestricted proc auras keep wildcard behavior.
	const bool procced = container->HandleProc(
		spell_proc_flags::DoneSpellMeleeDmgClass, spell_proc_flags_ex::NormalHit,
		nullptr, 0, 0, false, /*familyFlags=*/0);
	CHECK(procced);
	CHECK_FALSE(owner->HasAuraSpellFromCaster(702, casterId));
}
