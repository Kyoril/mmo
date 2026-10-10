# GM Player Observation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A GM can `port <name>` into the target player's dungeon instance and observe invisibly (`invisible on|off`), with invisibility and godmode persisted across map changes and relogs.

**Architecture:** The realm owns a per-character `gm_flags` bitmask (DB column, carried in `CharacterData`) and applies it on every world join; the world server turns the invisible flag into `unit_visibility::Off`, which already hides the unit from non-GM players and creature AI. `port` carries the target's instance id as a one-shot transfer override on the realm, and the world node forces a real transfer when the instance differs on the same map.

**Tech Stack:** C++17, ASIO, MySQL, Catch2 (`src/tests/*_tests`), Lua E2E harness (`e2e/`).

**Spec:** [docs/superpowers/specs/2026-10-10-gm-observation-design.md](../specs/2026-10-10-gm-observation-design.md)

## Global Constraints

- Code style: Allman braces, braces on every `if`, tabs, `m_camelCase` members, `PascalCase` methods, `#pragma once`, Doxygen on public members, header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on new files.
- No exceptions in game code; use `ASSERT` / `DLOG` / `WLOG` / `ELOG`.
- Every dev-command code path stays inside `#if MMO_WITH_DEV_COMMANDS` / `#ifdef MMO_WITH_DEV_COMMANDS` like its neighbours.
- `invisible` requires `gm_level::Gm` (1). `godmode` keeps `gm_level::SeniorGm` (2).
- `gm_flags`: bit `Invisible = 1`, bit `Godmode = 2`. Forced to 0 on the realm when the account's gm level is 0.
- Migration must be additive: `ALTER TABLE characters ADD COLUMN gm_flags TINYINT UNSIGNED NOT NULL DEFAULT 0`. Do **not** edit `realm_db_schema_full.sql` (it is a base schema; all `updates/realm` migrations apply on top, there are no history inserts in it).
- Wire changes: bump `mmo::auth::ProtocolVersion` 8 → 9 and `mmo::game::ProtocolVersion` 0x13 → 0x14, run `python tools/protocol_version_check.py --update`, add rows to `docs/protocol_versions.md`.
- Realm and world servers are single-threaded io; no new threads.
- Server realm GM feedback messages are plain English (`ChatType::System`), not localization keys.
- Build: `cmake --build build --config Debug -t <target>`; tests: `build/bin/Debug/<suite>.exe "[tag]"` (or `ctest -C Debug -R <suite>` from `build/`). If `build/` does not exist in this worktree, configure first with the Windows command in CLAUDE.md (`-DMMO_BUILD_CLIENT=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON`).

## Review Focus

1. **Stale instance override:** a `port` whose teleport never completes (target logs out between fetch and transfer, transfer aborted) must not redirect a later, unrelated map change into the target's instance. Expected: override cleared at every `OnMoveWorldPortAck`, and ignored when the map does not match. → Task 6 test `"Override for another map is ignored and cleared"`.
2. **Invisible GM toggled inside an instance:** a GM who turns invisibility on or off while inside a dungeon must not drift `GetPlayerCount()` (e.g. counted on add, not counted on remove → stuck at 1 or underflow). → Task 3 test `"Toggling invisibility inside the instance keeps the player count consistent"`.
3. **Stealth aura on an invisible GM:** a stealth/visibility aura applied or removed (or interrupted) while GM-invisible must not make the GM visible. → Task 2 test `"Visibility recomputation keeps a GM-invisible unit Off"`.
4. **Demoted account:** a character with `gm_flags` stored but an account whose gm level dropped to 0 must log in visible and mortal. → Task 5 test `"SanitizeGmFlags clears flags without a gm level"`.
5. **Same-map port into another instance:** the client ignores a `TransferPending` for its current map, so a same-map instance switch would hang on the loading screen. Expected: transfer completes. → Task 6 client change + manual test step M4.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/shared/game/gm_flags.h` (new) | `gm_flags` bit enum + `SanitizeGmFlags(flags, gmLevel)` |
| `src/shared/game_server/character_data.h` | `gmFlags` field + serialization |
| `src/shared/game_server/objects/game_unit_s.{h,cpp}` | `SetGmInvisible` / `IsGmInvisible`, precedence in `NotifyVisibilityChanged` |
| `src/shared/game_server/world/world_instance.{h,cpp}` | counted-player set, GM-invisible players not counted |
| `src/shared/game_protocol/game_protocol.h` | `CheatInvisible` opcode, version bump |
| `src/shared/auth_protocol/auth_protocol.h` | version bump |
| `src/realm_server/instance_override.h` (new) | pure one-shot override logic, unit-testable |
| `src/realm_server/player.{h,cpp}` | gm-flag command handler, persistence, system messages, port override, binding skip |
| `src/realm_server/database.h`, `mysql_database.{h,cpp}` | load `gm_flags`, `SetCharacterGmFlags` |
| `src/realm_server/world.{h,cpp}` | `SendTeleportRequest` carries instance id |
| `src/world_server/realm_connector.cpp` | apply flags on join, forced transfer on teleport request |
| `src/world_server/player.{h,cpp}`, `player_dev_handlers.cpp` | `BeginMapTransfer`, `OnCheatInvisible` |
| `src/mmo_client/...` | console command, packet writer, same-map transfer fix |
| `src/shared/bot_core/bot_realm_connector.{h,cpp}`, `src/e2e_client/scenario_engine.cpp` | `CheatInvisible`, `GM.Invisible` |
| `data/realm/updates/20261010_1_character_gm_flags.sql` (new) | migration |
| `e2e/scenarios/gm_observer_persistence.lua` (new), `e2e/README.md` | E2E |
| tests: `src/tests/game_tests/test_gm_flags.cpp`, `src/tests/game_server_tests/gm_invisibility_test.cpp`, `src/tests/realm_server_tests/test_instance_override.cpp` (new) | unit tests |

---

### Task 1: `gm_flags` header and `CharacterData.gmFlags`

**Files:**
- Create: `src/shared/game/gm_flags.h`
- Modify: `src/shared/game_server/character_data.h:153` (field), `:241` (read), `:386` (write)
- Modify: `src/shared/auth_protocol/auth_protocol.h:34` (version 8 → 9)
- Test: `src/tests/game_tests/test_gm_flags.cpp`

**Interfaces:**
- Produces: `mmo::gm_flags::Type { None = 0, Invisible = 1, Godmode = 2 }`; `uint8 mmo::SanitizeGmFlags(uint8 flags, uint8 gmLevel)`; `CharacterData::gmFlags` (`uint8`, default 0) serialized right after `isGameMaster`.

- [ ] **Step 1: Write the failing test** — `src/tests/game_tests/test_gm_flags.cpp`

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/gm_flags.h"
#include "game/gm_level.h"
#include "game_server/character_data.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"
#include "binary_io/memory_source.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("SanitizeGmFlags clears flags without a gm level", "[gm_flags]")
{
	const uint8 both = gm_flags::Invisible | gm_flags::Godmode;
	CHECK(SanitizeGmFlags(both, gm_level::Player) == gm_flags::None);
	CHECK(SanitizeGmFlags(both, gm_level::Gm) == both);
	CHECK(SanitizeGmFlags(both, gm_level::Operator) == both);
}

TEST_CASE("SanitizeGmFlags drops unknown bits", "[gm_flags]")
{
	CHECK(SanitizeGmFlags(0xFF, gm_level::Gm) == (gm_flags::Invisible | gm_flags::Godmode));
}

TEST_CASE("CharacterData round-trips gmFlags", "[gm_flags]")
{
	CharacterData out;
	out.name = "Observer";
	out.isGameMaster = true;
	out.gmFlags = gm_flags::Invisible | gm_flags::Godmode;

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << out;

	io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
	io::Reader reader{ source };
	CharacterData in;
	REQUIRE(reader >> in);
	CHECK(in.gmFlags == out.gmFlags);
	CHECK(in.isGameMaster);
	CHECK(in.name == "Observer");
}
```

(If the binary_io header names differ, copy the include set from an existing round-trip test: `grep -rl "VectorSink" src/tests | head -1`.)

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Debug -t game_tests`
Expected: compile error — `game/gm_flags.h` not found.

- [ ] **Step 3: Implement** — `src/shared/game/gm_flags.h`

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Per-character game master state that survives map changes and relogs. Stored in the
	/// realm database (characters.gm_flags) and carried to world nodes in CharacterData.
	namespace gm_flags
	{
		enum Type : uint8
		{
			/// No game master state active.
			None = 0,

			/// The character is invisible to players and creatures (other game masters still see it).
			Invisible = 1 << 0,

			/// The character ignores all incoming damage.
			Godmode = 1 << 1,

			/// Every known flag.
			All = Invisible | Godmode,
		};
	}

	/// Returns the flags that may actually apply for an account of the given gm level: none for
	/// a regular player account (a demoted account must never stay hidden), unknown bits dropped.
	/// @param flags The stored flags.
	/// @param gmLevel The account's gm level (gm_level::Type).
	/// @returns The effective flags.
	inline uint8 SanitizeGmFlags(const uint8 flags, const uint8 gmLevel)
	{
		if (gmLevel == 0)
		{
			return gm_flags::None;
		}

		return flags & gm_flags::All;
	}
}
```

In `character_data.h`: add after `bool isGameMaster = false;`

```cpp
		/// Persistent game master state (gm_flags::Type bits). Owned by the realm.
		uint8 gmFlags = 0;
```

Reader: replace `>> io::read<uint8>(data.isGameMaster)` with
`>> io::read<uint8>(data.isGameMaster)` followed by `>> io::read<uint8>(data.gmFlags)`.
Writer: after `<< io::write<uint8>(data.isGameMaster)` add `<< io::write<uint8>(data.gmFlags)`.

`auth_protocol.h:34`: `constexpr uint32 ProtocolVersion = 0x00000009;`

- [ ] **Step 4: Run tests**

Run: `cmake --build build --config Debug -t game_tests` then `build/bin/Debug/game_tests.exe "[gm_flags]"`
Expected: all 3 test cases pass.

- [ ] **Step 5: Protocol record and commit**

Run `python tools/protocol_version_check.py --update`. Add to the `### auth` table in `docs/protocol_versions.md` (top row):
`| 9 | 2026-10-10 | GM observation: CharacterData carries uint8 gmFlags after isGameMaster; realm→world TeleportRequest carries the target InstanceId |`
Also fix the stale "Current" cells in the table at the top of that file (auth 9, game 20 after Task 4).

```bash
git add src/shared/game/gm_flags.h src/shared/game_server/character_data.h src/shared/auth_protocol/auth_protocol.h src/tests/game_tests/test_gm_flags.cpp src/shared/protocol_fingerprint.json docs/protocol_versions.md
git commit -m "feat(gm): gm_flags bitmask carried in CharacterData"
```

---

### Task 2: GM invisibility on `GameUnitS`

**Files:**
- Modify: `src/shared/game_server/objects/game_unit_s.h:911-914` (next to godmode), `:1847` (member)
- Modify: `src/shared/game_server/objects/game_unit_s.cpp:1752` (`NotifyVisibilityChanged`)
- Test: `src/tests/game_server_tests/gm_invisibility_test.cpp`

**Interfaces:**
- Produces: `void GameUnitS::SetGmInvisible(bool enable)`, `bool GameUnitS::IsGmInvisible() const`.

- [ ] **Step 1: Write the failing tests** — `src/tests/game_server_tests/gm_invisibility_test.cpp`. Copy the `MakeUnit` helper verbatim from `stealth_visibility_test.cpp:17-51` into an anonymous namespace, then:

```cpp
TEST_CASE("GM-invisible unit is hidden from players and visible to GMs", "[gm_invisibility]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto gm = MakeUnit(project, timers, 10);
	const auto player = MakeUnit(project, timers, 60);
	const auto otherGm = MakeUnit(project, timers, 10);
	otherGm->SetIsGameMaster(true);

	gm->SetGmInvisible(true);
	CHECK(gm->IsGmInvisible());
	CHECK(gm->GetVisibility() == unit_visibility::Off);

	player->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	otherGm->Relocate(Vector3(0.0f, 0.0f, 0.0f), Radian(0.0f));
	gm->Relocate(Vector3(1.0f, 0.0f, 0.0f), Radian(0.0f));

	CHECK_FALSE(gm->CanBeSeenBy(*player));
	CHECK(gm->CanBeSeenBy(*otherGm));
}

TEST_CASE("Visibility recomputation keeps a GM-invisible unit Off", "[gm_invisibility]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto gm = MakeUnit(project, timers, 10);
	gm->SetGmInvisible(true);

	// Aura add/remove and interrupt-removal all funnel into NotifyVisibilityChanged; without
	// any visibility aura it used to fall through to On.
	gm->NotifyVisibilityChanged();
	CHECK(gm->GetVisibility() == unit_visibility::Off);
}

TEST_CASE("Turning GM invisibility off restores normal visibility", "[gm_invisibility]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	const auto gm = MakeUnit(project, timers, 10);
	gm->SetGmInvisible(true);
	gm->SetGmInvisible(false);

	CHECK_FALSE(gm->IsGmInvisible());
	CHECK(gm->GetVisibility() == unit_visibility::On);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Debug -t game_server_tests`
Expected: compile error — `SetGmInvisible` is not a member of `GamePlayerS`.

- [ ] **Step 3: Implement**

`game_unit_s.h`, after `IsGodmode()`:

```cpp
		/// @brief Enables or disables game master invisibility. While enabled the unit's visibility
		///        is forced to unit_visibility::Off regardless of stealth or invisibility auras, so it
		///        is hidden from players and creatures and only other game masters see it.
		/// @param enable True to hide the unit.
		void SetGmInvisible(bool enable);

		/// @brief Whether game master invisibility is enabled on this unit.
		bool IsGmInvisible() const { return m_gmInvisible; }
```

Member next to `bool m_godmode = false;`: `bool m_gmInvisible = false;`

`game_unit_s.cpp`, before `NotifyVisibilityChanged`:

```cpp
	void GameUnitS::SetGmInvisible(const bool enable)
	{
		m_gmInvisible = enable;
		NotifyVisibilityChanged();
	}
```

At the top of `NotifyVisibilityChanged`:

```cpp
		// Game master invisibility outranks every aura: an observing GM must never be revealed
		// by a stealth or invisibility aura being added, removed or interrupted.
		if (m_gmInvisible)
		{
			SetVisibility(unit_visibility::Off);
			return;
		}
```

- [ ] **Step 4: Run tests**

Run: `build/bin/Debug/game_server_tests.exe "[gm_invisibility],[stealth],[godmode]"`
Expected: all pass (new and existing).

- [ ] **Step 5: Commit**

```bash
git add src/shared/game_server/objects/game_unit_s.h src/shared/game_server/objects/game_unit_s.cpp src/tests/game_server_tests/gm_invisibility_test.cpp
git commit -m "feat(gm): GM invisibility outranks visibility auras"
```

---

### Task 3: Instance player count ignores GM-invisible players; creature aggro check

**Files:**
- Modify: `src/shared/game_server/world/world_instance.h:378,490`
- Modify: `src/shared/game_server/world/world_instance.cpp:612-623` (add), `:713-732` (remove)
- Test: `src/tests/game_server_tests/gm_invisibility_test.cpp` (append)

**Interfaces:**
- Consumes: `GameUnitS::IsGmInvisible()` (Task 2).
- Produces: `WorldInstance::GetPlayerCount()` keeps its signature; it now returns the number of *counted* players.

- [ ] **Step 1: Write the failing tests.** Append to `gm_invisibility_test.cpp` a fixture built like `AssistFixture` in `creature_assist_test.cpp:41-127` (same includes, `NullTriggerHandler`, map 0 with directory `Worlds/__gm_test__/__gm_test__`, `WorldInstanceManager`, `world = &manager->CreateInstance(0)`, `RunPosted()`, destructor `io.stop()`), plus `project.classes` setup via `MakeUnit`. Give each player a distinct guid with `unit->Set<uint64>(object_fields::Guid, guid)` before adding.

```cpp
TEST_CASE_METHOD(GmWorldFixture, "GM-invisible player is not counted by the instance", "[gm_invisibility]")
{
	const auto gm = MakePlayer(0x10);
	gm->SetGmInvisible(true);
	world->AddGameObject(*gm);
	CHECK(world->GetPlayerCount() == 0);

	const auto player = MakePlayer(0x11);
	world->AddGameObject(*player);
	CHECK(world->GetPlayerCount() == 1);

	world->RemoveGameObject(*gm);
	CHECK(world->GetPlayerCount() == 1);
	world->RemoveGameObject(*player);
	CHECK(world->GetPlayerCount() == 0);
}

TEST_CASE_METHOD(GmWorldFixture, "Toggling invisibility inside the instance keeps the player count consistent", "[gm_invisibility]")
{
	// Counted on entry, hidden afterwards: leaving must still uncount it exactly once.
	const auto gm = MakePlayer(0x20);
	world->AddGameObject(*gm);
	CHECK(world->GetPlayerCount() == 1);
	gm->SetGmInvisible(true);
	world->RemoveGameObject(*gm);
	CHECK(world->GetPlayerCount() == 0);

	// Not counted on entry, visible afterwards: leaving must not underflow or uncount others.
	const auto other = MakePlayer(0x21);
	world->AddGameObject(*other);
	const auto gm2 = MakePlayer(0x22);
	gm2->SetGmInvisible(true);
	world->AddGameObject(*gm2);
	gm2->SetGmInvisible(false);
	world->RemoveGameObject(*gm2);
	CHECK(world->GetPlayerCount() == 1);
}
```

`MakePlayer(guid)` = `MakeUnit(project, timers, 10)` + `Set<uint64>(object_fields::Guid, guid)` + `Relocate(Vector3(0,0,0), Radian(0))`. If `AddGameObject` on a bare `GamePlayerS` asserts (no net watcher), stop and report the assert text — do not work around it by changing production code.

Creature aggro on an invisible GM is not unit-tested here: idle aggro is driven by movement watchers that need a full player session. The AI already gates on `CanBeSeenBy` (`creature_ai_idle_state.cpp:54`), whose `Off` behaviour Task 2 pins; the end-to-end behaviour is covered by the E2E in Task 7.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --config Debug -t game_server_tests` then `build/bin/Debug/game_server_tests.exe "[gm_invisibility]"`
Expected: the first two tests FAIL (count is 1 / 2 where 0 / 1 is expected).

- [ ] **Step 3: Implement.** `world_instance.h`: replace `uint32 m_playerCount{ 0 };` with

```cpp
		/// Players counted towards the instance (game-master observers are not), by guid. Removal
		/// uncounts exactly the players that were counted on entry.
		std::unordered_set<uint64> m_countedPlayers;
		uint32 m_playerCount{ 0 };
```

(add `#include <unordered_set>` if missing). In `AddGameObject` replace the player block with:

```cpp
		if (added.GetTypeId() == ObjectTypeId::Player)
		{
			// An invisible game master observes; it must not start the instance, count towards
			// PlayerCount conditions or fire OnPlayerEnterInstance.
			if (added.AsUnit().IsGmInvisible())
			{
				return;
			}

			m_countedPlayers.insert(added.GetGuid());
			m_playerCount = static_cast<uint32>(m_countedPlayers.size());

			// Start instance-wide periodic timers once the first player is present.
			if (m_playerCount == 1)
			{
				StartInstanceTimers();
			}

			FireInstanceTriggerEvent(trigger_event::OnPlayerEnterInstance, nullptr);
		}
```

In `RemoveGameObject` replace the player block with:

```cpp
		if (remove.GetTypeId() == ObjectTypeId::Player)
		{
			if (m_countedPlayers.erase(remove.GetGuid()) == 0)
			{
				return;
			}

			m_playerCount = static_cast<uint32>(m_countedPlayers.size());

			FireInstanceTriggerEvent(trigger_event::OnPlayerLeaveInstance, nullptr);

			if (m_playerCount == 0)
			{
				// No players left — stop instance-wide periodic timers.
				StopInstanceTimers();

				if (IsInstancedPvE())
				{
					FireInstanceTriggerEvent(trigger_event::OnAllPlayersDead, nullptr);
				}
			}
		}
```

Note: `remove.GetGuid()` is read after `remove.destroy(remove)` in the existing order — capture `const uint64 removedGuid = remove.GetGuid();` near the top of `RemoveGameObject` (next to `auto strong = ...`) and use it, since `destroy` may release state.

- [ ] **Step 4: Run tests**

Run: `build/bin/Debug/game_server_tests.exe` (whole suite — the count drives triggers used by other tests)
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/shared/game_server/world/world_instance.h src/shared/game_server/world/world_instance.cpp src/tests/game_server_tests/gm_invisibility_test.cpp
git commit -m "feat(gm): invisible GMs do not count as instance players"
```

---

### Task 4: `invisible` command end to end (client → realm → world)

**Files:**
- Modify: `src/shared/game_protocol/game_protocol.h` (append opcode before `Count_` at ~`:446`; version `:84` → `0x00000014`)
- Modify: `src/mmo_client/net/realm_connector.{h,cpp}` (next to `CheatGodmode` `:326` / `:641`)
- Modify: `src/mmo_client/game_states/world_state.h:591`, `world_state.cpp:2030`, `world_state_commands.cpp:487`
- Modify: `src/shared/bot_core/bot_realm_connector.{h,cpp}` (`:464` / `:2534`)
- Modify: `src/e2e_client/scenario_engine.cpp` (`:1298`, `:1615`, `:1691`)
- Modify: `src/realm_server/player.cpp` (`OnProxyPacket` switch `:786`, registration `:3239`)
- Modify: `src/world_server/player.h:735`, `player.cpp:839`, `player_dev_handlers.cpp:794`

**Interfaces:**
- Consumes: `GameUnitS::SetGmInvisible(bool)` (Task 2).
- Produces: opcode `game::client_realm_packet::CheatInvisible` (payload `uint8 enable`); `RealmConnector::CheatInvisible(bool)` (client) and `BotRealmConnector::CheatInvisible(bool)`; Lua `GM.Invisible(enable)`; world `Player::OnCheatInvisible`.

This task is wiring; its test is the build plus the E2E in Task 7. No new unit test.

- [ ] **Step 1: Opcode.** In `game_protocol.h`, directly before `/// Counter constant` / `Count_,` in the client→realm enum:

```cpp
				/// GAME MASTER. Toggles game master invisibility on the sender's character: hidden from
				/// players and creatures, visible to other game masters. Persisted by the realm.
				/// Payload: uint8 (0/1).
				CheatInvisible, // GAME MASTER
```

Bump `ProtocolVersion` to `0x00000014`.

- [ ] **Step 2: Client.** `realm_connector.h` after `void CheatGodmode(bool enable);`: `void CheatInvisible(bool enable);` with a Doxygen line. `realm_connector.cpp`, copying `CheatGodmode`:

```cpp
	void RealmConnector::CheatInvisible(bool enable)
	{
		sendSinglePacket([enable](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::CheatInvisible);
				packet << io::write<uint8>(enable ? 1 : 0);
				packet.Finish();
			});
	}
```

`world_state.h` after `Command_Godmode`: `void Command_Invisible(const std::string &cmd, const std::string &args) const;`
`world_state_commands.cpp`: copy `Command_Godmode` (`:487-500`) verbatim as `Command_Invisible`, calling `m_realmConnector.CheatInvisible(enable)` and logging `"GM invisibility " << (enable ? "enabled" : "disabled")`.
`world_state.cpp` next to the godmode registration (`:2030`), same macro block and category:

```cpp
		console::RegisterCommand("invisible", [this](const std::string &cmd, const std::string &args)
								 { Command_Invisible(cmd, args); }, ConsoleCommandCategory::Gm, "Hides your character from players and creatures (other GMs still see you). Persists across map changes and relogs. 'invisible' or 'invisible 1' enables it, 'invisible 0' disables it.");
```

(Match the exact registration call used for `godmode` on that line — copy it and change the name, handler and text.)

- [ ] **Step 3: Bot/E2E.** `BotRealmConnector::CheatInvisible` = copy of `CheatGodmode` (`:2534`) with the new opcode; declare in the header next to `CheatGodmode`. In `scenario_engine.cpp` add `luaGmInvisible` copying `luaGmGodmode` (transcript text `GM.Invisible(true|false)`), register `luabind::def_lambda("GM_Invisible", &luaGmInvisible),` and `Invisible = GM_Invisible,` beside the godmode entries.

- [ ] **Step 4: Realm gate and forwarding.** In `OnProxyPacket`'s switch add `case game::client_realm_packet::CheatInvisible:` to the gm-level-1 group (with `CheatTeleportToPlayer`). Register it next to `CheatGodmode` (`:3239`): `m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatInvisible, *this, &Player::OnProxyPacket);` (Task 5 replaces both registrations with the persisting handler.)

- [ ] **Step 5: World handler.** `player.h` next to `OnCheatGodmode`: `void OnCheatInvisible(uint16 opCode, uint32 size, io::Reader& contentReader);`. `player.cpp` dispatch switch next to `case game::client_realm_packet::CheatGodmode:`:

```cpp
		case game::client_realm_packet::CheatInvisible:
			OnCheatInvisible(opCode, buffer.size(), reader);
			break;
```

`player_dev_handlers.cpp` after `OnCheatGodmode`:

```cpp
#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatInvisible(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 enable = 0;
		if (!(contentReader >> io::read<uint8>(enable)))
		{
			ELOG("Failed to read CheatInvisible packet!");
			return;
		}

		if (!m_character)
		{
			return;
		}

		DLOG("GM invisibility " << (enable ? "enabled" : "disabled") << " on character " << log_hex_digit(m_character->GetGuid()));
		m_character->SetGmInvisible(enable != 0);
	}
#endif
```

- [ ] **Step 6: Build and protocol record**

Run: `cmake --build build --config Debug -t mmo_client realm_server world_server e2e_client` → succeeds.
Run: `python tools/protocol_version_check.py --update`. Add `### game` row:
`| 20 | 2026-10-10 | GM observation: client `CheatInvisible` (uint8 enable), handled by the realm (persisted) and forwarded to the world node |`

- [ ] **Step 7: Commit**

```bash
git add -A src/shared/game_protocol src/mmo_client src/shared/bot_core src/e2e_client src/realm_server/player.cpp src/world_server src/shared/protocol_fingerprint.json docs/protocol_versions.md
git commit -m "feat(gm): invisible console command"
```

---

### Task 5: Persist GM flags (DB, realm, world join)

**Files:**
- Create: `data/realm/updates/20261010_1_character_gm_flags.sql`
- Modify: `src/realm_server/database.h` (near `SetCharacterChannelState` `:316`), `mysql_database.h:184`, `mysql_database.cpp:557-609` (load), new `SetCharacterGmFlags`
- Modify: `src/realm_server/player.h` (dev-command block `:613`), `player.cpp` (`OnCharacterData` `:3554`, world-character update `:2885`, registrations `:3239`, `OnWorldJoined` `:3328`, `OnWorldChanged` `:3442`)
- Modify: `src/world_server/realm_connector.cpp:773`
- Test: build + E2E (Task 7); `SanitizeGmFlags` already unit-tested in Task 1.

**Interfaces:**
- Consumes: `gm_flags`, `SanitizeGmFlags`, `CharacterData::gmFlags` (Task 1); `SetGmInvisible` (Task 2); `CheatInvisible` opcode (Task 4).
- Produces: `IDatabase::SetCharacterGmFlags(uint64 characterId, uint8 flags)`; realm `Player::OnCheatGmFlag(game::IncomingPacket&)`, `Player::SendSystemMessage(const String&)`, `Player::ForwardProxyPayload(uint16 opCode, const std::vector<uint8>& payload)`, `Player::SendGmFlagStatus()`.

- [ ] **Step 1: Migration** — `data/realm/updates/20261010_1_character_gm_flags.sql`

```sql
-- GM observation: persistent game master state per character (bit 1 = invisible, bit 2 = godmode).
ALTER TABLE `characters`
ADD COLUMN `gm_flags` tinyint unsigned NOT NULL DEFAULT 0;
```

- [ ] **Step 2: DB.** `database.h` next to `SetCharacterChannelState`:

```cpp
		/// Stores the persistent game master flags (gm_flags::Type bits) of a character.
		virtual void SetCharacterGmFlags(uint64 characterId, uint8 flags) = 0;
```

`mysql_database.h`: `/// @copydoc IDatabase::SetCharacterGmFlags` + `void SetCharacterGmFlags(uint64 characterId, uint8 flags) override;`
`mysql_database.cpp`, modelled on `SetCharacterChannelState` (`:2199`):

```cpp
	void MySQLDatabase::SetCharacterGmFlags(const uint64 characterId, const uint8 flags)
	{
		std::lock_guard<std::recursive_mutex> dbLock(m_databaseMutex);

		if (!m_connection.Execute(std::format(
			"UPDATE `characters` SET `gm_flags` = '{0}' WHERE `id` = '{1}' LIMIT 1"
			, static_cast<uint32>(flags)
			, characterId
		)))
		{
			PrintDatabaseError();
			throw mysql::Exception(m_connection.GetErrorMessage());
		}
	}
```

`CharacterEnterWorld` (`:557`): append `, gm_flags` to the SELECT column list after `sleep_pose`, and after `row.GetField(index++, result.sleepPoseEmote);` add `row.GetField<uint8, uint16>(index++, result.gmFlags);`.
If any other class implements `IDatabase` (check `grep -rn "public IDatabase\|: IDatabase" src`), add the override there too.

- [ ] **Step 3: Realm sanitize on enter world.** `OnCharacterData` (`:3554`), after `m_characterData->isGameMaster = (m_gmLevel > 0);`:

```cpp
		m_characterData->gmFlags = SanitizeGmFlags(m_characterData->gmFlags, m_gmLevel);
```

The world-character update at `:2885` copies fields individually and never touches `gmFlags`, so the realm keeps owning it — leave it alone. Include `game/gm_flags.h` in `player.cpp`.

- [ ] **Step 4: Realm handler.** `player.h` dev block: declare `PacketParseResult OnCheatGmFlag(game::IncomingPacket &packet);`. Outside the dev block (private): `void SendSystemMessage(const String &message);`, `void ForwardProxyPayload(uint16 opCode, const std::vector<uint8> &payload);`, `void SendGmFlagStatus();` with Doxygen.

`player.cpp`:

```cpp
	void Player::SendSystemMessage(const String &message)
	{
		m_connection->sendSinglePacket([&message](game::OutgoingPacket &outPacket)
		{
			outPacket.Start(game::realm_client_packet::ChatMessage);
			outPacket
				<< io::write_packed_guid(0)
				<< io::write<uint8>(ChatType::System)
				<< io::write_range(message) << io::write<uint8>(0)
				<< io::write<uint8>(0)
				<< io::write<uint8>(0);
			outPacket.Finish();
		});
	}

	void Player::ForwardProxyPayload(const uint16 opCode, const std::vector<uint8> &payload)
	{
		const auto strongWorld = m_world.lock();
		if (!strongWorld || !m_characterData)
		{
			return;
		}

		const auto characterId = m_characterData->characterId;
		strongWorld->GetConnection().sendSinglePacket([characterId, opCode, &payload](auth::OutgoingPacket &outPacket)
		{
			outPacket.Start(auth::realm_world_packet::ProxyPacket);
			outPacket
				<< io::write<uint64>(characterId)
				<< io::write<uint16>(opCode)
				<< io::write<uint32>(payload.size());
			if (!payload.empty())
			{
				outPacket << io::write_range(payload);
			}
			outPacket.Finish();
		});
	}

	void Player::SendGmFlagStatus()
	{
		if (!m_characterData || m_characterData->gmFlags == gm_flags::None)
		{
			return;
		}

		if (m_characterData->gmFlags & gm_flags::Invisible)
		{
			SendSystemMessage("GM invisibility is on.");
		}
		if (m_characterData->gmFlags & gm_flags::Godmode)
		{
			SendSystemMessage("Godmode is on.");
		}
	}
```

(`OnProxyPacket` can be refactored to use `ForwardProxyPayload` if it stays byte-identical; optional.)

Dev-command handler (inside `#ifdef MMO_WITH_DEV_COMMANDS`, next to `OnCheatSummon`):

```cpp
	PacketParseResult Player::OnCheatGmFlag(game::IncomingPacket &packet)
	{
		const bool isInvisible = packet.GetId() == game::client_realm_packet::CheatInvisible;
		const uint8 flag = isInvisible ? gm_flags::Invisible : gm_flags::Godmode;

		if (!HasGMLevel(isInvisible ? gm_level::Gm : gm_level::SeniorGm))
		{
			WLOG("Player " << m_characterData->name << " attempted to use a GM command without sufficient privileges");
			return PacketParseResult::Pass;
		}

		uint8 enable = 0;
		if (!(packet >> io::read<uint8>(enable)))
		{
			return PacketParseResult::Disconnect;
		}

		if (enable)
		{
			m_characterData->gmFlags |= flag;
		}
		else
		{
			m_characterData->gmFlags &= ~flag;
		}

		m_database.asyncRequestKeyed(m_characterData->characterId, [](bool) {}, &IDatabase::SetCharacterGmFlags, m_characterData->characterId, m_characterData->gmFlags);

		SendSystemMessage(std::string(isInvisible ? "GM invisibility" : "Godmode") + (enable ? " is on." : " is off."));
		ForwardProxyPayload(static_cast<uint16>(packet.GetId()), { enable });
		return PacketParseResult::Pass;
	}
```

Check how `asyncRequestKeyed` reports failures for a `void` DB call with `[](bool) {}` (that is what `SetCharacterChannelState` callers pass at `:1732`); match it exactly. The DB-failure log required by the spec comes from the `ELOG`/`PrintDatabaseError` path inside the DB worker.

Registrations (`:3239`): replace the `CheatGodmode` `OnProxyPacket` registration and the Task 4 `CheatInvisible` one with
`m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatGodmode, *this, &Player::OnCheatGmFlag);`
`m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatInvisible, *this, &Player::OnCheatGmFlag);`
and remove both cases from the `OnProxyPacket` switch (the handler checks itself).

`OnWorldJoined` (after `SendFullSubsystemStatus();`) and `OnWorldChanged` (at the end): call `SendGmFlagStatus();`. Only `OnWorldJoined` is strictly needed for "after relog"; on `OnWorldChanged` it reminds after each port — keep both, it is a GM-only message.

- [ ] **Step 5: World applies flags on join.** `world_server/realm_connector.cpp`, after `characterObject->SetIsGameMaster(characterData.isGameMaster);` (`:773`) and before `ClearFieldChanges()` / `AddGameObject`:

```cpp
		// Persistent GM state (the realm already cleared it for accounts without a gm level). Applied
		// before the character enters the instance so an invisible observer is never spawned to
		// other players and never counted as an instance player.
		characterObject->SetGodmode((characterData.gmFlags & gm_flags::Godmode) != 0);
		characterObject->SetGmInvisible((characterData.gmFlags & gm_flags::Invisible) != 0);
```

Include `game/gm_flags.h`.

- [ ] **Step 6: Build**

Run: `cmake --build build --config Debug -t realm_server world_server` and `-t all_tests`, then `cd build && ctest -C Debug --output-on-failure`
Expected: build OK, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add data/realm/updates/20261010_1_character_gm_flags.sql src/realm_server src/world_server/realm_connector.cpp
git commit -m "feat(gm): persist invisibility and godmode per character"
```

---

### Task 6: `port <name>` joins the target's instance

**Files:**
- Create: `src/realm_server/instance_override.h`
- Create: `src/tests/realm_server_tests/test_instance_override.cpp`
- Modify: `src/realm_server/player.h` (members near `m_transferMap` `:467`), `player.cpp` (`OnCheatTeleportToPlayer` `:2374`, `SendTeleportRequest` `:4256`, `OnMoveWorldPortAck` `:1199`, binding blocks in `OnWorldJoined` `:3341` / `OnWorldChanged` `:3456`)
- Modify: `src/realm_server/world.{h,cpp}` (`SendTeleportRequest` `:125` / `:614`)
- Modify: `src/world_server/realm_connector.cpp:971` (`OnTeleportRequest`)
- Modify: `src/world_server/player.{h,cpp}` (`OnTeleport` `:3973`, new `BeginMapTransfer`)
- Modify: `src/mmo_client/game_states/world_state.{h,cpp}` (`OnTransferPending` `:5030`, `OnNewWorld` `:5070`)

**Interfaces:**
- Produces: `struct mmo::InstanceOverride { MapId mapId; InstanceId instanceId; }`; `std::optional<InstanceId> mmo::ConsumeInstanceOverride(std::optional<InstanceOverride>& pending, MapId transferMap)`; realm `Player::SendTeleportRequest(uint32 mapId, const Vector3&, const Radian&, const InstanceId& instanceId = {})`; `World::SendTeleportRequest(uint64, uint32, const Vector3&, const Radian&, const InstanceId&)`; world `Player::BeginMapTransfer(uint32 mapId, const Vector3&, const Radian&)`.

- [ ] **Step 1: Failing test** — `src/tests/realm_server_tests/test_instance_override.cpp`

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "realm_server/instance_override.h"

#include "catch.hpp"

using namespace mmo;

namespace
{
	InstanceId MakeId()
	{
		return InstanceId::from_string("0b9b1f3a-6a7e-4a52-9d3c-6f0e0d4b2a11").value();
	}
}

TEST_CASE("Override for the transfer map is used once", "[instance_override]")
{
	std::optional<InstanceOverride> pending = InstanceOverride{ 1, MakeId() };

	const auto used = ConsumeInstanceOverride(pending, 1);
	REQUIRE(used.has_value());
	CHECK(*used == MakeId());
	CHECK_FALSE(pending.has_value());

	CHECK_FALSE(ConsumeInstanceOverride(pending, 1).has_value());
}

TEST_CASE("Override for another map is ignored and cleared", "[instance_override]")
{
	std::optional<InstanceOverride> pending = InstanceOverride{ 1, MakeId() };

	CHECK_FALSE(ConsumeInstanceOverride(pending, 0).has_value());
	CHECK_FALSE(pending.has_value());
}

TEST_CASE("Nil override instance is never used", "[instance_override]")
{
	std::optional<InstanceOverride> pending = InstanceOverride{ 1, InstanceId{} };

	CHECK_FALSE(ConsumeInstanceOverride(pending, 1).has_value());
	CHECK_FALSE(pending.has_value());
}
```

(`InstanceId` is the uuid type used in `character_data.h`; if `from_string` returns a different optional type, mirror how `mysql_database.cpp:611` uses it.)

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Debug -t realm_server_tests`
Expected: compile error — `realm_server/instance_override.h` not found.

- [ ] **Step 3: Implement the header** — `src/realm_server/instance_override.h`

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/game.h"

#include <optional>

namespace mmo
{
	/// A one-shot request to place the next world transfer into a specific instance, instead of
	/// the one the character's own group or personal bindings resolve to. Used by the GM port
	/// command to follow a player into their dungeon instance.
	struct InstanceOverride
	{
		/// The map the override was issued for.
		MapId mapId = 0;

		/// The instance to join.
		InstanceId instanceId{};
	};

	/// Takes the pending override for a transfer to transferMap. The pending override is always
	/// cleared, used or not, so a teleport that never completed cannot redirect a later one.
	/// @param pending The pending override slot of the transferring player.
	/// @param transferMap The map the player is being transferred to.
	/// @returns The instance to join, or nothing if no override applies.
	inline std::optional<InstanceId> ConsumeInstanceOverride(std::optional<InstanceOverride>& pending, const MapId transferMap)
	{
		const std::optional<InstanceOverride> taken = pending;
		pending.reset();

		if (!taken || taken->mapId != transferMap || taken->instanceId.is_nil())
		{
			return std::nullopt;
		}

		return taken->instanceId;
	}
}
```

(Use whatever header defines `MapId`/`InstanceId` — find it with `grep -rn "using InstanceId\|typedef.*InstanceId" src/shared`; replace `game/game.h` accordingly.)

- [ ] **Step 4: Run test**

Run: `build/bin/Debug/realm_server_tests.exe "[instance_override]"` → 3 pass.

- [ ] **Step 5: Realm wiring.**

`player.h` members (next to `m_transferMap`):

```cpp
		/// Instance the next transfer must join (GM port to a player), consumed by OnMoveWorldPortAck.
		std::optional<InstanceOverride> m_pendingInstanceOverride;

		/// True while the character is in an instance it joined through an override: no dungeon
		/// binding is stored for it, so a visiting GM never rebinds their own group or themselves.
		bool m_joinedByInstanceOverride = false;
```

Change `SendTeleportRequest` declaration to `void SendTeleportRequest(uint32 mapId, const Vector3 &position, const Radian &facing, const InstanceId &instanceId = {}) const;` and pass `instanceId` through to `world->SendTeleportRequest(m_characterData->characterId, mapId, position, facing, instanceId);`.

`World::SendTeleportRequest` gains `const InstanceId& instanceId` and writes it last: `<< io::write<float>(facing.GetValueRadians()) << instanceId;` (capture `instanceId` by value in the lambda).

`OnCheatTeleportToPlayer` online branch (`:2416`): capture `playerName`; inside the callback, after the success check:

```cpp
				// Follow the target into its instance, even across copies of the same dungeon map.
				InstanceId targetInstance{};
				if (Player* target = strongThis->m_manager.GetPlayerByCharacterName(playerName))
				{
					targetInstance = target->GetWorldInstanceId();
				}

				if (!targetInstance.is_nil())
				{
					strongThis->m_pendingInstanceOverride = InstanceOverride{ mapId, targetInstance };
				}

				DLOG("Teleporting player to map " << mapId << ", instance " << targetInstance << ", location " << position << "...");
				strongThis->SendTeleportRequest(mapId, position, facing, targetInstance);
```

`OnMoveWorldPortAck` (`:1210-1218`) becomes:

```cpp
		// Resolve the correct instance id based on map type
		InstanceId targetInstanceId{};
		const std::optional<InstanceId> overrideInstance = ConsumeInstanceOverride(m_pendingInstanceOverride, m_transferMap);
		m_joinedByInstanceOverride = overrideInstance.has_value();
		const proto::MapEntry* mapEntry = m_project.maps.getById(m_transferMap);
		if (overrideInstance)
		{
			targetInstanceId = *overrideInstance;
			DLOG("Using instance override for transfer map " << m_transferMap << ": " << targetInstanceId);
		}
		else if (mapEntry && mapEntry->instancetype() != proto::MapEntry_MapInstanceType_GLOBAL)
		{
			// Dungeon/raid map: resolve instance from group or personal bindings
			targetInstanceId = ResolveDungeonInstanceId(m_transferMap);
			DLOG("Resolved dungeon instance id for transfer map " << m_transferMap << ": " << targetInstanceId);
		}
```

In `OnWorldChanged`'s binding block (`:3458`) add `&& !m_joinedByInstanceOverride` to the `if` condition, with the comment `// A GM who followed a player in is a visitor: never bind them or their group to that instance.` In `OnWorldJoined` (login path, never overridden) set `m_joinedByInstanceOverride = false;` before the binding block. Include `instance_override.h` in `player.h`.

- [ ] **Step 6: World forced transfer.** In `world_server/player.cpp`, move the whole `else` branch body of `OnTeleport` (`:4009-4044`) into a new public method and call it from `OnTeleport`:

```cpp
	void Player::BeginMapTransfer(const uint32 mapId, const Vector3& position, const Radian& facing)
	{
		// (moved body of the former OnTeleport else-branch, unchanged)
	}
```

Declare in `player.h` with Doxygen: "Leaves the current world instance and asks the realm to transfer the character to mapId — also used for a same-map move into another instance."

`RealmConnector::OnTeleportRequest`: read the trailing instance id and branch:

```cpp
		InstanceId instanceId{};
		if (!(packet >> io::read<uint64>(characterId) >> io::read<uint32>(mapId)
			>> io::read<float>(position.x)
			>> io::read<float>(position.y)
			>> io::read<float>(position.z)
			>> io::read<float>(facingRadianValue)
			>> instanceId))
		...
		// A GM following a player into another copy of the same dungeon map: the in-instance
		// relocation would keep them in their own copy, so force a real transfer.
		const WorldInstance* current = player->GetGameUnit().GetWorldInstance();
		if (!instanceId.is_nil() && current && current->GetMapId() == mapId && current->GetId() != instanceId)
		{
			player->BeginMapTransfer(mapId, position, Radian(facingRadianValue));
			return PacketParseResult::Pass;
		}

		player->GetGameUnit().Teleport(mapId, position, Radian(facingRadianValue));
```

(Use the existing accessor for the unit's world instance — `GameObjectS::GetWorldInstance()`.)

- [ ] **Step 7: Client same-map transfer.** The client ignores `TransferPending` for its current map (`world_state.cpp:5040`), so a same-map instance switch would never register the `NewWorld` handler. Add `bool m_transferPending = false;` to `WorldState` (header, with a Doxygen line) and change `OnTransferPending`:

```cpp
		ILOG("Transfer pending to map " << mapId << "...");
		if (m_transferPending)
		{
			// A transfer is already in progress; the realm only ever starts one at a time.
			return PacketParseResult::Pass;
		}

		m_transferPending = true;
		LoadingScreen::Show();
```

(remove the `g_mapId == mapId` early return; keep the rest). In `OnNewWorld` set `m_transferPending = false;` right after reading the packet. `BotRealmConnector` needs no change (it does not filter).

- [ ] **Step 8: Build and test**

Run: `cmake --build build --config Debug -t mmo_client realm_server world_server e2e_client all_tests` then `cd build && ctest -C Debug --output-on-failure`
Expected: all pass.

- [ ] **Step 9: Commit**

```bash
git add src/realm_server src/world_server src/mmo_client src/tests/realm_server_tests/test_instance_override.cpp
git commit -m "feat(gm): port follows the target into its instance"
```

---

### Task 7: E2E scenario and docs

**Files:**
- Create: `e2e/scenarios/gm_observer_persistence.lua`
- Modify: `e2e/README.md` (GM command list ~`:153`, gotcha `:171-176`)
- Modify: `docs/` — add `docs/gm-observation.md` (operator how-to)

**Interfaces:**
- Consumes: `GM.Invisible`, `GM.Godmode`, `GM.Worldport`, `GM.CreateMonster`, `GM.DamageTarget`, `Reconnect`, `TargetUnit("0x0")`.

- [ ] **Step 1: Write the scenario** — `e2e/scenarios/gm_observer_persistence.lua`

```lua
-- e2e-own-character: g
--
-- GM observation: invisibility and godmode survive a cross-map port and a relog, and an
-- invisible GM is ignored by a hostile creature standing next to it.
--
-- Uses: Choirbound Acolyte (entry 83, hostile level 11 caster), map 1 (Hollow Choir crypt).

local ACOLYTE = 83
local CRYPT_MAP = 1

-- Spawns a hostile acolyte, steps 6 yards away (well inside aggro range) and checks that the
-- GM is neither attacked nor pulled. Godmode is switched off for the check so that an attack
-- would actually show up as lost health.
local function assertIgnoredByAcolyte(label, mapId)
	local me = Me()
	local acolyte = GM.CreateMonster(ACOLYTE)
	GM.Worldport(mapId, GetPosX(me) + 6, GetPosY(me), GetPosZ(me), 0)
	GM.Godmode(false)
	TargetUnit("0x0")
	GM.Heal()
	Assert(WaitUntil(function() return GetHealth(me) == GetMaxHealth(me) end, 5000, "healed"), label .. ": heal")
	Sleep(6000)
	Assert(GetHealth(me) == GetMaxHealth(me), label .. ": invisible GM must not be attacked")
	GM.Godmode(true)
	GM.DestroyMonster(acolyte)
end

local function assertGodmode(label)
	TargetUnit("0x0")
	local me = Me()
	local health = GetHealth(me)
	GM.DamageTarget(50)
	Sleep(1500)
	Assert(GetHealth(me) == health, label .. ": godmode must absorb damage")
end

GM.Invisible(true)
GM.Godmode(true)
assertGodmode("same map")

-- Cross-map: into the dungeon (a new world instance on the world node).
GM.Worldport(CRYPT_MAP, 4.0, 0.5, -24.0, 0)
assertGodmode("after worldport")
assertIgnoredByAcolyte("after worldport", CRYPT_MAP)

-- Relog: flags come back from the realm database.
Assert(Reconnect(30000), "should log back in")
assertGodmode("after relog")
assertIgnoredByAcolyte("after relog", CRYPT_MAP)

-- Positive control: once visible and mortal, the same acolyte does engage. Without this the
-- checks above would also pass if the acolyte simply never aggroed on anyone.
local me = Me()
local acolyte = GM.CreateMonster(ACOLYTE)
GM.Worldport(CRYPT_MAP, GetPosX(me) + 6, GetPosY(me), GetPosZ(me), 0)
GM.Godmode(false)
GM.Invisible(false)
Assert(WaitUntil(function() return GetHealth(me) < GetMaxHealth(me) end, 15000, "acolyte engages a visible GM"),
	"a visible GM should be attacked by the acolyte")
GM.Invisible(true)
GM.Godmode(true)
GM.DestroyMonster(acolyte)
if not IsAlive(me) then
	GM.Revive()
end

-- Leave the character clean: the flags are persisted per character.
GM.Invisible(false)
GM.Godmode(false)
Log("GM observer state persisted across port and relog")
```

The `e2e-own-character` letter must be unique among scenarios: `grep -h "e2e-own-character" e2e/scenarios/*.lua | sort` and pick an unused one if `g` is taken. `GM.Heal()` with no target heals the GM (README); `TargetUnit("0x0")` clears the target.

- [ ] **Step 2: Run it**

```powershell
$env:MMO_E2E_MYSQL_PASSWORD = "<mysql password>"
cmake --build build -t e2e_client login_server realm_server world_server --config Debug
powershell -File tools/e2e/e2e_run.ps1 -Scenario gm_observer_persistence
```

(Check `tools/e2e/e2e_run.ps1` for the exact single-scenario parameter name.) Expected: PASS. If the relog step fails because the E2E database lacks the column, confirm the realm applied `20261010_1_character_gm_flags` on startup (realm log).

- [ ] **Step 3: Docs.** In `e2e/README.md`: add `GM.Invisible(enable)` (hides the GM character from players and creatures; persisted) next to `GM.Godmode`, and replace the gotcha "`GM.Godmode` does **not survive a cross-map teleport** …" with: "`GM.Godmode` and `GM.Invisible` are persisted per character by the realm: they survive ports and relogs, so turn them off at the end of a scenario that uses its own character."

Create `docs/gm-observation.md`: commands (`invisible on|off`, `godmode`, `port <name>`), required gm levels, what is hidden and what is not (friends/guild/whisper/channels show you online; your own casts, `/say`, emotes and loot rolls are visible to players), persistence (relog keeps you hidden; system message on login), and the step list from the spec's manual test plan.

- [ ] **Step 4: Commit**

```bash
git add e2e/scenarios/gm_observer_persistence.lua e2e/README.md docs/gm-observation.md
git commit -m "test(gm): E2E for observer persistence, GM observation docs"
```

---

### Task 8: Gate and manual verification

- [ ] **Step 1:** Run `/gate full` (fast tier + E2E + review; this touches transfers). Fix anything red, re-run.
- [ ] **Step 2: Manual test plan (two clients, dev stack)** — hand to the user:
  - M1: Player A enters a dungeon. GM B (not in A's group) runs `invisible`, `godmode`, `port A` → B lands next to A in A's instance.
  - M2: A does not see B; mobs next to B do not aggro; B sees A and the mobs.
  - M3: B is grouped with a third character C; after M1, C entering the same dungeon gets C's own instance, not A's (no group binding written).
  - M4: B enters their own copy of the same dungeon first, then `port A` → loading screen, B ends up in A's copy.
  - M5: B relogs → system message "GM invisibility is on.", still invisible to A.
  - M6: `invisible 0` → A sees B appear.
- [ ] **Step 3:** `/ship` once the user has signed off.
