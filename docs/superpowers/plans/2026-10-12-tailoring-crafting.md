# Professions & Tailoring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the crafting UI mock with a real, server-authoritative profession system and ship Tailoring (learnable via a 3-quest chain, levelled by crafting).

**Architecture:** A profession is a `SkillEntry` (shared proto, exported to the ClientDB). A passive unlock spell with the `TradeSkill` effect grants the skill and its cap; recipes are `TradeSpell`-flagged CreateItem spells carrying skill id, category and difficulty thresholds. The world server keeps `{skill → value, max}` per character in a `TradeSkillBook`, persisted by the realm in `character_skills` and sent to the client with two new packets. The client hides recipe spells from the spellbook and exposes professions/recipes to Lua, where `Crafting.lua` drives the existing UI.

**Tech Stack:** C++17, protobuf (proto2), Catch2 (`src/tests/<lib>_tests`), luabind, Lua/XML frame UI, MySQL migrations, E2E Lua scenarios.

**Spec:** `docs/superpowers/specs/2026-10-12-tailoring-crafting-design.md`

## Global Constraints

- Code style: Allman braces, braces on every `if`, tabs, `m_camelCase` members, `PascalCase` methods, `#pragma once`, Doxygen on public members, header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on every new source file.
- No exceptions; use `ASSERT`/`VERIFY`/`DLOG`/`WLOG`/`ELOG`.
- `spell_effects` and `spell_cast_result` enums are append-only. Reuse `spell_effects::TradeSkill = 39`; do not add a new effect.
- New proto fields: `SpellEntry` 75 `skillrequired`, 76 `skillgreen`, 77 `craftcategory` — identical numbers in `src/shared/proto_data/spells.proto` and `src/shared/client_data/spells.proto` (ClientDB is a binary copy of the server blob).
- Localized proto strings use `repeated LocalizedString <field>_loc = 200+`, same number in both packages.
- Game `ProtocolVersion` 0x14 → 0x15, then `python tools/protocol_version_check.py --update`, row in `docs/protocol_versions.md`.
- Realm migration is additive only: `data/realm/updates/20261012_1_character_skills.sql`.
- UI strings live in `data/client/Locales/<locale>/Localization.txt` for enUS, deDE, frFR, ruRU; no hard-coded strings in Lua/XML.
- Engine/content tooling goes into C++ `mmo_edit`; Python only as uncommitted scratch.
- Quest rewards and collect objectives must never be grey items (`quest-item-quality-rule`).
- `data/editor` and `data/client` are submodules: commit inside them on a `feature/tailoring-crafting` branch, land on their `master` before the superproject pointer is shipped.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Login re-applies passive spells** — a character relogging with Tailoring 40/75 must keep 40, not be reset to 1 by the re-cast `TradeSkill` effect. Pinned in Task 3 (`LearnOrRaise` idempotence) and Task 4 (handler on a known skill).
2. **Full bags at cast finish** — bags filling up during a 3 s cast must fail the craft without consuming reagents. Pinned in Task 5 (`FailedInventoryFull` path is checked again before `ConsumeReagents`).
3. **Skill at cap / grey recipes** — no gain past `max`, none at or above grey, cap raised by a higher rank keeps the value. Pinned in Task 1 and Task 3.
4. **Old data without the new fields** — existing CreateItem spells (Smelt Copper, Minor Potion) with `skill = 0` must cast exactly as before, with no skill checks and no skill-up. Pinned in Task 5.
5. **Character stream compatibility** — `CharacterData` and `GamePlayerS` streams gain a trailing list; a truncated/older stream must still parse the older fields. Pinned in Task 3 (round trip + empty list).

---

## File Map

| File | Responsibility |
|---|---|
| `src/shared/game/trade_skill.h` (new) | Pure difficulty/chance rules shared by client and server |
| `src/shared/proto_data/skills.proto`, `src/shared/client_data/skills.proto` (new) | Profession + categories |
| `src/shared/proto_data/spells.proto`, `src/shared/client_data/spells.proto` | Recipe fields 75–77 |
| `src/shared/client_data/project.h` | Optional `skills` manager |
| `src/shared/game_server/trade_skill_book.h/.cpp` (new) | Per-character skill state + learn/raise/skill-up |
| `src/shared/game_server/character_data.h` | `tradeSkills` in the realm↔world payload |
| `src/shared/game_server/objects/game_player_s.{h,cpp}` | Owns the book, stream ops, signals |
| `src/shared/game_server/spells/spell_effects.{h,cpp}` | `HandleTradeSkill` |
| `src/shared/game_server/spells/single_cast_state.cpp` | Skill + bag-space validation, skill-up |
| `src/shared/game/spell.h`, `src/shared/game/vendor.h` | New cast results / trainer result |
| `src/shared/game_protocol/game_protocol.h` | Opcodes, version |
| `src/world_server/player.{h,cpp}`, `player_npc_handlers.cpp` | Packets, load order, trainer gate |
| `src/realm_server/{database.h,mysql_database.*,world.cpp,player.cpp}` | Persistence |
| `src/shared/bot_core/*`, `src/e2e_client/scenario_engine.cpp` | Bot state + `GetTradeSkill` |
| `src/shared/game_client/trade_skill_list.{h,cpp}` (new) | Build profession/recipe lists from known spells |
| `src/shared/game_client/game_unit_c.cpp`, `game_player_c.h` | Spellbook filter, skill cache |
| `src/mmo_client/game_states/world_state.{h,cpp}`, `game_script.cpp`, `systems/trainer_client.cpp` | Packets, Lua API, trainer check |
| `src/mmo_edit/editor_windows/skill_editor_window.{h,cpp}` (new), `spell_editor_window.cpp`, `main_window.cpp`, `mmo_edit.cpp`, `data_navigator_window.cpp` | Authoring + ClientDB export |
| `data/client/Interface/GameUI/Crafting.{xml,lua}`, `GameMenuBar*`, `Bindings.xml`, `DefaultBindings.cfg`, `TrainerFrame.lua`, `Locales/*` | UI |
| `data/editor/data/*.data`, `data/client/ClientDB/*` | Content |
| `e2e/scenarios/tailoring_profession.lua` (new) | End-to-end |

---

### Task 0: Workspace setup

**Files:** none committed (environment only).

- [ ] **Step 1: Initialise submodules from the local checkout**

```powershell
foreach ($p in 'deps/asio','deps/imgui','deps/glfw','deps/recastnavigation','deps/json','data/editor','data/client') { git config "submodule.$p.url" "H:/mmo/$p" }
git -c protocol.file.allow=always submodule update --init
```

Expected: all seven paths populated (`data/client` ~1.6 GB working tree).

- [ ] **Step 2: Branch the data submodules**

```powershell
git -C data/editor switch -c feature/tailoring-crafting
git -C data/client switch -c feature/tailoring-crafting
```

- [ ] **Step 3: Configure the build mirroring the main cache**

Read the `MMO_*` options from `H:/mmo/build/CMakeCache.txt`, then:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON
cmake --build build --config Debug -t all_tests --parallel
```

Expected: build succeeds; `cd build; ctest -C Debug` green (baseline).

- [ ] **Step 4: Python for content tooling**

If `python` is the Store shim, create `<scratchpad>/venv` from `%LOCALAPPDATA%\Programs\Python\Python314\python.exe` and `pip install protobuf numpy Pillow`; prepend its `Scripts` to `$env:Path` for skill scripts and the gate. Copy `protoc.exe` into `build/_deps/protobuf-build/Release/` if the skill scripts cannot find it.

---

### Task 1: Shared trade-skill rules

**Files:**
- Create: `src/shared/game/trade_skill.h`
- Test: `src/tests/game_tests/test_trade_skill.cpp`

**Interfaces:**
- Produces: `mmo::trade_skill_difficulty::Type { Impossible, Orange, Yellow, Green, Grey }`, `TradeSkillThresholds { uint32 required, yellow, green, grey; }`, `TradeSkillThresholds TradeSkillThresholdsOf(const SpellT&)` (template, works for `proto::SpellEntry` and `proto_client::SpellEntry`), `TradeSkillDifficulty GetTradeSkillDifficulty(uint32 value, const TradeSkillThresholds&)`, `float GetTradeSkillUpChance(uint32 value, const TradeSkillThresholds&)`.

Threshold normalisation (so sparse data stays sane): a `grey` of 0 means "never trivial" — yellow, green and grey all become `UINT32_MAX`, so the recipe stays orange with a certain skill-up. Otherwise `yellow = max(yellow, required)`, `grey = max(grey, yellow)`, `green` defaults to the midpoint of yellow and grey when 0 and is clamped into `[yellow, grey]`.

- [ ] **Step 1: Write the failing test**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/trade_skill.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	TradeSkillThresholds Make(const uint32 required, const uint32 yellow, const uint32 green, const uint32 grey)
	{
		return NormalizeTradeSkillThresholds({ required, yellow, green, grey });
	}
}

TEST_CASE("Trade skill difficulty follows the four colour bands", "[trade_skill]")
{
	const TradeSkillThresholds t = Make(10, 25, 35, 45);
	CHECK(GetTradeSkillDifficulty(9, t) == trade_skill_difficulty::Impossible);
	CHECK(GetTradeSkillDifficulty(10, t) == trade_skill_difficulty::Orange);
	CHECK(GetTradeSkillDifficulty(24, t) == trade_skill_difficulty::Orange);
	CHECK(GetTradeSkillDifficulty(25, t) == trade_skill_difficulty::Yellow);
	CHECK(GetTradeSkillDifficulty(35, t) == trade_skill_difficulty::Green);
	CHECK(GetTradeSkillDifficulty(45, t) == trade_skill_difficulty::Grey);
	CHECK(GetTradeSkillDifficulty(300, t) == trade_skill_difficulty::Grey);
}

TEST_CASE("Trade skill-up chance is certain below yellow and falls linearly to grey", "[trade_skill]")
{
	const TradeSkillThresholds t = Make(10, 25, 35, 45);
	CHECK(GetTradeSkillUpChance(10, t) == Approx(1.0f));
	CHECK(GetTradeSkillUpChance(24, t) == Approx(1.0f));
	CHECK(GetTradeSkillUpChance(25, t) == Approx(1.0f));
	CHECK(GetTradeSkillUpChance(35, t) == Approx(0.5f));
	CHECK(GetTradeSkillUpChance(44, t) == Approx(0.05f));
	CHECK(GetTradeSkillUpChance(45, t) == Approx(0.0f));
	CHECK(GetTradeSkillUpChance(9, t) == Approx(0.0f));
}

TEST_CASE("Trade skill thresholds are normalised", "[trade_skill]")
{
	const TradeSkillThresholds missingGreen = Make(1, 20, 0, 40);
	CHECK(missingGreen.green == 30);

	const TradeSkillThresholds inverted = Make(30, 10, 5, 20);
	CHECK(inverted.yellow == 30);
	CHECK(inverted.green >= inverted.yellow);
	CHECK(inverted.grey >= inverted.green);

	const TradeSkillThresholds neverTrivial = Make(0, 0, 0, 0);
	CHECK(GetTradeSkillDifficulty(500, neverTrivial) == trade_skill_difficulty::Orange);
	CHECK(GetTradeSkillUpChance(500, neverTrivial) == Approx(1.0f));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Debug -t game_tests; bin/Debug/game_tests "[trade_skill]"`
Expected: compile error, `game/trade_skill.h` not found.

- [ ] **Step 3: Implement**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>
#include <limits>

namespace mmo
{
	namespace trade_skill_difficulty
	{
		/// Difficulty colour of a recipe relative to the crafter's skill value.
		enum Type
		{
			/// Skill is below the recipe's requirement; the recipe cannot be crafted.
			Impossible,
			/// Guaranteed skill-up.
			Orange,
			/// Likely skill-up.
			Yellow,
			/// Unlikely skill-up.
			Green,
			/// No skill-up.
			Grey
		};
	}

	typedef trade_skill_difficulty::Type TradeSkillDifficulty;

	/// Skill values at which a recipe changes colour.
	struct TradeSkillThresholds
	{
		uint32 required = 0;
		uint32 yellow = 0;
		uint32 green = 0;
		uint32 grey = 0;
	};

	/// Orders the thresholds (required <= yellow <= green <= grey), fills a missing green with the
	/// midpoint of yellow and grey and treats a grey of 0 as "never trivial".
	inline TradeSkillThresholds NormalizeTradeSkillThresholds(TradeSkillThresholds t)
	{
		constexpr uint32 never = std::numeric_limits<uint32>::max();
		if (t.grey == 0)
		{
			t.yellow = never;
			t.green = never;
			t.grey = never;
			return t;
		}

		t.yellow = std::max(t.yellow, t.required);
		t.grey = std::max(t.grey, t.yellow);
		if (t.green == 0)
		{
			t.green = t.yellow + (t.grey - t.yellow) / 2;
		}

		t.green = std::clamp(t.green, t.yellow, t.grey);
		return t;
	}

	/// Reads the thresholds of a recipe spell. Works for server and client spell entries.
	template <typename SpellT>
	TradeSkillThresholds TradeSkillThresholdsOf(const SpellT& spell)
	{
		return NormalizeTradeSkillThresholds({ spell.skillrequired(), spell.trivialskilllow(), spell.skillgreen(), spell.trivialskillhigh() });
	}

	/// Returns the colour of a recipe for a crafter with the given skill value.
	inline TradeSkillDifficulty GetTradeSkillDifficulty(const uint32 value, const TradeSkillThresholds& t)
	{
		if (value < t.required)
		{
			return trade_skill_difficulty::Impossible;
		}
		if (value >= t.grey)
		{
			return trade_skill_difficulty::Grey;
		}
		if (value >= t.green)
		{
			return trade_skill_difficulty::Green;
		}
		if (value >= t.yellow)
		{
			return trade_skill_difficulty::Yellow;
		}

		return trade_skill_difficulty::Orange;
	}

	/// Returns the chance (0..1) that crafting the recipe raises the skill: 1 up to yellow, then
	/// falling linearly to 0 at grey; 0 when the recipe cannot be crafted.
	inline float GetTradeSkillUpChance(const uint32 value, const TradeSkillThresholds& t)
	{
		if (value < t.required || value >= t.grey)
		{
			return 0.0f;
		}
		if (value <= t.yellow || t.grey == std::numeric_limits<uint32>::max())
		{
			return 1.0f;
		}

		return static_cast<float>(t.grey - value) / static_cast<float>(t.grey - t.yellow);
	}
}
```

Note: `TradeSkillThresholdsOf` needs the proto fields from Task 2; it is only instantiated from Task 3 on, so Task 1 compiles on its own.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --config Debug -t game_tests; bin/Debug/game_tests "[trade_skill]"`
Expected: PASS (3 test cases).

- [ ] **Step 5: Commit**

```bash
git add src/shared/game/trade_skill.h src/tests/game_tests/test_trade_skill.cpp
git commit -m "Add shared trade skill difficulty and skill-up rules"
```

---

### Task 2: Data schema — skills, recipe fields, ClientDB, editor

**Files:**
- Modify: `src/shared/proto_data/skills.proto`, `src/shared/proto_data/spells.proto`
- Create: `src/shared/client_data/skills.proto`
- Modify: `src/shared/client_data/spells.proto`, `src/shared/client_data/project.h`, `src/shared/client_data/CMakeLists.txt` (only if protos are listed explicitly)
- Create: `src/mmo_edit/editor_windows/skill_editor_window.{h,cpp}`
- Modify: `src/mmo_edit/mmo_edit.cpp:258`, `src/mmo_edit/editor_windows/data_navigator_window.cpp:77,394`, `src/mmo_edit/main_window.cpp:964` (`sharedManagers`), `src/mmo_edit/editor_windows/spell_editor_window.cpp` (reagent section ~1040–1140), `src/mmo_edit/localization_io.cpp` (Export/ImportTranslations dispatch)
- Test: `src/tests/client_data_tests/test_skills_wire_compat.cpp`

**Interfaces:**
- Produces: `proto::SkillEntry { id, name, category, cost, spells[], categories[] { id, name, sortorder, name_loc[] }, name_loc[] }`, same shape in `proto_client`; `proto_client::Project::skills` (`SkillManager`); `SpellEntry::skillrequired()/skillgreen()/craftcategory()`; constant `skill_category::Profession = 1` in `src/shared/game/trade_skill.h`.

- [ ] **Step 1: Write the failing wire-compat test**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shared/client_data/proto_client/skills.pb.h"
#include "shared/client_data/proto_client/spells.pb.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Client skill entries parse the categories and localized names", "[client_data][skills]")
{
	proto_client::SkillEntry source;
	source.set_id(1);
	source.set_name("Tailoring");
	source.set_category(1);
	source.add_spells(500);
	auto* category = source.add_categories();
	category->set_id(2);
	category->set_name("Bags");
	category->set_sortorder(20);
	auto* loc = source.add_name_loc();
	loc->set_locale(1);
	loc->set_value("Schneiderei");

	proto_client::SkillEntry parsed;
	REQUIRE(parsed.ParseFromString(source.SerializeAsString()));
	CHECK(parsed.categories_size() == 1);
	CHECK(parsed.categories(0).name() == "Bags");
	CHECK(parsed.name_loc(0).value() == "Schneiderei");
}

TEST_CASE("Client spell entries carry the recipe fields", "[client_data][skills]")
{
	proto_client::SpellEntry spell;
	spell.set_skill(1);
	spell.set_skillrequired(5);
	spell.set_skillgreen(30);
	spell.set_craftcategory(2);
	CHECK(spell.skillrequired() == 5);
	CHECK(spell.skillgreen() == 30);
	CHECK(spell.craftcategory() == 2);
}
```

Additionally add the same test against the **server** blob in `game_tests` (it links `proto_data`): serialize a `proto::SkillEntry` with categories and `name_loc` and parse it as `proto_client::SkillEntry` — only if `game_tests` can link `client_data` in the headless build; otherwise put that case behind `if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)` in `client_data_tests` by adding `proto_data` to its link list there.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S . -B build; cmake --build build --config Debug -t client_data_tests`
Expected: compile error (`skills.pb.h` missing in client_data).

- [ ] **Step 3: Implement the protos**

`src/shared/proto_data/skills.proto`:

```proto
syntax = "proto2";
package mmo.proto;

import "localization.proto";

message SkillCategory {
	required uint32 id = 1;
	required string name = 2;
	optional uint32 sortorder = 3;
	repeated LocalizedString name_loc = 200;
}

message SkillEntry {
	required uint32 id = 1;
	required string name = 2;
	required int32 category = 3;
	required uint32 cost = 4;
	/// Spells learned automatically when the skill is first learned (starter recipes).
	repeated uint32 spells = 5;
	/// Recipe categories shown in the profession window.
	repeated SkillCategory categories = 6;
	repeated LocalizedString name_loc = 200;
}

message Skills {
	repeated SkillEntry entry = 1;
}
```

`src/shared/client_data/skills.proto`: identical with `package mmo.proto_client;`.

Both `spells.proto` files, inside `SpellEntry` (next to the existing fields):

```proto
	/// Minimum profession skill to cast this recipe (orange from here).
	optional uint32 skillrequired = 75;
	/// Profession skill from which the recipe is green.
	optional uint32 skillgreen = 76;
	/// SkillCategory.id of the recipe within its skill.
	optional uint32 craftcategory = 77;
```

Before writing, `grep -n "= 7[5-7];" src/shared/*/spells.proto` must return nothing.

`src/shared/client_data/project.h`: add `#include "shared/client_data/proto_client/skills.pb.h"`, `typedef TemplateManager<mmo::proto_client::Skills, mmo::proto_client::SkillEntry> SkillManager;`, member `SkillManager skills;` and `managers.push_back(ManagerEntry("skills", skills, true));` (optional, so an older ClientDB still loads).

`src/shared/game/trade_skill.h`: add

```cpp
	namespace skill_category
	{
		/// SkillEntry.category value of a profession.
		constexpr int32 Profession = 1;
	}
```

- [ ] **Step 4: Editor support**

1. `skill_editor_window.{h,cpp}`: copy the structure of `proficiency_editor_window.{h,cpp}` (base `EditorEntryWindowBase<proto::Skills, proto::SkillEntry>`, `project.skills`, toolbar text "Skills"). `DrawDetailsImpl` edits: Name, ID (read-only), Category (`InputInt`, tooltip "1 = Profession"), Cost; a "Starter Spells" list (add by id via spell combo like the reagent combo in `spell_editor_window.cpp`, remove button per row); a "Categories" table (id, name, sortorder, add/remove; new ids = max+1). `OnNewEntry`: name "New Skill", category 1, cost 0.
2. Register it in `mmo_edit.cpp` next to the Proficiency editor and in `data_navigator_window.cpp` (icon `ICON_FA_SCISSORS`, label "Skills").
3. `main_window.cpp` `sharedManagers`: add `"skills"`.
4. `spell_editor_window.cpp`: in the reagent section add a "Profession" block: Skill combo over `m_project.skills` (None = 0), Category combo over the chosen skill's categories, and `InputScalar` for Required / Yellow (`trivialskilllow`) / Green / Grey (`trivialskillhigh`). Show the computed colour bands as text using `GetTradeSkillDifficulty` for skill values required…grey to make the bands visible.
5. `localization_io.cpp`: add `type="skill"` with `field="name"` and `type="skill_category"` with `sub=<category id>`, `field="name"`, in both Export and Import dispatch (one line each, following the existing pattern).

- [ ] **Step 5: Build and run**

Run: `cmake -S . -B build; cmake --build build --config Debug -t client_data_tests mmo_edit; bin/Debug/client_data_tests "[skills]"`
Expected: PASS; mmo_edit builds.

- [ ] **Step 6: Commit**

```bash
git add src/shared/proto_data src/shared/client_data src/shared/game/trade_skill.h src/mmo_edit src/tests/client_data_tests
git commit -m "Add profession skill data: SkillEntry categories, recipe fields, editor and ClientDB export"
```

---

### Task 3: Server trade-skill state and persistence payloads

**Files:**
- Create: `src/shared/game_server/trade_skill_book.{h,cpp}`
- Modify: `src/shared/game_server/character_data.h`, `src/shared/game_server/objects/game_player_s.{h,cpp}`
- Test: `src/tests/game_server_tests/trade_skill_book_test.cpp`

**Interfaces:**
- Consumes: `TradeSkillThresholdsOf`, `GetTradeSkillUpChance` (Task 1).
- Produces:

```cpp
struct TradeSkillData { uint32 skillId = 0; uint16 value = 0; uint16 max = 0; };
io::Writer& operator<<(io::Writer&, const TradeSkillData&);
io::Reader& operator>>(io::Reader&, TradeSkillData&);

class TradeSkillBook
{
public:
	enum class LearnResult { Learned, CapRaised, Unchanged };
	LearnResult LearnOrRaise(uint32 skillId, uint16 max);
	bool Has(uint32 skillId) const;
	const TradeSkillData* Get(uint32 skillId) const;
	/// Rolls a skill-up for a successful craft; `roll` in [0,1). Returns true if the value rose.
	bool TrySkillUp(uint32 skillId, const TradeSkillThresholds& thresholds, float roll);
	void SetAll(const std::vector<TradeSkillData>& skills);
	std::vector<TradeSkillData> GetAll() const;   // ordered by skill id
};
```

`CharacterData::tradeSkills` (`std::vector<TradeSkillData>`); `GamePlayerS::GetTradeSkills()` (`TradeSkillBook&` and const), `GamePlayerS::SetTradeSkills(const std::vector<TradeSkillData>&)`, signals `signal<void(uint32 skillId)> tradeSkillLearned;` (learn or cap change) and `signal<void(uint32 skillId)> tradeSkillChanged;` (value change).

- [ ] **Step 1: Write the failing tests**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/trade_skill_book.h"
#include "game_server/character_data.h"
#include "binary_io/vector_sink.h"
#include "binary_io/memory_source.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Learning a trade skill starts at 1 and re-learning never resets it", "[trade_skill]")
{
	TradeSkillBook book;
	CHECK(book.LearnOrRaise(1, 75) == TradeSkillBook::LearnResult::Learned);
	REQUIRE(book.Get(1) != nullptr);
	CHECK(book.Get(1)->value == 1);
	CHECK(book.Get(1)->max == 75);

	const TradeSkillThresholds orange = NormalizeTradeSkillThresholds({ 1, 50, 60, 70 });
	for (int i = 0; i < 39; ++i)
	{
		book.TrySkillUp(1, orange, 0.99f);
	}
	CHECK(book.Get(1)->value == 40);

	CHECK(book.LearnOrRaise(1, 75) == TradeSkillBook::LearnResult::Unchanged);
	CHECK(book.Get(1)->value == 40);
	CHECK(book.LearnOrRaise(1, 50) == TradeSkillBook::LearnResult::Unchanged);
	CHECK(book.Get(1)->max == 75);
	CHECK(book.LearnOrRaise(1, 150) == TradeSkillBook::LearnResult::CapRaised);
	CHECK(book.Get(1)->max == 150);
	CHECK(book.Get(1)->value == 40);
}

TEST_CASE("Skill-up respects cap, grey and the roll", "[trade_skill]")
{
	TradeSkillBook book;
	book.SetAll({ { 1, 75, 75 } });
	const TradeSkillThresholds t = NormalizeTradeSkillThresholds({ 1, 50, 60, 100 });
	CHECK_FALSE(book.TrySkillUp(1, t, 0.0f));
	CHECK(book.Get(1)->value == 75);

	book.SetAll({ { 1, 70, 75 } });
	const TradeSkillThresholds grey = NormalizeTradeSkillThresholds({ 1, 30, 40, 50 });
	CHECK_FALSE(book.TrySkillUp(1, grey, 0.0f));

	// Value 70 is green here: chance (80 - 70) / (80 - 60) = 0.5.
	const TradeSkillThresholds green = NormalizeTradeSkillThresholds({ 1, 60, 70, 80 });
	CHECK_FALSE(book.TrySkillUp(1, green, 0.6f));
	CHECK(book.TrySkillUp(1, green, 0.4f));
	CHECK(book.Get(1)->value == 71);

	CHECK_FALSE(book.TrySkillUp(99, t, 0.0f));
}

TEST_CASE("CharacterData round-trips trade skills", "[trade_skill]")
{
	CharacterData data;
	data.tradeSkills = { { 1, 12, 75 }, { 3, 1, 75 } };

	std::vector<char> buffer;
	io::VectorSink sink(buffer);
	io::Writer writer(sink);
	writer << data;

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);
	CharacterData read;
	REQUIRE(reader >> read);
	REQUIRE(read.tradeSkills.size() == 2);
	CHECK(read.tradeSkills[0].skillId == 1);
	CHECK(read.tradeSkills[0].value == 12);
	CHECK(read.tradeSkills[1].max == 75);
}
```

Check `binary_io` header names against an existing round-trip test (`grep -rn "VectorSink" src/tests | head -3`) and adjust includes if they differ.

Add a `GamePlayerS` stream round trip to the same file using `MakeUnit` from `test_unit_factory.h`: set `SetTradeSkills({{1, 5, 75}})`, write with `operator<<`, read into a fresh `GamePlayerS`, check `GetTradeSkills().Get(1)->value == 5`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S . -B build; cmake --build build --config Debug -t game_server_tests`
Expected: compile error (`trade_skill_book.h` missing).

- [ ] **Step 3: Implement `TradeSkillBook`**

`TradeSkillData` + its stream operators (`io::write<uint32>`, `io::write<uint16>` ×2) go into `src/shared/game/trade_skill.h` (include `binary_io/reader.h`/`writer.h`), because the client needs them too. `trade_skill_book.h` includes that header and declares the class above, backed by `std::map<uint32, TradeSkillData> m_skills`. Rules:
- `LearnOrRaise`: absent → insert `{id, 1, max}` → `Learned`; present and `max > current.max` → raise → `CapRaised`; else `Unchanged`.
- `TrySkillUp`: absent or `value >= max` → false; `roll < GetTradeSkillUpChance(value, t)` → `++value`, true.

- [ ] **Step 4: Wire into `CharacterData` and `GamePlayerS`**

`character_data.h`: `#include "game_server/trade_skill_book.h"`; member `std::vector<TradeSkillData> tradeSkills;` after `sleepPoseEmote`. Reader: after the emote/pose block, `io::read_container<uint8>(data.tradeSkills)` is not usable for structs — write the loop explicitly (uint8 count, then `reader >> entry`), and tolerate end-of-stream for the count (an older stream without the list leaves `tradeSkills` empty: read the count only `if (reader)` succeeded so far, and do not fail the whole read when the count is missing — mirror how the file treats the trailing emote block). Writer: `io::write<uint8>(tradeSkills.size())` then each entry.

`game_player_s.h/.cpp`: member `TradeSkillBook m_tradeSkills;`, accessors, signals as listed. `operator<<(GamePlayerS)`: after the emote block write `uint8 count` + entries of `m_tradeSkills.GetAll()`. `operator>>`: read them back with `SetAll`.

- [ ] **Step 5: Run tests**

Run: `cmake --build build --config Debug -t game_server_tests; bin/Debug/game_server_tests "[trade_skill]"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/shared/game/trade_skill.h src/shared/game_server src/tests/game_server_tests/trade_skill_book_test.cpp
git commit -m "Add per-character trade skill book and carry it in character payloads"
```

---

### Task 4: `TradeSkill` effect — learning a profession

**Files:**
- Modify: `src/shared/game_server/spells/spell_effects.{h,cpp}`, `src/shared/game_server/spells/single_cast_state.cpp:1037` (handler table)
- Test: `src/tests/game_server_tests/spell_effects_test.cpp`

**Interfaces:**
- Consumes: `TradeSkillBook::LearnOrRaise`, `GamePlayerS::tradeSkillLearned`, `GamePlayerS::AddSpell(uint32, bool)`.
- Produces: `void SpellEffects::HandleTradeSkill(SpellEffectContext& ctx)`.

- [ ] **Step 1: Write the failing tests** (append to `spell_effects_test.cpp`, reuse its `TestCtx`)

```cpp
TEST_CASE("HandleTradeSkill learns the skill and its starter recipes", "[spell_effects][trade_skill]")
{
	TestCtx ctx;
	auto* skill = ctx.project.skills.add(1);
	skill->set_name("Tailoring");
	skill->set_category(1);
	skill->set_cost(0);
	skill->add_spells(600);
	auto* recipe = ctx.project.spells.add(600);
	recipe->set_name("Bolt of Linen Cloth");
	recipe->add_attributes(spell_attributes::TradeSpell);
	recipe->add_attributes(0);
	ctx.Init();

	ctx.effect.set_type(spell_effects::TradeSkill);
	ctx.effect.set_miscvaluea(1);
	auto effCtx = ctx.MakeEffectCtx(75);
	SpellEffects::HandleTradeSkill(effCtx);

	const TradeSkillData* data = ctx.target->GetTradeSkills().Get(1);
	REQUIRE(data != nullptr);
	CHECK(data->value == 1);
	CHECK(data->max == 75);
	CHECK(ctx.target->HasSpell(600));
}

TEST_CASE("HandleTradeSkill on a known skill only raises the cap", "[spell_effects][trade_skill]")
{
	TestCtx ctx;
	auto* skill = ctx.project.skills.add(1);
	skill->set_name("Tailoring");
	skill->set_category(1);
	skill->set_cost(0);
	ctx.Init();
	ctx.target->SetTradeSkills({ { 1, 40, 75 } });

	ctx.effect.set_type(spell_effects::TradeSkill);
	ctx.effect.set_miscvaluea(1);
	auto relogin = ctx.MakeEffectCtx(75);
	SpellEffects::HandleTradeSkill(relogin);
	CHECK(ctx.target->GetTradeSkills().Get(1)->value == 40);

	auto journeyman = ctx.MakeEffectCtx(150);
	SpellEffects::HandleTradeSkill(journeyman);
	CHECK(ctx.target->GetTradeSkills().Get(1)->max == 150);
	CHECK(ctx.target->GetTradeSkills().Get(1)->value == 40);
}

TEST_CASE("HandleTradeSkill ignores unknown skills and non-positive caps", "[spell_effects][trade_skill]")
{
	TestCtx ctx;
	ctx.Init();
	ctx.effect.set_type(spell_effects::TradeSkill);
	ctx.effect.set_miscvaluea(42);
	auto effCtx = ctx.MakeEffectCtx(75);
	SpellEffects::HandleTradeSkill(effCtx);
	CHECK_FALSE(ctx.target->GetTradeSkills().Has(42));
}
```

If `ctx.project.skills.add` does not exist with that signature, use the same `TemplateManager::add(id)` call the file uses for `classes`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Debug -t game_server_tests`
Expected: compile error (`HandleTradeSkill` not declared).

- [ ] **Step 3: Implement**

`spell_effects.h`: declare `void HandleTradeSkill(SpellEffectContext& ctx);` next to `HandleTeachEmote`, with a Doxygen line ("Learns the profession miscvaluea for player targets or raises its cap to basePoints; first-time learning also teaches SkillEntry.spells.").

`spell_effects.cpp`:

```cpp
		void HandleTradeSkill(SpellEffectContext& ctx)
		{
			const proto::Project& project = ctx.castContext.GetExecutor().GetProject();
			const uint32 skillId = static_cast<uint32>(ctx.effect.miscvaluea());
			const proto::SkillEntry* skill = project.skills.getById(skillId);
			if (!skill)
			{
				ELOG("TradeSkill effect of spell " << ctx.castContext.GetSpell().id() << " references unknown skill " << skillId);
				return;
			}

			if (ctx.basePoints <= 0)
			{
				WLOG("TradeSkill effect of spell " << ctx.castContext.GetSpell().id() << " has no skill cap");
				return;
			}

			const uint16 cap = static_cast<uint16>(std::min<int32>(ctx.basePoints, std::numeric_limits<uint16>::max()));
			for (auto* targetObject : ctx.effectTargets)
			{
				if (!targetObject->IsPlayer())
				{
					continue;
				}

				ctx.markAffectedTarget(*targetObject);
				auto& player = targetObject->AsPlayer();
				const auto result = player.GetTradeSkills().LearnOrRaise(skillId, cap);
				if (result == TradeSkillBook::LearnResult::Unchanged)
				{
					continue;
				}

				if (result == TradeSkillBook::LearnResult::Learned)
				{
					for (const uint32 spellId : skill->spells())
					{
						player.AddSpell(spellId, true);
					}
				}

				player.tradeSkillLearned(skillId);
			}
		}
```

Use the executor accessor already used by `HandleCreateItem` (`ctx.castContext.GetExecutor().GetProject()`). Register `{se::TradeSkill, SpellEffects::HandleTradeSkill},` in the `kHandlers` table in `single_cast_state.cpp`.

- [ ] **Step 4: Run tests**

Run: `cmake --build build --config Debug -t game_server_tests; bin/Debug/game_server_tests "[trade_skill]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/shared/game_server/spells src/tests/game_server_tests/spell_effects_test.cpp
git commit -m "Handle the TradeSkill spell effect: learn professions and raise their cap"
```

---

### Task 5: Craft validation, bag space and skill-up

**Files:**
- Modify: `src/shared/game/spell.h:255-283` (append results), `src/shared/game_server/spells/single_cast_state.{h,cpp}`
- Create: `src/shared/game_server/spells/trade_skill_cast_rules.{h,cpp}`
- Test: `src/tests/game_server_tests/trade_skill_cast_rules_test.cpp`

**Interfaces:**
- Produces: `spell_cast_result::FailedSkillTooLow`, `spell_cast_result::FailedInventoryFull` (appended after `FailedLineOfSight`); free functions

```cpp
/// FailedSkillTooLow if the recipe needs a profession the player lacks or has too low; CastOkay otherwise
/// (also for spells with skill() == 0).
SpellCastResult CheckTradeSkillRequirement(const proto::SpellEntry& spell, const TradeSkillBook& skills);
/// FailedInventoryFull if any CreateItem effect targeting the caster cannot be stored; CastOkay otherwise.
SpellCastResult CheckCreateItemSpace(const proto::SpellEntry& spell, const proto::Project& project, const Inventory& inventory);
/// Rolls the skill-up for a successful cast; returns true if the value rose. No-op for skill() == 0.
bool ApplyTradeSkillUp(const proto::SpellEntry& spell, TradeSkillBook& skills, float roll);
```

- [ ] **Step 1: Write the failing tests**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/trade_skill_cast_rules.h"
#include "game_server/objects/game_player_s.h"
#include "test_unit_factory.h"
#include "base/timer_queue.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

using namespace mmo;

namespace
{
	proto::SpellEntry MakeRecipe(const uint32 skill, const uint32 required)
	{
		proto::SpellEntry spell;
		spell.set_id(600);
		spell.add_attributes(spell_attributes::TradeSpell);
		spell.add_attributes(0);
		spell.set_skill(skill);
		spell.set_skillrequired(required);
		spell.set_trivialskilllow(required + 10);
		spell.set_trivialskillhigh(required + 30);
		return spell;
	}
}

TEST_CASE("Recipes require the profession at the required value", "[trade_skill]")
{
	TradeSkillBook book;
	CHECK(CheckTradeSkillRequirement(MakeRecipe(1, 1), book) == spell_cast_result::FailedSkillTooLow);
	book.SetAll({ { 1, 9, 75 } });
	CHECK(CheckTradeSkillRequirement(MakeRecipe(1, 10), book) == spell_cast_result::FailedSkillTooLow);
	book.SetAll({ { 1, 10, 75 } });
	CHECK(CheckTradeSkillRequirement(MakeRecipe(1, 10), book) == spell_cast_result::CastOkay);
}

TEST_CASE("Spells without a skill are unaffected by trade skill rules", "[trade_skill]")
{
	TradeSkillBook book;
	proto::SpellEntry smelt;
	smelt.add_attributes(0);
	smelt.add_attributes(0);
	CHECK(CheckTradeSkillRequirement(smelt, book) == spell_cast_result::CastOkay);
	CHECK_FALSE(ApplyTradeSkillUp(smelt, book, 0.0f));
}

TEST_CASE("A successful craft raises the skill by one", "[trade_skill]")
{
	TradeSkillBook book;
	book.SetAll({ { 1, 1, 75 } });
	CHECK(ApplyTradeSkillUp(MakeRecipe(1, 1), book, 0.99f));
	CHECK(book.Get(1)->value == 2);
}

TEST_CASE("CreateItem spells fail when the bags cannot take the result", "[trade_skill]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	auto* item = project.items.add(700);
	item->set_name("Linen Bag");
	item->set_maxstack(1);
	auto player = MakeUnit(project, timers);

	proto::SpellEntry spell = MakeRecipe(1, 1);
	auto* effect = spell.add_effects();
	effect->set_type(spell_effects::CreateItem);
	effect->set_itemtype(700);
	effect->set_basepoints(1);
	CHECK(CheckCreateItemSpace(spell, project, player->GetInventory()) == spell_cast_result::CastOkay);

	// Fill every backpack slot with single, unstackable items.
	REQUIRE(player->GetInventory().CreateItems(*item, player_inventory_pack_slots::End - player_inventory_pack_slots::Start) == inventory_change_failure::Okay);
	CHECK(CheckCreateItemSpace(spell, project, player->GetInventory()) == spell_cast_result::FailedInventoryFull);
}
```

Look up the real backpack slot constant and the item fields needed by `CreateItems` in `bag_manager_test.cpp`/`inventory.h` before running, and adjust the fill line to them (the intent: backpack full, no bags).

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S . -B build; cmake --build build --config Debug -t game_server_tests`
Expected: compile error (header missing).

- [ ] **Step 3: Implement**

`spell.h`: append before `CastOkay`:

```cpp
			/// The caster's profession skill is missing or below the recipe's requirement.
			FailedSkillTooLow,
			/// The caster's bags cannot hold the item the spell creates.
			FailedInventoryFull,
```

`trade_skill_cast_rules.cpp`:

```cpp
	SpellCastResult CheckTradeSkillRequirement(const proto::SpellEntry& spell, const TradeSkillBook& skills)
	{
		if (spell.skill() == 0)
		{
			return spell_cast_result::CastOkay;
		}

		const TradeSkillData* data = skills.Get(spell.skill());
		if (!data || data->value < spell.skillrequired())
		{
			return spell_cast_result::FailedSkillTooLow;
		}

		return spell_cast_result::CastOkay;
	}

	SpellCastResult CheckCreateItemSpace(const proto::SpellEntry& spell, const proto::Project& project, const Inventory& inventory)
	{
		for (const auto& effect : spell.effects())
		{
			if (effect.type() != spell_effects::CreateItem || effect.basepoints() <= 0)
			{
				continue;
			}

			const proto::ItemEntry* item = project.items.getById(effect.itemtype());
			if (!item)
			{
				continue;
			}

			if (inventory.CanStoreItems(*item, static_cast<uint16>(effect.basepoints())) != inventory_change_failure::Okay)
			{
				return spell_cast_result::FailedInventoryFull;
			}
		}

		return spell_cast_result::CastOkay;
	}

	bool ApplyTradeSkillUp(const proto::SpellEntry& spell, TradeSkillBook& skills, const float roll)
	{
		if (spell.skill() == 0)
		{
			return false;
		}

		return skills.TrySkillUp(spell.skill(), TradeSkillThresholdsOf(spell), roll);
	}
```

The effect's created amount is `basepoints` after `CalculateBasePoints`; for recipes `basedice` is 0, so `effect.basepoints()` is the count. Document that assumption in the header.

`single_cast_state.cpp`:
1. In `ValidatePlayerRequirements`, after the reagent loop:

```cpp
		if (const SpellCastResult skillResult = CheckTradeSkillRequirement(m_spell, playerCaster->GetTradeSkills()); skillResult != spell_cast_result::CastOkay)
		{
			SendEndCast(skillResult);
			return false;
		}

		if (const SpellCastResult spaceResult = CheckCreateItemSpace(m_spell, playerCaster->GetProject(), playerCaster->GetInventory()); spaceResult != spell_cast_result::CastOkay)
		{
			SendEndCast(spaceResult);
			return false;
		}
```

(Use whatever accessor gives the player's `proto::Project`; `GameObjectS` has `GetProject()` if `HandleCreateItem` reaches it through the executor — check and use the same.)

2. In the finish path, between `ValidateTargetRequirements(...)` and `ConsumePower()`, re-check bag space for player casters (bags can fill during the cast) and end with `FailedInventoryFull` before anything is consumed.
3. After `SendEndCast(spell_cast_result::CastOkay);` in the finish path:

```cpp
		if (GamePlayerS* playerCaster = m_context.GetPlayerExecutor(); playerCaster && m_spell.skill() != 0)
		{
			std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
			if (ApplyTradeSkillUp(m_spell, playerCaster->GetTradeSkills(), distribution(randomGenerator)))
			{
				playerCaster->tradeSkillChanged(m_spell.skill());
			}
		}
```

Instant recipes (cast time 0) take the same finish path; verify by reading `SingleCastState::Activate` that instant casts reach this function, otherwise add the same block where instant casts send `CastOkay`.

- [ ] **Step 4: Run tests**

Run: `cmake --build build --config Debug -t game_server_tests; bin/Debug/game_server_tests "[trade_skill]"; bin/Debug/game_server_tests`
Expected: all PASS (no regressions in other spell tests).

- [ ] **Step 5: Commit**

```bash
git add src/shared/game/spell.h src/shared/game_server/spells src/tests/game_server_tests/trade_skill_cast_rules_test.cpp
git commit -m "Validate profession skill and bag space for crafts, roll skill-ups"
```

---

### Task 6: Protocol, world server packets, trainer gate

**Files:**
- Modify: `src/shared/game_protocol/game_protocol.h` (opcodes before `Count_` ~line 856, version line 100), `src/shared/game/vendor.h` (append `FailedSkillTooLow` to `trainer_result`), `src/world_server/player.{h,cpp}`, `src/world_server/player_npc_handlers.cpp:513-598`, `docs/protocol_versions.md`, `tools/protocol_version_check.py` baseline (via `--update`)

**Interfaces:**
- Produces: `realm_client_packet::TradeSkills` (payload `uint8 count, {uint32 skill, uint16 value, uint16 max}[]`), `realm_client_packet::TradeSkillUpdate` (payload `uint32 skill, uint16 value, uint16 max`), `Player::SendTradeSkills()`, `trainer_result::FailedSkillTooLow`; `ProtocolVersion = 0x00000015`.

- [ ] **Step 1: Opcodes and version**

Append before `Count_` in `realm_client_packet`:

```cpp
				/// The character's full profession skill list. Sent on world entry and whenever a
				/// profession is learned or its cap changes. Payload: uint8 count, then per skill
				/// uint32 skillId, uint16 value, uint16 max.
				TradeSkills,

				/// One profession skill value changed (skill-up). Payload: uint32 skillId,
				/// uint16 value, uint16 max.
				TradeSkillUpdate,
```

Bump `ProtocolVersion` to `0x00000015`. Add the row to `docs/protocol_versions.md` (version 21, 2026-10-12: "TradeSkills/TradeSkillUpdate packets, spell cast results FailedSkillTooLow/FailedInventoryFull, trainer result FailedSkillTooLow, CharacterData/GamePlayerS trade skill list"). Run `python tools/protocol_version_check.py --update`, then `python tools/protocol_version_check.py` → exit 0.

- [ ] **Step 2: World server sends**

`player.h`: declare `void SendTradeSkills();` and `void SendTradeSkillUpdate(uint32 skillId);`, plus `scoped_connection` members for the two `GamePlayerS` signals (follow how `spellLearned` is connected in `player.cpp`).

`player.cpp`:
- Where `m_character->SetKnownSpells(m_characterData.spellIds)` is called (~line 150), call `m_character->SetTradeSkills(m_characterData.tradeSkills);` **before** it, so the passive unlock spells re-cast at spawn find the stored values.
- Connect `tradeSkillLearned` → `if (m_spawned) { SendTradeSkills(); SaveCharacterData(); }` and `tradeSkillChanged` → `if (m_spawned) { SendTradeSkillUpdate(id); }`.
- In the spawn sequence, after the passive-spell cast block (~line 1549–1566), call `SendTradeSkills();` (after passives so the list is final).

```cpp
	void Player::SendTradeSkills()
	{
		const std::vector<TradeSkillData> skills = m_character->GetTradeSkills().GetAll();
		SendPacket([skills](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::TradeSkills);
			packet << io::write<uint8>(skills.size());
			for (const auto& skill : skills)
			{
				packet << skill;
			}
			packet.Finish();
		}, false);
	}

	void Player::SendTradeSkillUpdate(const uint32 skillId)
	{
		const TradeSkillData* skill = m_character->GetTradeSkills().Get(skillId);
		if (!skill)
		{
			return;
		}

		const TradeSkillData copy = *skill;
		SendPacket([copy](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::TradeSkillUpdate);
			packet << copy;
			packet.Finish();
		}, false);
	}
```

Match the `SendPacket` signature used by `OnClassXpGained` (lambdas capture by value).

- [ ] **Step 3: Trainer gate**

In `OnTrainerBuySpell`, after the level check and before `ConsumeMoney`:

```cpp
				if (trainerSpellEntry.reqskill() != 0)
				{
					const TradeSkillData* skill = m_character->GetTradeSkills().Get(trainerSpellEntry.reqskill());
					if (!skill || skill->value < trainerSpellEntry.reqskillval())
					{
						SendTrainerBuyError(trainerGuid, trainer_result::FailedSkillTooLow);
						return;
					}
				}
```

Profession trainers (`SKILL_TRAINER`) use the character level for `reqlevel` already (only CLASS_TRAINER uses class level) and `IsTrainerClassAllowed` must return true for non-class trainers — verify by reading it.

- [ ] **Step 4: Build**

Run: `cmake --build build --config Debug -t world_server realm_server game_server_tests game_protocol_tests; bin/Debug/game_protocol_tests`
Expected: builds, tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/game_protocol src/shared/game/vendor.h src/world_server docs/protocol_versions.md tools/protocol_version_check.py tools/*.json
git commit -m "Send profession skills to the client and enforce trainer skill requirements"
```

(`git status` first: add whatever baseline file `--update` changed.)

---

### Task 7: Realm persistence

**Files:**
- Create: `data/realm/updates/20261012_1_character_skills.sql`
- Modify: `src/realm_server/database.h:191-195`, `src/realm_server/mysql_database.{h,cpp}`, `src/realm_server/world.cpp:773-905`, `src/realm_server/player.cpp:2843-2870`, `src/tests/realm_server_tests/mock_databases.h` (if it implements `IDatabase`)

**Interfaces:**
- Consumes: `CharacterData::tradeSkills`, `GamePlayerS::GetTradeSkills().GetAll()`.
- Produces: `virtual void IDatabase::UpdateCharacterTradeSkills(uint64 characterId, const std::vector<TradeSkillData>& skills) = 0;`

- [ ] **Step 1: Migration**

```sql
-- Professions: per-character trade skill values and caps.
CREATE TABLE IF NOT EXISTS `character_skills` (
  `character` bigint unsigned NOT NULL,
  `skill` int unsigned NOT NULL,
  `value` smallint unsigned NOT NULL DEFAULT 1,
  `max` smallint unsigned NOT NULL DEFAULT 1,
  PRIMARY KEY (`character`,`skill`),
  CONSTRAINT `fk_skill_character_id` FOREIGN KEY (`character`) REFERENCES `characters` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_german1_ci;
```

`realm_db_schema_full.sql` only creates the base tables and an empty `history`; migrations are applied on top, so it needs no change.

- [ ] **Step 2: Load**

In `MySQLDatabase` character load, after the emote block (~line 641):

```cpp
				if (mysql::Select skillSelect(m_connection, "SELECT `skill`, `value`, `max` FROM `character_skills` WHERE `character` = " + std::to_string(characterId)); skillSelect.Success())
				{
					mysql::Row skillRow(skillSelect);
					while (skillRow)
					{
						TradeSkillData skill;
						skillRow.GetField(0, skill.skillId);
						skillRow.GetField(1, skill.value);
						skillRow.GetField(2, skill.max);
						result.tradeSkills.push_back(skill);
						skillRow = mysql::Row::Next(skillSelect);
					}
				}
				else
				{
					PrintDatabaseError();
					throw mysql::Exception("Could not load character trade skills");
				}
```

(Check `Row::GetField` has a `uint16` overload; otherwise read into `uint32` and narrow.)

- [ ] **Step 3: Save**

`UpdateCharacterTradeSkills` implemented like `UpdateCharacterAuras`: lock, transaction, `DELETE FROM character_skills WHERE character=…`, one multi-row `INSERT` when non-empty, commit. Declare in `database.h` (Doxygen) and `mysql_database.h` (`/// @copydoc`), add a no-op override to any mock implementing `IDatabase`.

`world.cpp` `OnCharacterData`: after the aura request, `m_database.asyncRequestKeyed(characterGuid, handler-like lambda logging on failure, &IDatabase::UpdateCharacterTradeSkills, characterGuid, player.GetTradeSkills().GetAll());` — the keyed queue preserves order per character.

`realm_server/player.cpp` `NotifyCharacterUpdate`: `m_characterData->tradeSkills = character.GetTradeSkills().GetAll();` next to the known-classes sync, so a later world transfer carries current values.

- [ ] **Step 4: Build and test**

Run: `cmake --build build --config Debug -t realm_server realm_server_tests; bin/Debug/realm_server_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add data/realm/updates/20261012_1_character_skills.sql src/realm_server src/tests/realm_server_tests
git commit -m "Persist profession skills in the realm database"
```

---

### Task 8: Bot connector and E2E query

**Files:**
- Modify: `src/shared/bot_core/bot_unit.h` (+ `.cpp`), `src/shared/bot_core/bot_realm_connector.{h,cpp}:452`, `src/e2e_client/scenario_engine.cpp:679,1615`, `e2e/README.md` (API table)

**Interfaces:**
- Produces: `BotUnit::SetTradeSkill(uint32 skillId, uint16 value, uint16 max)`, `BotUnit::GetTradeSkill(uint32) const -> const TradeSkillData*`, `BotUnit::ClearTradeSkills()`; Lua `GetTradeSkill(skillId) -> value` (`-1` when unknown) and `GetTradeSkillMax(skillId) -> max` (`-1`); `LastCastResult()` keeps reporting `failed:<number>` (21 = FailedSkillTooLow, 22 = FailedInventoryFull).

- [ ] **Step 1: Implement**

Register handlers `OnTradeSkills` (clear, then set each) and `OnTradeSkillUpdate` next to `OnInitialSpells`, parsing exactly the payloads from Task 6 and calling `UnitUpdated(*self)`. Lua lambdas mirror `luaHasSpell`, reading `self()` (the own `BotUnit`). Document both queries and the two numeric cast results in `e2e/README.md`.

- [ ] **Step 2: Build**

Run: `cmake --build build --config Debug -t e2e_client bot_ai_tests; bin/Debug/bot_ai_tests`
Expected: builds and passes.

- [ ] **Step 3: Commit**

```bash
git add src/shared/bot_core src/e2e_client e2e/README.md
git commit -m "Track profession skills in the bot client and expose them to E2E scenarios"
```

---

### Task 9: Client C++ — spellbook filter, skill cache, Lua API

**Files:**
- Create: `src/shared/game_client/trade_skill_list.{h,cpp}`
- Modify: `src/shared/game_client/game_unit_c.cpp:2213-2240`, `src/shared/game_client/game_player_c.h`, `src/mmo_client/game_states/world_state.{h,cpp}` (handler registration ~1878, `OnSpellLearnedOrUnlearned` ~3733, `OnSpellFailure` string table ~4102), `src/mmo_client/game_script.cpp`, `src/mmo_client/systems/trainer_client.cpp:~98,233`, `src/tests/game_client_tests/CMakeLists.txt`
- Test: `src/tests/game_client_tests/test_trade_skill_list.cpp`

**Interfaces:**
- Consumes: `proto_client::Project::skills`, trade skill packets, `GetTradeSkillDifficulty`.
- Produces:

```cpp
struct ProfessionInfo { uint32 skillId; const proto_client::SpellEntry* unlockSpell; uint16 value; uint16 max; };
struct RecipeInfo { const proto_client::SpellEntry* spell; uint32 category; TradeSkillDifficulty difficulty; uint32 resultItemId; int32 resultCount; };
/// Professions are the skills referenced by known spells with a TradeSkill effect; per skill the
/// spell with the highest base points (highest rank) provides name and icon. Skills without packet
/// state show value 0 / max from the spell. Sorted by skill name.
std::vector<ProfessionInfo> BuildProfessionList(const std::vector<const proto_client::SpellEntry*>& knownSpells, const proto_client::SkillManager& skills, const std::map<uint32, TradeSkillData>& state);
/// Known TradeSpell spells whose skill() matches, in category sortorder then name order.
std::vector<RecipeInfo> BuildRecipeList(uint32 skillId, const std::vector<const proto_client::SpellEntry*>& knownSpells, const proto_client::SkillEntry& skill, uint32 skillValue);
```

`GamePlayerC::SetTradeSkills(std::map<uint32, TradeSkillData>)`, `SetTradeSkill(TradeSkillData)`, `GetTradeSkills() const`. `GameUnitC::GetSpells()` (all known spells; add a const accessor if missing). Lua events `TRADE_SKILLS_CHANGED`, `TRADE_SKILL_UPDATE` (arg: skillId). Lua globals:
- `GetProfessions()` → array of `{ skillId, name, icon, value, max }`
- `GetProfessionCategories(skillId)` → array of `{ id, name }` sorted by `sortorder`
- `GetRecipes(skillId)` → array of `{ spellId, name, icon, category, difficulty ("orange"|"yellow"|"green"|"grey"|"impossible"), resultItemId, resultCount, reagents = { { itemId, count }, … } }`
- `HasProfessions()` → bool
- `GetTradeSkillValue(skillId)` → value, max (0, 0 when unknown)

`TradeSkillData` comes from `src/shared/game/trade_skill.h` (Task 3); never include `game_server` headers from the client.

- [ ] **Step 1: Write the failing test** (only built with `MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR`; add `trade_skill_list.cpp` and the test to the guarded `target_sources` block, and the test file to the `HEADER_FILE_ONLY` list in the else branch)

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_client/trade_skill_list.h"
#include "game/spell.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	proto_client::SpellEntry MakeUnlock(const uint32 id, const uint32 skillId, const int32 cap, const std::string& name)
	{
		proto_client::SpellEntry spell;
		spell.set_id(id);
		spell.set_name(name);
		spell.add_attributes(spell_attributes::Passive);
		spell.add_attributes(0);
		auto* effect = spell.add_effects();
		effect->set_type(spell_effects::TradeSkill);
		effect->set_miscvaluea(skillId);
		effect->set_basepoints(cap);
		return spell;
	}

	proto_client::SpellEntry MakeRecipe(const uint32 id, const uint32 skillId, const uint32 category, const uint32 item)
	{
		proto_client::SpellEntry spell;
		spell.set_id(id);
		spell.set_name("Recipe " + std::to_string(id));
		spell.add_attributes(spell_attributes::TradeSpell);
		spell.add_attributes(0);
		spell.set_skill(skillId);
		spell.set_skillrequired(1);
		spell.set_trivialskilllow(20);
		spell.set_trivialskillhigh(40);
		spell.set_craftcategory(category);
		auto* effect = spell.add_effects();
		effect->set_type(spell_effects::CreateItem);
		effect->set_itemtype(item);
		effect->set_basepoints(1);
		return spell;
	}
}

TEST_CASE("Professions come from known TradeSkill spells, highest rank wins", "[trade_skill]")
{
	proto_client::SkillManager skills;
	auto* tailoring = skills.add(1);
	tailoring->set_name("Tailoring");
	tailoring->set_category(1);
	tailoring->set_cost(0);

	const auto apprentice = MakeUnlock(10, 1, 75, "Tailoring (Apprentice)");
	const auto journeyman = MakeUnlock(11, 1, 150, "Tailoring (Journeyman)");
	const auto recipe = MakeRecipe(20, 1, 2, 700);
	const std::vector<const proto_client::SpellEntry*> known{ &apprentice, &recipe, &journeyman };

	std::map<uint32, TradeSkillData> state{ { 1, { 1, 33, 150 } } };
	const auto professions = BuildProfessionList(known, skills, state);
	REQUIRE(professions.size() == 1);
	CHECK(professions[0].unlockSpell->id() == 11);
	CHECK(professions[0].value == 33);
	CHECK(professions[0].max == 150);
}

TEST_CASE("Recipes are filtered by skill, sorted by category and coloured", "[trade_skill]")
{
	proto_client::SkillEntry skill;
	skill.set_id(1);
	skill.set_name("Tailoring");
	skill.set_category(1);
	skill.set_cost(0);
	auto* bags = skill.add_categories();
	bags->set_id(2);
	bags->set_name("Bags");
	bags->set_sortorder(20);
	auto* materials = skill.add_categories();
	materials->set_id(1);
	materials->set_name("Materials");
	materials->set_sortorder(10);

	const auto bag = MakeRecipe(21, 1, 2, 701);
	const auto bolt = MakeRecipe(20, 1, 1, 700);
	const auto other = MakeRecipe(30, 2, 1, 800);
	const std::vector<const proto_client::SpellEntry*> known{ &bag, &other, &bolt };

	const auto recipes = BuildRecipeList(1, known, skill, 25);
	REQUIRE(recipes.size() == 2);
	CHECK(recipes[0].spell->id() == 20);
	CHECK(recipes[1].spell->id() == 21);
	CHECK(recipes[0].difficulty == trade_skill_difficulty::Yellow);
	CHECK(recipes[0].resultItemId == 700);
	CHECK(recipes[0].resultCount == 1);
}

TEST_CASE("Unknown professions without packet state are still listed", "[trade_skill]")
{
	proto_client::SkillManager skills;
	auto* tailoring = skills.add(1);
	tailoring->set_name("Tailoring");
	tailoring->set_category(1);
	tailoring->set_cost(0);
	const auto apprentice = MakeUnlock(10, 1, 75, "Tailoring (Apprentice)");
	const std::vector<const proto_client::SpellEntry*> known{ &apprentice };
	const auto professions = BuildProfessionList(known, skills, {});
	REQUIRE(professions.size() == 1);
	CHECK(professions[0].value == 0);
	CHECK(professions[0].max == 75);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S . -B build; cmake --build build --config Debug -t game_client_tests`
Expected: compile error (header missing).

- [ ] **Step 3: Implement `trade_skill_list.cpp`**

Straightforward loops per the interface comments: collect `TradeSkill` effects (`miscvaluea` → skill id, keep the spell with the largest `basepoints`), skip skills missing from the manager, fill value/max from `state` (fallback `0`/spell cap), sort by skill name. Recipes: `attributes(0) & TradeSpell` and `skill() == skillId`; result item = first `CreateItem` effect; order by the category's `sortorder` (unknown categories last), then spell name; difficulty via `GetTradeSkillDifficulty(skillValue, TradeSkillThresholdsOf(*spell))`.

- [ ] **Step 4: Spellbook filter and packets**

- `GameUnitC::SetInitialSpells`/`LearnSpell`: a spell is visible only if neither `HiddenClientSide` nor `TradeSpell` is set (one helper `IsSpellBookVisible(const proto_client::SpellEntry&)` in the .cpp's anonymous namespace).
- `OnSpellLearnedOrUnlearned`: skip action-bar auto-placement when `TradeSpell` is set (defensive; recipes are not abilities).
- `WorldState::OnTradeSkills` / `OnTradeSkillUpdate`: parse, update `GamePlayerC`, fire `TRADE_SKILLS_CHANGED` / `TRADE_SKILL_UPDATE` (with skillId). Register next to `KnownClasses`. Also fire `TRADE_SKILLS_CHANGED` from `OnSpellLearnedOrUnlearned` when the (un)learned spell has a `TradeSkill` effect or the `TradeSpell` attribute, so the window refreshes on new recipes.
- `OnSpellFailure` string table: append `"SPELL_CAST_FAILED_SKILL_TOO_LOW"`, `"SPELL_CAST_FAILED_INVENTORY_FULL"` (keys added to locales in Task 10).
- `trainer_client.cpp`: replace the `// TODO: Skill requirements checks` with a check of `m_trainerSpells[index].skill/skillValue` against `GamePlayerC::GetTradeSkills()`, firing `TRAINER_BUY_ERROR` with `3`; map `trainer_result::FailedSkillTooLow` to `3` in the result switch.

- [ ] **Step 5: Lua API in `game_script.cpp`**

Add the five globals as `luabind::def<std::function<...>>` lambdas next to `GetScreenResolutions`, building tables with `luabind::newtable(m_luaState.get())` exactly like the existing examples. Names: `Script_GetSpellName`-style localization for spell names; skill/category names through `GetLocalizedString(entry.name(), entry.name_loc(), GetClientLocale())`. Reagents from `spell->reagents()` (`item`, `count`). Return an empty table when there is no active player.

- [ ] **Step 6: Run tests and build the client**

Run: `cmake --build build --config Debug -t game_client_tests mmo_client; bin/Debug/game_client_tests "[trade_skill]"`
Expected: PASS; client builds.

- [ ] **Step 7: Commit**

```bash
git add src/shared/game_client src/mmo_client src/tests/game_client_tests
git commit -m "Client: hide recipes from the spellbook, cache profession skills, expose professions to Lua"
```

---

### Task 10: Profession window UI (data/client)

**Files (all in the `data/client` submodule):**
- Modify: `Interface/GameUI/Crafting.xml`, `Interface/GameUI/Crafting.lua`, `Interface/GameUI/GameUI.toc` (if it lists the mock data file)
- Delete: `Interface/GameUI/CraftingMockData.lua`
- Modify: `Interface/Bindings.xml`, `Config/DefaultBindings.cfg`, `Interface/GameUI/TrainerFrame.lua:169`, `Locales/Locale_{enUS,deDE,frFR,ruRU}/Localization.txt`
- Create: `Interface/GameUI/Alestia/MenuIcons/Professions.htex` (convert an existing profession icon from `Interface/Icons/fg4_icons_*` or copy the closest menu icon until a bespoke one exists)
- Superproject: `docs/crafting_ui_mock.md` → rename to `docs/professions.md` and rewrite; `tools/tests/test_crafting_mock.py` → update or remove (it tests the mock)

Use the `mmo-ui-designer` skill for this task.

- [ ] **Step 1: Read the mock end to end** (`Crafting.lua` 320 lines, `Crafting.xml` 1712 lines) and list every reference to `CraftingMock`.

- [ ] **Step 2: Data layer in `Crafting.lua`**

Replace `CraftingMock` with `Crafting = { professions = {}, profession = nil, recipes = {}, categories = {}, collapsed = {}, favorites = {}, queue = 0, selected = nil, quantity = 1 }`. `Crafting_Reload()` fills `Crafting.professions = GetProfessions()`; `Crafting_LoadRecipes(skillId)` fills `recipes = GetRecipes(skillId)` and `categories = GetProfessionCategories(skillId)`. `available(recipe)` becomes `min(floor(GetItemCount(r.itemId) / r.count))` over `recipe.reagents`, capped at 99.

- [ ] **Step 3: Overview cards**

Keep the ten card frames, but set name (`SetText` with the profession name, no `CRAFT_<KEY>` text in XML), icon (`profession.icon`), skill bar (`value / max`, `RANK` format) from data; hide cards beyond `#Crafting.professions`. Remove the hard-coded `CRAFT_MINING` … texts from the XML.

- [ ] **Step 4: Recipe list and details**

Category headers from `Crafting.categories`; rows coloured by `difficulty`: orange `FFFF8040`, yellow `FFFFFF00`, green `FF40C040`, grey `FF808080`, impossible `FFCC3333`. The "skill-up" filter shows `difficulty ~= "grey"`. Details: result icon/tooltip from `GetCachedItemInfo(resultItemId)` with the existing 0.2 s retry; reagent icons, names and tooltips from each reagent's item (support up to the three existing reagent slots; hide unused). Remove `CraftingTarget`, `CraftingReset` and `CraftingWork` (the mock progress bar) from XML and Lua.

- [ ] **Step 5: Crafting**

```lua
function Crafting_Start(all)
	local recipe = Crafting.selected;
	if (not recipe) then
		return;
	end
	local count = all and available(recipe) or Crafting.quantity;
	if (count < 1) then
		return;
	end
	Crafting.queue = count;
	Crafting.queueSpell = recipe.spellId;
	CastSpellById(recipe.spellId);
	Crafting_UpdateButtons();
end

function Crafting_OnCastFinish(self, spellId)
	if (Crafting.queueSpell ~= spellId or Crafting.queue <= 0) then
		return;
	end
	Crafting.queue = Crafting.queue - 1;
	if (Crafting.queue > 0) then
		CastSpellById(spellId);
	end
	Crafting_UpdateButtons();
end

function Crafting_StopQueue()
	Crafting.queue = 0;
	Crafting.queueSpell = nil;
	Crafting_UpdateButtons();
end
```

Register `PLAYER_SPELL_CAST_FINISH` → `Crafting_OnCastFinish`, `PLAYER_SPELL_CAST_FAILED` → `Crafting_StopQueue`; the Cancel button calls `SpellStopCasting()` and `Crafting_StopQueue()`. Check the argument list of `PLAYER_SPELL_CAST_FINISH` in `CastBar.lua` and adapt the handler signature (if it carries no spell id, track the queue by `queueSpell` only). Refresh on `TRADE_SKILLS_CHANGED`, `TRADE_SKILL_UPDATE`, `SPELL_LEARNED` and the bag/item-count event the inventory UI already listens to (find it in `InventoryFrame.lua`).

- [ ] **Step 6: Locking and opening**

- `Crafting_Toggle()`: `if (not HasProfessions()) then return; end` before showing; slash commands go through it.
- Menu bar: in `Crafting_OnLoad`, `AddMenuBarButton("Interface/GameUI/Alestia/MenuIcons/Professions.htex", Crafting_Toggle, "MENUBAR_TOOLTIP_PROFESSIONS", "TOGGLEPROFESSIONS")`, keep the returned/last child (`MenuBarButtons:GetChild(MenuBarButtons:GetChildCount() - 1)`, the BugReporter pattern), and in `Crafting_UpdateLock()` call `SetEnabled(HasProfessions())` and swap its enter handler to show `MENUBAR_TOOLTIP_PROFESSIONS_LOCKED` while disabled. Call it on `PLAYER_ENTER_WORLD` (or the event the other windows use after login), `TRADE_SKILLS_CHANGED` and `SPELL_LEARNED`; hide the window if it is open and professions became empty.
- `Bindings.xml`: binding `TOGGLEPROFESSIONS` (category like `TOGGLESPELLBOOK`, DOWN → `Crafting_Toggle()`); `DefaultBindings.cfg`: `bind K TOGGLEPROFESSIONS`.

- [ ] **Step 7: Trainer message**

`TrainerFrame.lua`: `[3] = "TRAINER_ERROR_SKILL_TOO_LOW"`.

- [ ] **Step 8: Localization (all four locales)**

Add: `MENUBAR_TOOLTIP_PROFESSIONS`, `MENUBAR_TOOLTIP_PROFESSIONS_LOCKED` ("Unlocked once you learn a profession." / "Wird freigeschaltet, sobald du einen Beruf erlernst."), `BINDING_NAME_TOGGLEPROFESSIONS` (match how other binding names are localized), `SPELL_CAST_FAILED_SKILL_TOO_LOW`, `SPELL_CAST_FAILED_INVENTORY_FULL`, `TRAINER_ERROR_SKILL_TOO_LOW`. Remove `CRAFT_*` keys that only the mock data used (profession names, mock recipes, reagent keys, `CRAFT_MOCK`, reset/enchant/target strings); keep the UI chrome keys still referenced. Verify with `grep -o "CRAFT_[A-Z_]*" Interface/GameUI/Crafting.* | sort -u` vs the locale files: every referenced key exists in all four files, no unreferenced `CRAFT_` key remains.

- [ ] **Step 9: Verify in the real client** (after Tasks 11 and the ClientDB export)

Use the `client-visual-verification` recipe (auto-login, `-exec` script). Check: no profession → button greyed, tooltip shows the lock text, `K` and `/craft` do nothing; after `.learn <unlock spell>` (GM) → button enabled, window lists Tailoring 1/75 with three recipes in Materials/Wrists/Bags, crafting a Bolt of Linen Cloth with materials consumes them and raises the bar; spellbook does not list recipes. Take screenshots for the user.

- [ ] **Step 10: Commit (inside `data/client`, then the superproject docs)**

```bash
git -C data/client add -A Interface Config Locales
git -C data/client commit -m "Turn the crafting mock into the real profession window"
git add docs/professions.md docs/crafting_ui_mock.md tools/tests/test_crafting_mock.py
git commit -m "Document the profession system"
```

---

### Task 11: Tailoring content (data/editor + ClientDB)

**Files:** `data/editor/data/{skills,items,spells,item_displays,vendors,trainers,units,quests,unit_loot}.data` (via the mmo-* skills or mmo_edit), `data/client/ClientDB/{skills,spells,project.txt}` (export), `data/editor` world spawn data for the new NPC.

Use the `mmo-item-designer`, `mmo-spell-designer`, `mmo-npc-designer` and `mmo-quest-creator` skills; read each skill's `<world_aware_design>` / loot rules first. Pass `--project-root` pointing at this worktree; let apply scripts assign ids (note the ids they print — later steps reference them by name below). Linen Cloth = item 68.

- [ ] **Step 1: Skill** — author skills.data with the new mmo_edit Skill editor (GUI) or a scratch protobuf script (not committed): id 1 "Tailoring" (deDE "Schneiderei", frFR "Couture", ruRU "Портняжное дело"), category 1, cost 0, categories: 1 Materials/Materialien (10), 2 Bags/Taschen (20), 3 Chest/Brust (30), 4 Legs/Beine (40), 5 Feet/Füße (50), 6 Wrists/Handgelenke (60) with `name_loc` for all four locales. Starter spells filled in Step 3.

- [ ] **Step 2: Items** (item designer; reuse existing displays — Linen Cloth's display 64 for the bolt, the Small Brown Pouch display 9 for the bag, Frayed/Initiate cloth displays for armour):

| Item | Class/sub | Quality | Level | Notes |
|---|---|---|---|---|
| Coarse Thread | 7/15 | 1 | — | stack 20, buy 10, sell 2 |
| Bolt of Linen Cloth | 7/15 | 1 | — | stack 20, sell 12 |
| Linen Bag | 4/0, invtype 18 | 1 | — | containerslots 6, sell 100 |
| Linen Bracers | 2/10 wrists | 1 | ilvl 3, req 1 | armor ≈ 3 |
| Linen Boots | 2/10 feet | 1 | ilvl 6, req 3 | armor ≈ 6 |
| Linen Pants | 2/10 legs | 2 | ilvl 8, req 4 | armor ≈ 12, one stat |
| Linen Robe | 2/10 chest | 2 | ilvl 10, req 5 | armor ≈ 16, one stat |
| Practice Sewing Kit | 9 (quest), white | 1 | — | on-use spell (Step 3), not consumed, quest-bound |
| Patched Linen Pouch | 9 (quest), white | 1 | — | quest item, unique |

Balance against `balance-guidelines.md` and the Initiate's Robe (ilvl 5, armor 11) / Harmony items.

- [ ] **Step 3: Spells** (spell designer; cast time 3000 ms for recipes, 0 for the unlock; icons = the created item's icon path)

| Spell | Attributes | Effect | Reagents | Skill fields (req/yellow/green/grey, category) |
|---|---|---|---|---|
| Tailoring (Apprentice) | Passive | TradeSkill misc 1, bp 75, target self | — | — |
| Bolt of Linen Cloth | TradeSpell | CreateItem bolt ×1 | 2 Linen Cloth | 1/25/37/50, Materials |
| Linen Bracers | TradeSpell | CreateItem ×1 | 1 Bolt, 1 Coarse Thread | 1/30/40/50, Wrists |
| Linen Bag | TradeSpell | CreateItem ×1 | 3 Bolt, 3 Coarse Thread | 1/40/52/65, Bags |
| Linen Boots (trainer) | TradeSpell | CreateItem ×1 | 2 Bolt, 1 Thread | 15/40/52/65, Feet |
| Linen Pants (trainer) | TradeSpell | CreateItem ×1 | 3 Bolt, 2 Thread | 30/55/67/80, Legs |
| Linen Robe (trainer) | TradeSpell | CreateItem ×1 | 4 Bolt, 2 Thread | 45/70/82/95, Chest |
| Practice Stitching | — | CreateItem pouch ×1 | 2 Linen Cloth, 1 Thread | none (skill 0) |

All recipe and unlock spells: `rank 0`, `baseid 0` (spell-rank-zero rule), no class/race mask. Then set skill 1's `spells` = Bolt, Bracers, Bag. Set the Practice Sewing Kit's item spell to Practice Stitching.

- [ ] **Step 4: Vendors** — add Coarse Thread to vendor 1 (Milly Goldwyn) and vendor 5 (General Goods).

- [ ] **Step 5: NPC + trainer** (npc designer, world-aware steps: bible, atlas, north-up render) — "Master Tailor" (name e.g. "Edda Threadwell", subname "Tailoring Trainer"), human, non-hostile, spawn in Oakenshire next to Milly Goldwyn (u20 at ~329, 11, 559); new trainer entry, type `SKILL_TRAINER`, title "Tailoring": Linen Boots (cost 50, reqskill 1, reqskillval 15, reqlevel 3), Linen Pants (100, 1, 30, 4), Linen Robe (150, 1, 45, 5); link via `trainerentry`. Questgiver for the chain.

- [ ] **Step 6: Quests** (quest creator; min level 5; all end with a turn-in)

1. "Good Cloth" — giver/ender Master Tailor; collect 6 Linen Cloth (68); reward small XP + money.
2. "Needle and Thread" — prev 1; giver Master Tailor provides Practice Sewing Kit (start item); objective: deliver 1 Patched Linen Pouch to Master Tailor; text tells the player to buy Coarse Thread from Milly Goldwyn.
3. "The Test Piece" — prev 2; start item Patched Linen Pouch; ender Milly Goldwyn; reward spell "Tailoring (Apprentice)" (`rewardspell`). Text sends the player back to the Master Tailor for further recipes.

Provide deDE/frFR/ruRU `_loc` texts.

- [ ] **Step 7: Loot** — new unit_loot entry "Humanoid Cloth - Level 1-12": one group, Linen Cloth 1–2 at ~20 %. Remove the Linen Cloth group from tables 11 and 16 (keep money/potions). Link the new table to every humanoid unit of level 1–12: 37, 39, 48, 49, 50, 51, 57, 60, 66, 83, 92, 93, 94, 95 — re-check that list with `inspect_npc_catalog` (any other bandit/cultist/poacher/human enemy ≤12; no animals, no undead skeletons). Follow `creature-loot-design` (single-roll groups, keep greens rare).

- [ ] **Step 8: Export ClientDB** — run mmo_edit "Export to Client" (or copy `skills.data` + `spells.data` to `data/client/ClientDB/` and add `skills = (file = "skills.data")` to `ClientDB/project.txt`).

- [ ] **Step 9: Validate** — each skill's `validate_*_json`, the content lint (`tools/world` lint/report), `xp_audit.py` stays ≥ 120 %, and start `realm_server`/`world_server` locally once to confirm the project loads without errors.

- [ ] **Step 10: Commit inside the submodules, then the superproject pointers**

```bash
git -C data/editor add -A data
git -C data/editor commit -m "Add Tailoring: skill, items, recipes, trainer, quest chain, humanoid cloth loot"
git -C data/client add ClientDB
git -C data/client commit -m "Export Tailoring skill and recipe spells to the ClientDB"
git add data/editor data/client
git commit -m "Update data: Tailoring profession content"
```

---

### Task 12: End-to-end scenario

**Files:**
- Create: `e2e/scenarios/tailoring_profession.lua`

**Interfaces:**
- Consumes: Lua API from Task 8; content ids from Task 11 (put them as named locals at the top).

- [ ] **Step 1: Write the scenario**

```lua
-- Learning Tailoring through its quest chain, crafting with it and the skill/bag rules.
local QUEST_GOOD_CLOTH, QUEST_NEEDLE, QUEST_TEST_PIECE = <q1>, <q2>, <q3>
local SPELL_TAILORING = <unlock spell>
local SPELL_BOLT, SPELL_BAG, SPELL_ROBE = <bolt>, <bag>, <robe>
local ITEM_LINEN, ITEM_THREAD, ITEM_BOLT, ITEM_BAG, ITEM_POUCH = 68, <thread>, <bolt>, <bag>, <pouch>
local SKILL_TAILORING = 1

GM.ClearInventory()
GM.LevelUp(5)

GM.AcceptQuest(QUEST_GOOD_CLOTH)
GM.AddItem(ITEM_LINEN, 6)
GM.TurnInQuest(QUEST_GOOD_CLOTH)

GM.AcceptQuest(QUEST_NEEDLE)
GM.AddItem(ITEM_POUCH, 1)
GM.TurnInQuest(QUEST_NEEDLE)

GM.AcceptQuest(QUEST_TEST_PIECE)
GM.TurnInQuest(QUEST_TEST_PIECE)

Assert(WaitUntil(function() return HasSpell(SPELL_TAILORING) end, 5000, "tailoring learned"), "Tailoring was not rewarded")
Assert(WaitUntil(function() return GetTradeSkill(SKILL_TAILORING) == 1 end, 5000, "skill 1"), "skill should start at 1")
Assert(GetTradeSkillMax(SKILL_TAILORING) == 75, "apprentice cap is 75")
Assert(HasSpell(SPELL_BOLT) and HasSpell(SPELL_BAG), "starter recipes learned")

-- Craft a bolt: orange recipe, guaranteed skill-up.
GM.AddItem(ITEM_LINEN, 2)
Assert(CastSpell(SPELL_BOLT), "bolt cast sent")
Assert(WaitUntil(function() return LastCastResult() == "ok" end, 6000, "bolt crafted"), "bolt craft failed: " .. LastCastResult())
Assert(WaitUntil(function() return GetItemCount(ITEM_BOLT) == 1 end, 3000, "bolt in bags"), "bolt not created")
Assert(GetItemCount(ITEM_LINEN) == 0, "linen consumed")
Assert(WaitUntil(function() return GetTradeSkill(SKILL_TAILORING) == 2 end, 3000, "skill up"), "skill did not rise")

-- A trainer recipe above the current skill is refused without consuming anything.
GM.LearnSpell(SPELL_ROBE)
GM.AddItem(ITEM_BOLT, 4)
GM.AddItem(ITEM_THREAD, 2)
CastSpell(SPELL_ROBE)
Assert(WaitUntil(function() return LastCastResult():find("^failed") ~= nil end, 3000, "robe refused"), "robe should be refused")
Assert(LastCastResult() == "failed:21", "expected FailedSkillTooLow, got " .. LastCastResult())
Assert(GetItemCount(ITEM_BOLT) == 5, "reagents kept")

-- The skill survives a relog (passive unlock spell re-applied on login).
Assert(Reconnect(), "reconnect")
Assert(WaitUntil(function() return GetTradeSkill(SKILL_TAILORING) == 2 end, 5000, "skill after relog"), "skill reset on relog")
```

Check `Reconnect` against the README gotchas (it reuses connectors; the trade skill packet must arrive again after re-entering the world). If the realm state between scenarios matters (the shared test character), first read how other scenarios reset learned spells; if there is no unlearn, make the assertions relative (record the starting skill value) and skip the "starts at 1" check when the spell is already known from a previous run.

- [ ] **Step 2: Run it**

```powershell
$env:MMO_E2E_MYSQL_PASSWORD = "loreg320"
cmake --build build -t e2e_client login_server realm_server world_server --config Debug
powershell -File tools/e2e/e2e_run.ps1 -Scenario tailoring_profession
```

Expected: exit 0. On failure read `e2e/runtime/logs/tailoring_profession.jsonl` and `.out.log`.

- [ ] **Step 3: Commit**

```bash
git add e2e/scenarios/tailoring_profession.lua
git commit -m "E2E: learn Tailoring through its quest chain and craft with it"
```

---

### Task 13: Gate and hand-over

- [ ] **Step 1: Full local check** — `cmake --build build --config Debug -t all_tests; cd build; ctest -C Debug --output-on-failure`; `python tools/shutdown_check.py`.
- [ ] **Step 2: `/gate`** (fast tier incl. WSL Linux step; set `MMO_E2E_MYSQL_PASSWORD`). Fix gcc-only failures.
- [ ] **Step 3: Submodule landing check** — `git -C data/editor branch -a --contains HEAD` and same for `data/client`: the pointers must be on (or merged into) the submodules' `master` before `/ship`; ask the user before pushing anything.
- [ ] **Step 4: Report** — screenshots from Task 10 Step 9, E2E result, gate report, the list of deferred items (bespoke icons, more recipes, dyes, unlearning, profession limit) and the quest-3 ender deviation.
