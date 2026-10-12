# Briarwatch Balance and Talent-Gated Spell Ranks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Briarwatch March playable for fresh level 5-6 characters by retuning its creatures, and give fire mages Fireball rank 2 from the trainer, gated on the Fireball talent.

**Architecture:** A header-only helper finds the talent a spell rank depends on (via `baseid`). `GamePlayerS` treats such follow-up ranks as inactive while the talent is missing, re-evaluates them after every talent learn/reset, and keeps them in the persisted known-spell set. The world server's trainer handler and the client trainer UI use the same rule. Data changes are written by one idempotent Python script.

**Tech Stack:** C++17, Catch2 (`game_server_tests`), Lua/XML UI in `data/client`, protobuf data in `data/editor`, Python 3 + protobuf for data authoring.

**Spec:** `docs/superpowers/specs/2026-10-12-briarwatch-balance-talent-ranks-design.md`

## Global Constraints

- Branch: `feature/briarwatch-balance` (this worktree). Never push; merging is `/ship` after user approval.
- No network protocol change, no `ProtocolVersion` bump, no DB migration.
- Code style: Allman braces, tabs, braces on every `if`, `m_camelCase` members, PascalCase methods, `#pragma once`, Doxygen on public members, header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- No exceptions; use `ASSERT`/`WLOG`/`ELOG`.
- UI strings only via localization keys, added to all four locales (`Locale_deDE`, `Locale_enUS`, `Locale_frFR`, `Locale_ruRU`).
- New spell id `1427` (Fireball rank 2); trainer `1`, `reqlevel 6`, `spellcost 450`.
- Fireball rank 2: direct `basepoints 29`, `diesides 9`, `pointsperlevel 0.6`; periodic `basepoints 4`; `casttime 1800`, `cost 45`, `spelllevel 6`, `baselevel 6`, `maxlevel 10`, `rank 2`, `baseid 4`. Spell 4: `rank 1`, `baseid 4`.
- Creature values exactly as in the spec table (Part 1).

## Review Focus

1. A multi-rank talent (rank 2 learned, rank-1 spell removed by `LearnTalent`) must keep its follow-up ranks active. Test in Task 2.
2. Saved talent data that no longer fits (InitializeTalents → ResetTalents) must leave follow-up ranks inactive but known. Test in Task 2.
3. A follow-up rank in the saved known list must not be active right after login before talents are restored, and must be active once they are. Test in Task 2.
4. Ordinary rank chains (Frostbolt 3 → 18) must be unaffected: known rank 2 is active with no talents at all. Test in Task 2.
5. Buying a dormant rank at the trainer without the talent must not take money. Handler gate runs before `ConsumeMoney` (Task 3); the predicate it calls is tested in Task 2.

---

### Task 0: Workspace setup

The worktree has no build directory and uninitialized submodules.

**Files:** none (environment only)

- [ ] **Step 1: Initialize submodules**

Run (Git Bash, worktree root):
```bash
git -c protocol.file.allow=always submodule update --init
```
Expected: `data/client`, `data/editor` and the `deps/*` submodules checked out. If `data/client` is slow, it is ~900 MB; let it finish.

- [ ] **Step 2: Put the submodules on branches**

Data commits must not land on a detached HEAD:
```bash
git -C data/editor checkout -b feature/briarwatch-balance
git -C data/client checkout -b feature/briarwatch-balance
```

- [ ] **Step 3: Configure the build like the main checkout**

Run (PowerShell, worktree root):
```powershell
. .\tools\gate\gate_worktree.ps1
Invoke-GateConfigure -Source (Get-Location).Path -MainBuild (Join-Path (Get-MainRepoRoot) "build")
```
Expected: `build/CMakeCache.txt` exists.

- [ ] **Step 4: Baseline build of the test suite**

```powershell
cmake --build build --config Debug -t game_server_tests
.\bin\Debug\game_server_tests.exe "[talents]"
```
Expected: build succeeds, all `[talents]` tests pass.

---

### Task 1: Shared helper `FindGatingTalent`

**Files:**
- Create: `src/shared/game/talent_rank_gate.h`
- Create: `src/tests/game_server_tests/talent_rank_gate_test.cpp`

**Interfaces:**
- Produces: `template<typename SpellEntryT, typename SpellManagerT, typename TalentManagerT> auto FindGatingTalent(const SpellEntryT& spell, const SpellManagerT& spells, const TalentManagerT& talents) -> decltype(talents.getById(0u))` in namespace `mmo`. Returns the talent entry pointer, or `nullptr`. Works with `proto::Project` and `proto_client::Project` managers.

- [ ] **Step 1: Write the failing test**

Create `src/tests/game_server_tests/talent_rank_gate_test.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Follow-up ranks of a talent spell (e.g. Fireball rank 2 bought from the trainer while rank 1 is
// the Fireball talent) are only usable while the talent is learned. They survive talent resets in
// the persistent known-spell set and come back when the talent is learned again.

#include "catch.hpp"

#include "game/talent_rank_gate.h"
#include "game_server/objects/game_player_s.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	constexpr uint32 ClassId = 1;
	constexpr uint32 TabId = 1;

	constexpr uint32 GatedTalent = 20;
	constexpr uint32 TwoRankTalent = 21;

	constexpr uint32 TalentRankSpell = 600;     // rank 1, the talent spell
	constexpr uint32 FollowUpRank = 601;        // rank 2, baseid 600, bought from a trainer
	constexpr uint32 TwoRankFirst = 620;        // rank 1 of TwoRankTalent
	constexpr uint32 TwoRankSecond = 621;       // rank 2 of TwoRankTalent (talent rank, not trainer)
	constexpr uint32 TwoRankFollowUp = 622;     // trainer rank, baseid 620
	constexpr uint32 PlainRank1 = 610;          // ordinary chain, not a talent
	constexpr uint32 PlainRank2 = 611;          // baseid 610
	constexpr uint32 DanglingRank = 630;        // baseid points at a missing spell

	proto::SpellEntry& AddSpell(proto::Project& project, const uint32 id, const uint32 baseId, const uint32 rank)
	{
		auto* spell = project.spells.add(id);
		spell->set_name("Test Spell");
		spell->set_baseid(baseId);
		spell->set_rank(rank);
		// Learning a spell reads both attribute words.
		spell->add_attributes(0);
		spell->add_attributes(0);
		return *spell;
	}

	struct GateCtx
	{
		asio::io_service io;
		TimerQueue timers{ io };
		proto::Project project;

		void Init()
		{
			auto* tab = project.talentTabs.add(TabId);
			tab->set_name("Test");
			tab->set_class_id(ClassId);

			AddSpell(project, TalentRankSpell, TalentRankSpell, 1);
			AddSpell(project, FollowUpRank, TalentRankSpell, 2);
			AddSpell(project, TwoRankFirst, TwoRankFirst, 1);
			AddSpell(project, TwoRankSecond, 0, 0);
			AddSpell(project, TwoRankFollowUp, TwoRankFirst, 2);
			AddSpell(project, PlainRank1, PlainRank1, 1);
			AddSpell(project, PlainRank2, PlainRank1, 2);
			AddSpell(project, DanglingRank, 9999, 2);

			auto* gated = project.talents.add(GatedTalent);
			gated->set_tab(TabId);
			gated->set_row(0);
			gated->set_column(0);
			gated->add_ranks(TalentRankSpell);

			auto* twoRank = project.talents.add(TwoRankTalent);
			twoRank->set_tab(TabId);
			twoRank->set_row(0);
			twoRank->set_column(1);
			twoRank->add_ranks(TwoRankFirst);
			twoRank->add_ranks(TwoRankSecond);
		}

		const proto::SpellEntry& Spell(const uint32 id) const
		{
			const auto* spell = project.spells.getById(id);
			REQUIRE(spell != nullptr);
			return *spell;
		}
	};
}

TEST_CASE("A trainer rank whose base spell is a talent rank is gated by that talent", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	const auto* talent = FindGatingTalent(ctx.Spell(FollowUpRank), ctx.project.spells, ctx.project.talents);
	REQUIRE(talent != nullptr);
	CHECK(talent->id() == GatedTalent);
}

TEST_CASE("Spells outside a talent rank chain are not gated", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	CHECK(FindGatingTalent(ctx.Spell(TalentRankSpell), ctx.project.spells, ctx.project.talents) == nullptr);
	CHECK(FindGatingTalent(ctx.Spell(PlainRank1), ctx.project.spells, ctx.project.talents) == nullptr);
	CHECK(FindGatingTalent(ctx.Spell(PlainRank2), ctx.project.spells, ctx.project.talents) == nullptr);
	CHECK(FindGatingTalent(ctx.Spell(DanglingRank), ctx.project.spells, ctx.project.talents) == nullptr);
	CHECK(FindGatingTalent(ctx.Spell(TwoRankSecond), ctx.project.spells, ctx.project.talents) == nullptr);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```powershell
cmake --build build --config Debug -t game_server_tests
```
Expected: compile error, `game/talent_rank_gate.h` not found. (`mmo_add_test` globs the folder; the new file needs a CMake re-glob, which the build triggers. If it does not appear, re-run `cmake -S . -B build`.)

- [ ] **Step 3: Write the helper**

Create `src/shared/game/talent_rank_gate.h`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Finds the talent a spell rank depends on.
	///
	/// Spell ranks are chained through `baseid` (rank 2 points at rank 1). When the spell a rank points
	/// at is itself the rank spell of a talent, the rank is a follow-up of that talent: it is bought from
	/// a trainer but only usable while the talent is learned. Works with server and client projects,
	/// whose template managers share the same interface.
	///
	/// @param spell The spell rank to check.
	/// @param spells The spell manager to resolve the base spell from.
	/// @param talents The talent manager to search.
	/// @returns The gating talent, or nullptr when the spell is not a follow-up rank of a talent spell.
	template<typename SpellEntryT, typename SpellManagerT, typename TalentManagerT>
	auto FindGatingTalent(const SpellEntryT& spell, const SpellManagerT& spells, const TalentManagerT& talents)
		-> decltype(talents.getById(0u))
	{
		const uint32 baseId = spell.baseid();
		if (baseId == 0 || baseId == spell.id())
		{
			return nullptr;
		}

		if (spells.getById(baseId) == nullptr)
		{
			return nullptr;
		}

		for (const auto& talent : talents.getTemplates().entry())
		{
			for (const uint32 rankSpellId : talent.ranks())
			{
				if (rankSpellId == baseId)
				{
					return talents.getById(talent.id());
				}
			}
		}

		return nullptr;
	}
}
```

- [ ] **Step 4: Run the test to verify it passes**

```powershell
cmake --build build --config Debug -t game_server_tests
.\bin\Debug\game_server_tests.exe "[talent_gate]"
```
Expected: 2 test cases pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/game/talent_rank_gate.h src/tests/game_server_tests/talent_rank_gate_test.cpp
git commit -m "Add FindGatingTalent for spell ranks that continue a talent spell"
```

---

### Task 2: Player activation, refresh after talent learn/reset

**Files:**
- Modify: `src/shared/game_server/objects/game_player_s.h` (near `IsSpellActiveForCurrentClass`, line ~513)
- Modify: `src/shared/game_server/objects/game_player_s.cpp` (`ResetTalents` ~436, `LearnTalent` tail ~2163-2180, `IsSpellActiveForCurrentClass` ~3021)
- Test: `src/tests/game_server_tests/talent_rank_gate_test.cpp`

**Interfaces:**
- Consumes: `FindGatingTalent` (Task 1).
- Produces: `[[nodiscard]] bool GamePlayerS::IsTalentGateSatisfied(const proto::SpellEntry& spell) const;` and `void GamePlayerS::RefreshTalentGatedSpells();` (public).

- [ ] **Step 1: Write the failing tests**

Append to `talent_rank_gate_test.cpp`. First add a player factory inside the anonymous namespace (after `GateCtx`'s `Init`, as a member):
```cpp
		std::shared_ptr<GamePlayerS> MakePlayer()
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

			static uint64 nextGuid = 0x2000;
			auto player = std::make_shared<GamePlayerS>(project, timers);
			player->Initialize();
			// Unlearning spells removes their auras by caster guid, which must not be 0.
			player->Set<uint64>(object_fields::Guid, ++nextGuid);
			player->SetClass(*cls);
			player->SetLevel(2);
			player->Set<uint32>(object_fields::TalentPoints, 5);
			return player;
		}
```
Then the test cases at the end of the file:
```cpp
TEST_CASE("A known follow-up rank stays inactive without its talent", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	player->SetKnownSpells({ FollowUpRank });
	CHECK_FALSE(player->HasSpell(FollowUpRank));
	CHECK(player->GetKnownSpellIds().contains(FollowUpRank));
	CHECK_FALSE(player->IsTalentGateSatisfied(ctx.Spell(FollowUpRank)));
}

TEST_CASE("Learning the talent activates a known follow-up rank", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	player->SetKnownSpells({ FollowUpRank });
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	CHECK(player->HasSpell(FollowUpRank));
	CHECK(player->IsTalentGateSatisfied(ctx.Spell(FollowUpRank)));
}

TEST_CASE("A talent reset deactivates follow-up ranks but keeps them known", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	player->AddSpell(FollowUpRank);
	REQUIRE(player->HasSpell(FollowUpRank));

	player->ResetTalents();
	CHECK_FALSE(player->HasSpell(FollowUpRank));
	CHECK(player->GetKnownSpellIds().contains(FollowUpRank));
}

TEST_CASE("Learning the talent again restores follow-up ranks", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	player->AddSpell(FollowUpRank);
	player->ResetTalents();

	REQUIRE(player->LearnTalent(GatedTalent, 0));
	CHECK(player->HasSpell(FollowUpRank));
}

TEST_CASE("Follow-up ranks survive save and load and wait for the talents", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	player->AddSpell(FollowUpRank);
	const auto& known = player->GetKnownSpellIds();
	const std::vector<uint32> saved(known.begin(), known.end());

	// Login order on the world server: known spells first, talents on spawn.
	auto loaded = ctx.MakePlayer();
	loaded->SetKnownSpells(saved);
	CHECK_FALSE(loaded->HasSpell(FollowUpRank));
	loaded->InitializeTalents({ { GatedTalent, 0 } });
	CHECK(loaded->HasSpell(FollowUpRank));
}

TEST_CASE("The class switch sequence re-activates follow-up ranks", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(GatedTalent, 0));
	player->AddSpell(FollowUpRank);

	// ChangeClass resets talents, then restores the target class's saved talents.
	player->ResetTalents();
	player->InitializeTalents({ { GatedTalent, 0 } });
	CHECK(player->HasSpell(FollowUpRank));
}

TEST_CASE("A follow-up rank stays active when its talent advances to a higher rank", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	REQUIRE(player->LearnTalent(TwoRankTalent, 0));
	player->AddSpell(TwoRankFollowUp);
	REQUIRE(player->LearnTalent(TwoRankTalent, 1));
	CHECK_FALSE(player->HasSpell(TwoRankFirst));
	CHECK(player->HasSpell(TwoRankFollowUp));
}

TEST_CASE("Saved talents that no longer fit leave follow-up ranks inactive but known", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	player->SetKnownSpells({ FollowUpRank });
	// Rank 5 does not exist: InitializeTalents resets all talents.
	player->InitializeTalents({ { GatedTalent, 5 } });
	CHECK_FALSE(player->HasSpell(FollowUpRank));
	CHECK(player->GetKnownSpellIds().contains(FollowUpRank));
}

TEST_CASE("Ordinary rank chains do not depend on talents", "[talent_gate]")
{
	GateCtx ctx;
	ctx.Init();
	auto player = ctx.MakePlayer();
	player->SetKnownSpells({ PlainRank1, PlainRank2 });
	CHECK(player->HasSpell(PlainRank2));
	CHECK(player->IsTalentGateSatisfied(ctx.Spell(PlainRank2)));
	player->ResetTalents();
	CHECK(player->HasSpell(PlainRank2));
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
cmake --build build --config Debug -t game_server_tests
```
Expected: compile error, `IsTalentGateSatisfied` is not a member of `GamePlayerS`.

- [ ] **Step 3: Declare the methods**

In `game_player_s.h`, directly after the `IsSpellActiveForCurrentClass` declaration (line ~513):
```cpp
		/// True unless the spell is a follow-up rank of a talent spell (see FindGatingTalent) whose
		/// talent is not learned. Such ranks are bought from trainers but only usable with the talent.
		[[nodiscard]] bool IsTalentGateSatisfied(const proto::SpellEntry& spell) const;

		/// Activates known follow-up ranks whose talent is learned and deactivates those whose talent is
		/// not. Deactivated ranks stay in the known-spell set, so learning the talent again restores them.
		void RefreshTalentGatedSpells();
```
Make sure both sit in a `public:` section (the trainer handler in Task 3 calls `IsTalentGateSatisfied`); if line ~513 is in a private section, place them in the public section next to `LearnTalent` (line ~380) instead.

- [ ] **Step 4: Implement**

In `game_player_s.cpp` add the include near the other `game/` includes:
```cpp
#include "game/talent_rank_gate.h"
```

In `IsSpellActiveForCurrentClass`, before the final `return true;`:
```cpp
		// A follow-up rank of a talent spell (e.g. Fireball rank 2) is only usable while its talent is
		// learned. It stays in the known-spell set, so learning the talent again restores it.
		if (!IsTalentGateSatisfied(spell))
		{
			return false;
		}
```

Add after `IsSpellActiveForCurrentClass`:
```cpp
	bool GamePlayerS::IsTalentGateSatisfied(const proto::SpellEntry& spell) const
	{
		const auto* talent = FindGatingTalent(spell, m_project.spells, m_project.talents);
		return talent == nullptr || HasTalent(talent->id());
	}

	void GamePlayerS::RefreshTalentGatedSpells()
	{
		// Collect first: AddSpell and RemoveSpell modify the spell sets.
		std::vector<uint32> toActivate;
		std::vector<uint32> toDeactivate;
		for (const uint32 spellId : m_knownSpellIds)
		{
			const auto* spell = m_project.spells.getById(spellId);
			if (!spell || FindGatingTalent(*spell, m_project.spells, m_project.talents) == nullptr)
			{
				continue;
			}

			const bool isActive = m_spells.contains(spell);
			const bool shouldBeActive = IsSpellActiveForCurrentClass(*spell);
			if (shouldBeActive && !isActive)
			{
				toActivate.push_back(spellId);
			}
			else if (!shouldBeActive && isActive)
			{
				toDeactivate.push_back(spellId);
			}
		}

		for (const uint32 spellId : toDeactivate)
		{
			RemoveSpell(spellId);
		}

		for (const uint32 spellId : toActivate)
		{
			AddSpell(spellId);
		}
	}
```

In `ResetTalents`, after `m_talents.clear();` and before `RefreshStats();`:
```cpp
		// Follow-up ranks of the removed talents are no longer usable (they stay known).
		RefreshTalentGatedSpells();
```

In `LearnTalent`, after `UpdateTalentPoints();` and before `return true;`:
```cpp
		// Follow-up ranks bought earlier become usable again.
		RefreshTalentGatedSpells();
```
`InitializeTalents` restores through `LearnTalent` and resets through `ResetTalents`, so it needs no call of its own.

- [ ] **Step 5: Run the tests**

```powershell
cmake --build build --config Debug -t game_server_tests
.\bin\Debug\game_server_tests.exe "[talent_gate]"
.\bin\Debug\game_server_tests.exe "[talents]"
```
Expected: all pass (11 `[talent_gate]` cases, existing `[talents]` unchanged).

- [ ] **Step 6: Run the whole suite**

```powershell
.\bin\Debug\game_server_tests.exe
```
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add src/shared/game_server/objects/game_player_s.h src/shared/game_server/objects/game_player_s.cpp src/tests/game_server_tests/talent_rank_gate_test.cpp
git commit -m "Keep follow-up ranks of talent spells dormant while the talent is not learned"
```

---

### Task 3: Trainer purchase gate (world server)

**Files:**
- Modify: `src/world_server/player_npc_handlers.cpp:560-575` (`Player::OnTrainerBuySpell`)

**Interfaces:**
- Consumes: `GamePlayerS::IsTalentGateSatisfied` (Task 2).

- [ ] **Step 1: Add the gate**

In `OnTrainerBuySpell`, directly after the `if (!spellEntry) { ... }` block and before the class-level check:
```cpp
				// Follow-up ranks of a talent spell need the talent. The client never asks for this (its
				// trainer list shows the rank locked), so no error code is sent. Checked before any money
				// is taken: a dormant rank the player already owns must not be sold twice.
				if (!m_character->IsTalentGateSatisfied(*spellEntry))
				{
					WLOG("Player " << log_hex_digit(m_character->GetGuid()) << " tried to buy spell " << spellId << " without its required talent");
					return;
				}
```

- [ ] **Step 2: Build the world server**

```powershell
cmake --build build --config Debug -t world_server
```
Expected: builds without warnings in this file.

- [ ] **Step 3: Commit**

```bash
git add src/world_server/player_npc_handlers.cpp
git commit -m "Refuse trainer purchases of talent follow-up ranks without the talent"
```

---

### Task 4: Client trainer UI

**Files:**
- Modify: `src/mmo_client/systems/trainer_client.h`
- Modify: `src/mmo_client/systems/trainer_client.cpp`
- Modify: `src/mmo_client/client_application.cpp:251`
- Modify: `src/mmo_client/game_script.cpp` (trainer Lua bindings, ~1514-1533)
- Modify: `data/client/Interface/GameUI/TrainerFrame.lua`
- Modify: `data/client/Locales/Locale_{deDE,enUS,frFR,ruRU}/Localization.txt`

**Interfaces:**
- Consumes: `FindGatingTalent` (Task 1).
- Produces: `const proto_client::TalentEntry* TrainerClient::GetRequiredTalent(uint32 index) const;`, `bool TrainerClient::HasRequiredTalent(uint32 index) const;`, Lua `GetTrainerSpellRequiredTalent(index)` returning `(name, learned)`, with `name == ""` and `learned == true` for entries that need no talent.

- [ ] **Step 1: TrainerClient gets the talent manager**

`trainer_client.h`: change the constructor and add the member and methods.
```cpp
		TrainerClient(RealmConnector& connector, const proto_client::SpellManager& spells, const proto_client::TalentManager& talents);
```
Public section, after `BuySpell`:
```cpp
		/// Returns the talent the trainer entry's spell depends on, or nullptr if it needs none.
		[[nodiscard]] const proto_client::TalentEntry* GetRequiredTalent(uint32 index) const;

		/// True when the entry needs no talent or the active player has learned any rank of it.
		[[nodiscard]] bool HasRequiredTalent(uint32 index) const;
```
Private members:
```cpp
		const proto_client::TalentManager& m_talents;
```

`client_application.cpp:251`:
```cpp
		context.trainerClient = std::make_unique<TrainerClient>(realmConnector, context.project->spells, context.project->talents);
```

- [ ] **Step 2: Implement in trainer_client.cpp**

Include `"game/talent_rank_gate.h"`. Constructor initializer list gets `, m_talents(talents)`. Add:
```cpp
	const proto_client::TalentEntry* TrainerClient::GetRequiredTalent(const uint32 index) const
	{
		if (index >= m_trainerSpells.size() || m_trainerSpells[index].spell == nullptr)
		{
			return nullptr;
		}

		return FindGatingTalent(*m_trainerSpells[index].spell, m_spells, m_talents);
	}

	bool TrainerClient::HasRequiredTalent(const uint32 index) const
	{
		const proto_client::TalentEntry* talent = GetRequiredTalent(index);
		if (talent == nullptr)
		{
			return true;
		}

		const std::shared_ptr<GamePlayerC>& player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return false;
		}

		// A talent is learned when the player knows any of its rank spells (as TalentClient does).
		for (const uint32 rankSpellId : talent->ranks())
		{
			if (player->HasSpell(rankSpellId))
			{
				return true;
			}
		}

		return false;
	}
```
In `BuySpell`, after the money check and before the skill TODO:
```cpp
		// Follow-up ranks of a talent spell need the talent; the trainer list shows them locked.
		if (!HasRequiredTalent(index))
		{
			return;
		}
```

- [ ] **Step 3: Lua export**

In `game_script.cpp`, after the `GetTrainerSpellReqLevel` binding:
```cpp
					   luabind::def<std::function<void(int32, String &, bool &)>>("GetTrainerSpellRequiredTalent", [this](int32 slot, String &out_name, bool &out_learned)
														   {
															   out_name.clear();
															   out_learned = true;
															   if (slot < 0 || slot >= static_cast<int32>(this->m_trainerClient.GetNumTrainerSpells()))
															   {
																   return;
															   }

															   const auto* talent = this->m_trainerClient.GetRequiredTalent(static_cast<uint32>(slot));
															   if (talent == nullptr || talent->ranks_size() == 0)
															   {
																   return;
															   }

															   if (const auto* rankSpell = this->m_project.spells.getById(talent->ranks(0)))
															   {
																   out_name = GetLocalizedString(rankSpell->name(), rankSpell->name_loc(), GetClientLocale());
															   }
															   out_learned = this->m_trainerClient.HasRequiredTalent(static_cast<uint32>(slot));
														   }, luabind::joined<luabind::pure_out_value<2>, luabind::pure_out_value<3>>()),
```
(`m_project` is `GameScript`'s `const proto_client::Project&`, see `game_script.h:171`.)

- [ ] **Step 4: Build the client**

```powershell
cmake --build build --config Debug -t mmo_client
```
Expected: builds. If other `TrainerClient` constructions exist (`grep -rn "TrainerClient>(" src`), update them the same way.

- [ ] **Step 5: TrainerFrame.lua**

In `TrainerSpellState`, read the talent requirement and add the state between LEARNED and LOCKED:
```lua
local function TrainerSpellState(index)
	local id, name, icon, cost, known = GetTrainerSpellInfo(index);
	local requiredLevel = GetTrainerSpellReqLevel(index) or 0;
	local talentName, talentLearned = GetTrainerSpellRequiredTalent(index);
	local player = GetUnit("player");
	local level = 0;
	if player then
		level = IsTrainerClassTrainer() and player:GetActiveClassLevel() or player:GetLevel();
	end
	local state = "TRAINER_STATE_AVAILABLE";
	if known then
		state = "TRAINER_STATE_LEARNED";
	elseif talentName ~= "" and not talentLearned then
		state = "TRAINER_STATE_TALENT";
	elseif not player or level < requiredLevel then
		state = "TRAINER_STATE_LOCKED";
	elseif UnitMoney("player") < cost then
		state = "TRAINER_STATE_MONEY";
	end
	local stateText = Localize(state);
	if state == "TRAINER_STATE_TALENT" then
		stateText = string.format(Localize("TRAINER_REQUIRES_TALENT"), talentName);
	end
	return { index = index, id = id, name = name, icon = icon, cost = cost,
		known = known, requiredLevel = requiredLevel, state = state, stateText = stateText,
		available = id > 0 and state == "TRAINER_STATE_AVAILABLE" };
end
```
Replace both `Localize(entry.state)` uses (preview `TrainerPreviewState:SetText(...)` and row `_G["TrainerRowState" .. slot]:SetText(...)`) with `entry.stateText`. The existing colour logic already paints non-available, non-known entries red, and the "hide unavailable" filter already hides them.

Add `"PLAYER_TALENT_UPDATE"` to the refresh event list:
```lua
	for _, event in ipairs({"MONEY_CHANGED", "SPELL_LEARNED", "PLAYER_LEVEL_CHANGED", "PLAYER_KNOWN_CLASSES_CHANGED", "PLAYER_TALENT_UPDATE"}) do
```

- [ ] **Step 6: Localization**

After the `TRAINER_STATE_MONEY` line in each file:
- `Locale_deDE`: `(key = "TRAINER_STATE_TALENT", string = "Talent fehlt")` and `(key = "TRAINER_REQUIRES_TALENT", string = "Benötigt Talent: %s")`
- `Locale_enUS`: `"Talent missing"` and `"Requires talent: %s"`
- `Locale_frFR`: `"Talent manquant"` and `"Nécessite le talent : %s"`
- `Locale_ruRU`: `"Нет таланта"` and `"Требуется талант: %s"`

Match the file's existing indentation (one tab) and encoding (UTF-8).

- [ ] **Step 7: Commit (submodule, then main repo)**

```bash
git -C data/client add Interface/GameUI/TrainerFrame.lua Locales
git -C data/client commit -m "Trainer: show spell ranks that need a talent as locked"
git add src/mmo_client data/client
git commit -m "Client trainer: lock talent follow-up ranks until the talent is learned"
```

---

### Task 5: Data authoring script and data changes

**Files:**
- Create: `tools/briarwatch_balance/author_briarwatch_balance.py`
- Modify (by the script): `data/editor/data/units.data`, `data/editor/data/spells.data`, `data/editor/data/trainers.data`, `data/client/ClientDB/spells.data`

**Interfaces:**
- Consumes: nothing from code tasks (data only), but must land together with Tasks 2-4.

- [ ] **Step 1: Write the script**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Briarwatch March creature retune and Fireball rank 2 (see
docs/superpowers/specs/2026-10-12-briarwatch-balance-talent-ranks-design.md).

    py tools/briarwatch_balance/author_briarwatch_balance.py            # show the changes only
    py tools/briarwatch_balance/author_briarwatch_balance.py --apply    # write editor data + ClientDB

Idempotent: re-running sets the same values. ClientDB receives a byte copy of the editor spells
blob, exactly what mmo_edit's ExportToClient does.

Easy to undo by accident:
* Fireball rank 2 keeps rank 1's familyflags (8): the fire talents select spells by family flags.
* Rank 2's baseid (4) is what ties it to the Fireball talent on server and client; without it the
  rank would be usable after a talent reset.
"""

import argparse
import importlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EDITOR = ROOT / "data/editor/data"
CLIENT_DB = ROOT / "data/client/ClientDB"
sys.path.insert(0, str(ROOT / ".agents/skills/mmo-spell-designer/scripts"))
from proto_runtime import find_protoc  # noqa: E402

FIREBALL, FIREBALL_R2 = 4, 1427
MAGE_TRAINER = 1
SHADOWBOLT = 180

# unit id: field overrides (spec, Part 1)
UNITS = {
    67: {"minmeleedmg": 3.5, "damagePerLevel": 0.75},
    48: {"eliteStatMultiplier": 1.0, "minmeleedmg": 4.0, "damagePerLevel": 0.75},
    49: {"eliteStatMultiplier": 1.0},
    66: {"eliteStatMultiplier": 1.0, "minmeleedmg": 3.0, "damagePerLevel": 0.7},
    50: {"eliteStatMultiplier": 1.0, "minmeleedmg": 4.0, "damagePerLevel": 0.75},
    51: {"eliteStatMultiplier": 1.0},
    55: {"minmeleedmg": 4.0, "damagePerLevel": 0.75},
    56: {"minmeleedmg": 4.0, "damagePerLevel": 0.75},
    57: {"eliteStatMultiplier": 1.0, "minmeleedmg": 3.0, "damagePerLevel": 0.7},
}
HEXERS = (49, 51)
HEXER_BOLT_COOLDOWN = (5000, 7000)


def find_any_protoc():
    try:
        return find_protoc(ROOT)
    except FileNotFoundError:
        common = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute",
                                 "--git-common-dir"], capture_output=True, text=True, check=True)
        return find_protoc(Path(common.stdout.strip()).parent)


def load_modules():
    out = Path(tempfile.mkdtemp(prefix="briarwatch_proto_"))
    proto_dir = ROOT / "src/shared/proto_data"
    subprocess.run([str(find_any_protoc()), f"-I{proto_dir}", f"--python_out={out}",
                    *sorted(p.name for p in proto_dir.glob("*.proto"))], check=True, cwd=proto_dir)
    sys.path.insert(0, str(out))
    return {name: importlib.import_module(f"{name}_pb2") for name in ("units", "spells", "trainers")}


def read(module, container, file_name):
    message = getattr(module, container)()
    message.ParseFromString((EDITOR / file_name).read_bytes())
    return message


def by_id(container, entry_id):
    for entry in container.entry:
        if entry.id == entry_id:
            return entry
    return None


def retune_units(units, log):
    for unit_id, fields in UNITS.items():
        unit = by_id(units, unit_id)
        assert unit is not None, f"unit {unit_id} missing"
        for field, value in fields.items():
            old = getattr(unit, field)
            if abs(old - value) > 1e-6:
                log.append(f"unit {unit_id} {unit.name}: {field} {old:g} -> {value:g}")
            setattr(unit, field, value)
        if unit_id in HEXERS:
            spells = [s for s in unit.creaturespells if s.spellid == SHADOWBOLT]
            assert spells, f"unit {unit_id} has no Shadowbolt"
            for spell in spells:
                if (spell.mincooldown, spell.maxcooldown) != HEXER_BOLT_COOLDOWN:
                    log.append(f"unit {unit_id} {unit.name}: Shadowbolt cooldown "
                               f"{spell.mincooldown}/{spell.maxcooldown} -> {HEXER_BOLT_COOLDOWN}")
                spell.mincooldown, spell.maxcooldown = HEXER_BOLT_COOLDOWN


def author_fireball(spells, log):
    rank1 = by_id(spells, FIREBALL)
    assert rank1 is not None and rank1.name == "Fireball"
    if (rank1.rank, rank1.baseid) != (1, FIREBALL):
        log.append(f"spell {FIREBALL}: rank/baseid {rank1.rank}/{rank1.baseid} -> 1/{FIREBALL}")
    rank1.rank = 1
    rank1.baseid = FIREBALL

    rank2 = by_id(spells, FIREBALL_R2)
    if rank2 is None:
        rank2 = spells.entry.add()
        log.append(f"spell {FIREBALL_R2}: created Fireball rank 2")
    elif rank2.name != "Fireball":
        raise SystemExit(f"spell {FIREBALL_R2} exists and is not Fireball: {rank2.name}")
    rank2.CopyFrom(rank1)
    rank2.id = FIREBALL_R2
    rank2.rank = 2
    rank2.baseid = FIREBALL
    rank2.casttime = 1800
    rank2.cost = 45
    rank2.spelllevel = 6
    rank2.baselevel = 6
    rank2.maxlevel = 10
    direct, periodic = rank2.effects[0], rank2.effects[1]
    assert direct.type == 2 and periodic.type == 6 and periodic.aura == 13
    direct.basepoints = 29
    direct.diesides = 9
    direct.pointsperlevel = 0.6
    periodic.basepoints = 4


def add_trainer_entry(trainers, log):
    trainer = by_id(trainers, MAGE_TRAINER)
    assert trainer is not None
    entry = next((s for s in trainer.spells if s.spell == FIREBALL_R2), None)
    if entry is None:
        entry = trainer.spells.add()
        log.append(f"trainer {MAGE_TRAINER}: teaches {FIREBALL_R2} at level 6 for 450")
    entry.spell = FIREBALL_R2
    entry.spellcost = 450
    entry.reqlevel = 6


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    modules = load_modules()
    units = read(modules["units"], "Units", "units.data")
    spells = read(modules["spells"], "Spells", "spells.data")
    trainers = read(modules["trainers"], "Trainers", "trainers.data")

    log = []
    retune_units(units, log)
    author_fireball(spells, log)
    add_trainer_entry(trainers, log)
    print("\n".join(log) if log else "no changes")

    if args.apply:
        (EDITOR / "units.data").write_bytes(units.SerializeToString())
        (EDITOR / "spells.data").write_bytes(spells.SerializeToString())
        (EDITOR / "trainers.data").write_bytes(trainers.SerializeToString())
        shutil.copyfile(EDITOR / "spells.data", CLIENT_DB / "spells.data")
        print("written")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Dry run**

```bash
python tools/briarwatch_balance/author_briarwatch_balance.py
```
Expected: one line per unit field change (9 units, 2 Shadowbolt cooldowns), `spell 4: rank/baseid 0/0 -> 1/4`, `spell 1427: created Fireball rank 2`, `trainer 1: teaches 1427 ...`.

- [ ] **Step 3: Apply and verify idempotence**

```bash
python tools/briarwatch_balance/author_briarwatch_balance.py --apply
python tools/briarwatch_balance/author_briarwatch_balance.py
```
Expected: first run prints the changes and `written`; second run prints `no changes` (spell 1427 and the trainer entry already exist, all unit values already match). If not, fix the script before continuing.

- [ ] **Step 4: Check the ClientDB copy is byte-identical**

```bash
cmp data/editor/data/spells.data data/client/ClientDB/spells.data && echo same
```
Expected: `same`.

- [ ] **Step 5: Commit (submodules, then main repo)**

```bash
git -C data/editor add data/units.data data/spells.data data/trainers.data
git -C data/editor commit -m "Briarwatch creature retune, Fireball rank 2 at the mage trainer"
git -C data/client add ClientDB/spells.data
git -C data/client commit -m "ClientDB: Fireball rank 2"
git add tools/briarwatch_balance/author_briarwatch_balance.py data/editor data/client
git commit -m "Retune Briarwatch March creatures, add Fireball rank 2"
```

---

### Task 6: Re-simulate with the real data and update the spec

**Files:**
- Modify: `docs/superpowers/specs/2026-10-12-briarwatch-balance-talent-ranks-design.md` (table "Expected result")

- [ ] **Step 1: Run the balance simulation against the worktree data**

Use the session scratchpad simulation (`$TEMP/bw/sim.py` with `load.py` pointed at `data/editor/data` of this worktree, Fireball rank 2 values from spell 1427). Run the four opponents of the spec table, level-6 int-only mage, 2000 runs each.
Expected: within a few points of the spec's predicted values; Cutthroat L7 ≥ 95 % win, Marauder L7 ≥ 90 %.

- [ ] **Step 2: Replace the `~` values in the spec table with the measured ones, commit**

```bash
git add docs/superpowers/specs/2026-10-12-briarwatch-balance-talent-ranks-design.md
git commit -m "Spec: measured win rates with Fireball rank 2"
```

---

### Task 7: Gate

- [ ] **Step 1: Run the fast gate**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast
```
Expected: protocol check green (no wire change), build, unit tests, tool tests, Linux build/tests all green.

- [ ] **Step 2: Report to the user**

Summarize changes, test results, and that pushing the `data/editor`/`data/client` branches and merging via `/ship` needs their go-ahead. The E2E harness has no talent-learn/reset or trainer-buy API, so the talent flow is covered by unit tests only; say so.
