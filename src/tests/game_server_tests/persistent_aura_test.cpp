// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/aura_container.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/persistent_aura.h"
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

	/// Registers a plain buff spell with the given base duration so it can be
	/// restored via RestorePersistentAuras.
	proto::SpellEntry* AddBuffSpell(proto::Project& project, const uint32 spellId, const GameTime duration)
	{
		auto* spell = project.spells.add(spellId);
		if (!spell)
		{
			return nullptr;
		}
		spell->set_id(spellId);
		spell->add_attributes(0);
		spell->add_attributes(0);
		spell->set_duration(static_cast<int32>(duration));
		return spell;
	}
}

TEST_CASE("Restored aura keeps its remaining duration at login", "[persistent_aura]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	constexpr uint32 spellId = 950;
	constexpr GameTime baseDuration = 30 * 60 * 1000;      // 30 minutes (like Frost Armor)
	constexpr GameTime persistedRemaining = 9 * 60 * 1000; // 9 minutes left at logout
	REQUIRE(AddBuffSpell(project, spellId, baseDuration) != nullptr);

	auto unit = MakeUnit(project, timers);

	PersistentAuraData data;
	data.spellId = spellId;
	data.casterId = unit->GetGuid();
	data.remainingDuration = persistedRemaining;
	data.stackCount = 1;
	unit->RestorePersistentAuras({ data });

	const auto persisted = unit->GetPersistentAuras();
	REQUIRE(persisted.size() == 1);
	CHECK(persisted[0].remainingDuration <= persistedRemaining);
	CHECK(persisted[0].remainingDuration > persistedRemaining - 60 * 1000);
}

TEST_CASE("Recasting a restored buff refreshes it to the full base duration", "[persistent_aura][regression]")
{
	// Bug: RestorePersistentAuras constructed the restored AuraContainer with the persisted
	// remaining time as its base duration, so RefreshAura (which extends by the base duration
	// and caps at it) could never push the buff past the remaining time it had at logout.
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	constexpr uint32 spellId = 951;
	constexpr GameTime baseDuration = 30 * 60 * 1000;      // 30 minutes (like Frost Armor)
	constexpr GameTime persistedRemaining = 9 * 60 * 1000; // 9 minutes left at logout
	proto::SpellEntry* spell = AddBuffSpell(project, spellId, baseDuration);
	REQUIRE(spell != nullptr);

	auto unit = MakeUnit(project, timers);
	const uint64 casterId = unit->GetGuid();

	// Simulate login: restore the persisted aura with 9 minutes remaining.
	PersistentAuraData data;
	data.spellId = spellId;
	data.casterId = casterId;
	data.remainingDuration = persistedRemaining;
	data.stackCount = 1;
	unit->RestorePersistentAuras({ data });

	// Recast the buff the way a real cast does: fresh container with the full base duration.
	auto recast = std::make_shared<AuraContainer>(*unit, casterId, *spell, baseDuration, /*itemGuid=*/0);
	unit->ApplyAura(std::move(recast));

	const auto persisted = unit->GetPersistentAuras();
	REQUIRE(persisted.size() == 1);
	CHECK(persisted[0].remainingDuration > baseDuration - 60 * 1000);
	CHECK(persisted[0].remainingDuration <= baseDuration);
}
