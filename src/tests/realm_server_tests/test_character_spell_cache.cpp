// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// The realm caches a character's spells between world nodes. The cache must hold every known spell,
// not only the active ones: spells of inactive classes and dormant talent follow-up ranks (e.g.
// Fireball rank 2 after a talent reset) would otherwise be lost on the next node and its next save.

#include "catch.hpp"

#include "realm_server/character_spell_cache.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <algorithm>
#include <memory>

using namespace mmo;

namespace
{
	constexpr uint32 ClassId = 1;
	constexpr uint32 TabId = 1;
	constexpr uint32 TalentId = 20;
	constexpr uint32 TalentRankSpell = 600;
	constexpr uint32 FollowUpRank = 601;
	constexpr uint32 PlainSpell = 610;

	void AddSpell(proto::Project& project, const uint32 id, const uint32 baseId)
	{
		auto* spell = project.spells.add(id);
		spell->set_name("Test Spell");
		spell->set_baseid(baseId);
		spell->add_attributes(0);
		spell->add_attributes(0);
	}

	bool Contains(const std::vector<uint32>& ids, const uint32 id)
	{
		return std::find(ids.begin(), ids.end(), id) != ids.end();
	}
}

TEST_CASE("The realm spell cache keeps dormant talent follow-up ranks", "[character_spell_cache]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto* tab = project.talentTabs.add(TabId);
	tab->set_name("Test");
	tab->set_class_id(ClassId);
	AddSpell(project, TalentRankSpell, TalentRankSpell);
	AddSpell(project, FollowUpRank, TalentRankSpell);
	AddSpell(project, PlainSpell, 0);
	auto* talent = project.talents.add(TalentId);
	talent->set_tab(TabId);
	talent->set_row(0);
	talent->set_column(0);
	talent->add_ranks(TalentRankSpell);

	auto* cls = project.classes.add(ClassId);
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

	auto player = std::make_shared<GamePlayerS>(project, timers);
	player->Initialize();
	player->Set<uint64>(object_fields::Guid, 0x3000);
	player->SetClass(*cls);
	player->SetLevel(2);

	// Known follow-up rank without its talent: dormant (not active), but still owned.
	player->SetKnownSpells({ PlainSpell, FollowUpRank });
	REQUIRE_FALSE(player->HasSpell(FollowUpRank));

	const std::vector<uint32> cached = CollectCachedSpellIds(*player);
	CHECK(Contains(cached, PlainSpell));
	CHECK(Contains(cached, FollowUpRank));
}
