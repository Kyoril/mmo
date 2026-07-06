# Stealth Movement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** WoW-style stealth — per-observer visibility (level difference + 120° front cone, party always sees), GUID-list visibility toggling for known units instead of spawn/despawn churn, and a creature alert state (stop, face, 3 s window, engage) with a per-creature client alert sound.

**Architecture:** Stealth is a new aura type (`ModStealth`) mapping to the existing-but-unimplemented `unit_visibility::GroupStealth` state. `CanBeSeenBy()` gains the per-observer stealth rule. Per-client visibility edges are tracked by the world-server `Player` (`m_spawnedGuids` + new `m_hiddenGuids`) and communicated via a new `UnitVisibilityList` packet. A periodic refresh in `WorldInstance::Update` re-evaluates stealthed units against subscribers. Creature idle AI routes stealth detections into a new `CreatureAIAlertState`. **The invisibility system (`ModVisibility` → `unit_visibility::Off`) is untouched.**

**Tech Stack:** C++17, ASIO, Catch2 (`game_server_unit_tests`), protobuf proto_data, custom game_protocol.

## Global Constraints

- Allman braces, tabs, `m_camelCase` members, `PascalCase` methods, `#pragma once`, copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on new files.
- No exceptions; use `ASSERT`/`DLOG`/`WLOG`/`ELOG` from `base/macros.h`.
- `aura_type` enum values are serialized — append only, before `Count_` (next value: 37).
- `realm_client_packet` opcodes are sequential enum values — append new ones directly before `Count_` (after `MailNotify`).
- Build check: `cmake --build build --config Debug -t game_server_unit_tests` and run `bin/game_server_unit_tests` (or the Debug output dir the solution uses).
- Do not modify `HandleModVisibility`, the `unit_visibility::Off` path, or `GroupInvisibility`.

---

### Task 1: `ModStealth` aura type → `GroupStealth` visibility

**Files:**
- Modify: `src/shared/game/aura.h` (enum, ~line 134)
- Modify: `src/shared/game_server/spells/aura_effect.h` (~line 112, next to `HandleModVisibility`)
- Modify: `src/shared/game_server/spells/aura_effect.cpp` (dispatch map ~line 74; implementation next to `HandleModVisibility` ~line 416)
- Modify: `src/shared/game_server/objects/game_unit_s.cpp` `NotifyVisibilityChanged()` (~line 1437)
- Test: `src/game_server_unit_tests/stealth_visibility_test.cpp` (new)

**Interfaces:**
- Produces: `aura_type::ModStealth = 37`; `GameUnitS::NotifyVisibilityChanged()` sets `unit_visibility::GroupStealth` when a `ModStealth` aura is active and no `ModVisibility` aura is active.

- [ ] **Step 1: Write the failing test** — new file `stealth_visibility_test.cpp`, registered automatically by the test CMake glob (verify `src/game_server_unit_tests/CMakeLists.txt` globs `*.cpp`; if it lists files explicitly, add it).

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/objects/game_player_s.h"
#include "game/aura.h"
#include "base/timer_queue.h"
#include "shared/proto_data/project.h"
#include "asio/io_service.hpp"

#include "catch.hpp"

#include <memory>

using namespace mmo;

namespace
{
	// Same helper pattern as aura_effect_test.cpp
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
}

TEST_CASE("Unit visibility defaults to On", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);
	CHECK(unit->GetVisibility() == unit_visibility::On);
}

TEST_CASE("SetVisibility GroupStealth is reflected by GetVisibility", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;
	auto unit = MakeUnit(project, timers);
	unit->SetVisibility(unit_visibility::GroupStealth);
	CHECK(unit->GetVisibility() == unit_visibility::GroupStealth);
}
```

(The aura → visibility chain itself is covered indirectly; applying a real aura container in a test requires a world instance. The `NotifyVisibilityChanged` change is small enough to verify by review + the CanBeSeenBy tests of Task 2.)

- [ ] **Step 2: Run test to verify it fails** (`GetVisibility` accessor exists — if the test builds and passes already, continue; the real assertions come in Task 2. Expected: compiles, passes.)
- [ ] **Step 3: Implement**

`src/shared/game/aura.h` — append after `ModDodgeChance = 36,` (keep the "Add new aura types HERE" comment below it):

```cpp
			/// Puts the unit into stealth. Visibility to other units is evaluated per
			/// observer (level difference + front cone); party members always see the unit.
			/// This is a separate system from ModVisibility (true invisibility).
			ModStealth            = 37,
```

`aura_effect.h` — after `HandleModVisibility` declaration:

```cpp
		void HandleModStealth(bool apply) const;
```

`aura_effect.cpp` — dispatch map entry after the `ModVisibility` line:

```cpp
			{ AuraType::ModStealth,            [](AuraEffect& self, bool apply){ self.HandleModStealth(apply); } },
```

Implementation (mirror `HandleModVisibility` exactly — post to universe, weak owner):

```cpp
	void AuraEffect::HandleModStealth(bool apply) const
	{
		std::shared_ptr<GameUnitS> owner = std::static_pointer_cast<GameUnitS>(m_container.GetOwner().shared_from_this());
		if (!owner || !owner->GetWorldInstance())
		{
			return;
		}

		std::weak_ptr weakOwner = owner;
		owner->GetWorldInstance()->GetUniverse().Post([weakOwner]()
		{
			if (const auto owner = weakOwner.lock())
			{
				owner->NotifyVisibilityChanged();
			}
		});
	}
```

`game_unit_s.cpp` `NotifyVisibilityChanged()` — replace body with priority chain (invisibility wins; do not alter Off semantics):

```cpp
	void GameUnitS::NotifyVisibilityChanged()
	{
		// Invisibility (ModVisibility) has priority over everything else.
		if (HasAuraEffect(aura_type::ModVisibility))
		{
			SetVisibility(unit_visibility::Off);
			return;
		}

		// Stealth: visibility is evaluated per observer in CanBeSeenBy.
		if (HasAuraEffect(aura_type::ModStealth))
		{
			SetVisibility(unit_visibility::GroupStealth);
			return;
		}

		SetVisibility(unit_visibility::On);
	}
```

- [ ] **Step 4: Build + run tests** — `cmake --build build --config Debug -t game_server_unit_tests`, run binary, expect PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(stealth): add ModStealth aura type mapping to GroupStealth visibility"`

---

### Task 2: Per-observer stealth detection in `CanBeSeenBy`

**Files:**
- Modify: `src/shared/game_server/objects/game_unit_s.h` (stealth constants near `unit_visibility` namespace, ~line 87; new method declaration near `CanBeSeenBy` ~line 663)
- Modify: `src/shared/game_server/objects/game_unit_s.cpp` (`CanBeSeenBy` ~line 383)
- Test: `src/game_server_unit_tests/stealth_visibility_test.cpp`

**Interfaces:**
- Consumes: `GameObjectS::IsFacingTowards(const GameObjectS&)` (120° arc, `game_object_s.cpp:207`), `GamePlayerS::GetGroupId()`, `GameObjectS::GetSquaredDistanceTo(pos, use3D)`.
- Produces:
  - `namespace stealth` constants in `game_unit_s.h`: `BaseDetectionRange = 10.0f`, `RangePerLevelAbove = 1.0f`, `RangePerLevelBelow = 1.5f`, `MinDetectionRange = 1.5f`, `MaxDetectionRange = 25.0f`.
  - `bool GameUnitS::CanDetectStealthedUnit(const GameUnitS& stealthed) const` — cone + level range check (observer = `this`).
  - `CanBeSeenBy` handles `unit_visibility::GroupStealth`.

- [ ] **Step 1: Write failing tests** (append to `stealth_visibility_test.cpp`). Position/facing via `MovementInfo` + `ApplyMovementInfo`:

```cpp
namespace
{
	void PlaceUnit(GameUnitS& unit, const Vector3& position, float facingRadians)
	{
		MovementInfo info = unit.GetMovementInfo();
		info.position = position;
		info.facing = Radian(facingRadians);
		unit.ApplyMovementInfo(info);
	}
}

TEST_CASE("Stealthed unit cannot be seen from behind even at melee range", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto stealthed = MakeUnit(project, timers, 10);
	auto observer = MakeUnit(project, timers, 10);

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	// Observer at origin facing -X; stealthed unit 2m in +X direction (behind observer).
	// Forward vector = (cos(yaw), 0, -sin(yaw)) — yaw = pi -> forward = (-1, 0, 0).
	PlaceUnit(*observer, Vector3(0.0f, 0.0f, 0.0f), 3.14159265f);
	PlaceUnit(*stealthed, Vector3(2.0f, 0.0f, 0.0f), 0.0f);

	CHECK(!stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Stealthed unit is seen inside the front cone within detection range", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto stealthed = MakeUnit(project, timers, 10);
	auto observer = MakeUnit(project, timers, 10);

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	// Observer at origin facing +X (yaw 0 -> forward (1,0,0)); stealthed 5m ahead.
	PlaceUnit(*observer, Vector3(0.0f, 0.0f, 0.0f), 0.0f);
	PlaceUnit(*stealthed, Vector3(5.0f, 0.0f, 0.0f), 0.0f);

	CHECK(stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Stealthed unit outside detection range is not seen even in front cone", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto stealthed = MakeUnit(project, timers, 10);
	auto observer = MakeUnit(project, timers, 10);

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	// Same level: detection range == stealth::BaseDetectionRange (10m). Place at 15m.
	PlaceUnit(*observer, Vector3(0.0f, 0.0f, 0.0f), 0.0f);
	PlaceUnit(*stealthed, Vector3(15.0f, 0.0f, 0.0f), 0.0f);

	CHECK(!stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Higher level observer detects stealth from further away", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto stealthed = MakeUnit(project, timers, 5);
	auto observer = MakeUnit(project, timers, 10);   // +5 levels -> 10 + 5*1 = 15m range

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	PlaceUnit(*observer, Vector3(0.0f, 0.0f, 0.0f), 0.0f);
	PlaceUnit(*stealthed, Vector3(14.0f, 0.0f, 0.0f), 0.0f);

	CHECK(stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Lower level observer has reduced stealth detection range", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto stealthed = MakeUnit(project, timers, 10);
	auto observer = MakeUnit(project, timers, 5);    // -5 levels -> 10 - 5*1.5 = 2.5m range

	stealthed->SetVisibility(unit_visibility::GroupStealth);

	PlaceUnit(*observer, Vector3(0.0f, 0.0f, 0.0f), 0.0f);
	PlaceUnit(*stealthed, Vector3(5.0f, 0.0f, 0.0f), 0.0f);

	CHECK(!stealthed->CanBeSeenBy(*observer));

	PlaceUnit(*stealthed, Vector3(2.0f, 0.0f, 0.0f), 0.0f);
	CHECK(stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Party members always see stealthed group mates regardless of cone and range", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto stealthed = MakeUnit(project, timers, 10);
	auto observer = MakeUnit(project, timers, 10);

	stealthed->SetGroupId(42);
	observer->SetGroupId(42);
	stealthed->SetVisibility(unit_visibility::GroupStealth);

	// Behind the observer and far away — still visible to the party member.
	PlaceUnit(*observer, Vector3(0.0f, 0.0f, 0.0f), 3.14159265f);
	PlaceUnit(*stealthed, Vector3(30.0f, 0.0f, 0.0f), 0.0f);

	CHECK(stealthed->CanBeSeenBy(*observer));
}

TEST_CASE("Invisibility (Off) still hides from everyone except GMs", "[stealth]")
{
	asio::io_service io;
	TimerQueue timers{ io };
	proto::Project project;

	auto invisible = MakeUnit(project, timers, 10);
	auto observer = MakeUnit(project, timers, 60);

	invisible->SetVisibility(unit_visibility::Off);
	PlaceUnit(*observer, Vector3(0.0f, 0.0f, 0.0f), 0.0f);
	PlaceUnit(*invisible, Vector3(1.0f, 0.0f, 0.0f), 0.0f);

	CHECK(!invisible->CanBeSeenBy(*observer));

	observer->SetIsGameMaster(true);
	CHECK(invisible->CanBeSeenBy(*observer));
}
```

Note: if `GamePlayerS::SetGroupId` doesn't exist, check `game_player_s.h:174` for the setter used by the world server and use that (it exists — group id is assigned on group join; if the setter is named differently, adapt the test, not the production naming).

- [ ] **Step 2: Run to verify failure** — expected: `CanBeSeenBy` returns GM-only for GroupStealth, so front-cone/party tests FAIL.
- [ ] **Step 3: Implement**

`game_unit_s.h` — below the `unit_visibility` namespace:

```cpp
	/// Tuning constants for stealth detection (unit_visibility::GroupStealth).
	namespace stealth
	{
		/// Detection distance in meters when observer and stealthed unit have equal level.
		static constexpr float BaseDetectionRange = 10.0f;
		/// Added per level the observer is above the stealthed unit.
		static constexpr float RangePerLevelAbove = 1.0f;
		/// Subtracted per level the observer is below the stealthed unit.
		static constexpr float RangePerLevelBelow = 1.5f;
		static constexpr float MinDetectionRange = 1.5f;
		static constexpr float MaxDetectionRange = 25.0f;
	}
```

Declaration next to `CanBeSeenBy`:

```cpp
		/// Determines whether this unit is able to detect the given stealthed unit right now,
		/// based on the front cone and the level difference. Does not include group or GM checks.
		bool CanDetectStealthedUnit(const GameUnitS& stealthed) const;
```

`game_unit_s.cpp`:

```cpp
	bool GameUnitS::CanDetectStealthedUnit(const GameUnitS& stealthed) const
	{
		// Dead units don't detect anything
		if (!IsAlive())
		{
			return false;
		}

		// A stealthed unit outside of the observer's front cone can never be seen
		if (!IsFacingTowards(stealthed))
		{
			return false;
		}

		const int32 observerLevel = Get<int32>(object_fields::Level);
		const int32 stealthLevel = stealthed.Get<int32>(object_fields::Level);

		float detectionRange = stealth::BaseDetectionRange;
		if (observerLevel > stealthLevel)
		{
			detectionRange += static_cast<float>(observerLevel - stealthLevel) * stealth::RangePerLevelAbove;
		}
		else if (observerLevel < stealthLevel)
		{
			detectionRange -= static_cast<float>(stealthLevel - observerLevel) * stealth::RangePerLevelBelow;
		}

		detectionRange = Clamp<float>(detectionRange, stealth::MinDetectionRange, stealth::MaxDetectionRange);

		return GetSquaredDistanceTo(stealthed.GetPosition(), true) <= detectionRange * detectionRange;
	}
```

`CanBeSeenBy` — add the `GroupStealth` case (leave `On` and `default` exactly as they are):

```cpp
		switch (m_visibility)
		{
		case unit_visibility::On:
			return true;

		case unit_visibility::GroupStealth:
			{
				if (other.IsGameMaster())
				{
					return true;
				}

				// Party members can always see stealthed group mates
				const auto* stealthedPlayer = dynamic_cast<const GamePlayerS*>(this);
				const auto* observingPlayer = dynamic_cast<const GamePlayerS*>(&other);
				if (stealthedPlayer && observingPlayer &&
					stealthedPlayer->GetGroupId() != 0 &&
					stealthedPlayer->GetGroupId() == observingPlayer->GetGroupId())
				{
					return true;
				}

				return other.CanDetectStealthedUnit(*this);
			}

		// TODO: Handle other values here except the default
		default:
			return other.IsGameMaster();
		}
```

Add `#include "game_server/objects/game_player_s.h"` to `game_unit_s.cpp` if not already present (check — GamePlayerS is referenced elsewhere in that file, likely already included).

- [ ] **Step 4: Build + run tests** — expect all `[stealth]` tests PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(stealth): per-observer stealth detection (front cone + level difference, party always sees)"`

---

### Task 3: Protocol + per-client visibility plumbing (GUID lists)

**Files:**
- Modify: `src/shared/game_protocol/game_protocol.h` (append opcodes before `Count_`, ~line 662)
- Modify: `src/shared/game_server/world/tile_subscriber.h`
- Modify: `src/world_server/player.h` (declarations + `m_hiddenGuids` member near `m_spawnedGuids`, ~line 857)
- Modify: `src/world_server/player.cpp` (`NotifyObjectsDespawned` ~line 568; new methods)
- Modify: `src/shared/game_server/objects/game_unit_s.h/.cpp` (`UpdateVisibilityAndView` ~line 3607, `SetVisibility` ~line 3592)
- Modify: `src/shared/game_server/world/world_instance.h/.cpp` (stealth registry + periodic refresh in `Update` ~line 254; filters at lines ~425, ~619, ~701)
- Modify: `src/world_server/player.cpp` `OnMovement` relay loop (~line 2179)

**Interfaces:**
- Produces:
  - Opcodes `game::realm_client_packet::UnitVisibilityList` and `game::realm_client_packet::StealthDetected` (appended after `MailNotify`).
  - `UnitVisibilityList` payload: `uint16 visibleCount`, packed guids…, `uint16 invisibleCount`, packed guids…
  - `StealthDetected` payload: `packed uint64 detectorGuid`.
  - `TileSubscriber::NotifyUnitVisibilityChanged(GameUnitS& unit, bool visible)` (virtual, default no-op) and `TileSubscriber::IsObjectHiddenForClient(uint64 guid) const` (virtual, default `false`).
  - `GameUnitS::UpdateVisibilityAndView()` (parameterless — per-subscriber delegation).
  - `WorldInstance::NotifyStealthStateChanged(GameUnitS& unit, bool stealthed)`.

- [ ] **Step 1: Opcodes** — in `game_protocol.h`, after `MailNotify` and before `Count_`:

```cpp
				/// Sent to the client when units it already knows change stealth visibility, so the
				/// client can hide/show them without despawning. Payload: uint16 visibleCount,
				/// packed guid[visibleCount], uint16 invisibleCount, packed guid[invisibleCount].
				UnitVisibilityList,

				/// Sent to a stealthed client when a hostile creature has spotted it and entered
				/// its alert state. Payload: packed uint64 detectorGuid.
				StealthDetected,
```

- [ ] **Step 2: TileSubscriber additions** — in `tile_subscriber.h` after `IsObjectKnown`:

```cpp
		/// Called when the per-observer visibility of a unit may have changed (e.g. stealth).
		/// The subscriber decides based on its own client state whether to spawn the unit,
		/// hide/show it via a UnitVisibilityList packet, or do nothing.
		virtual void NotifyUnitVisibilityChanged(GameUnitS& unit, bool visible) {}

		/// Returns true if the given object is known to the client but currently hidden
		/// from it (stealth). Hidden objects must not receive movement or field updates.
		virtual bool IsObjectHiddenForClient(uint64 guid) const { return false; }
```

- [ ] **Step 3: Player implementation** — `player.h`: declare overrides + member:

```cpp
		void NotifyUnitVisibilityChanged(GameUnitS& unit, bool visible) override;

		bool IsObjectHiddenForClient(uint64 guid) const override { return m_hiddenGuids.contains(guid); }
```

member next to `m_spawnedGuids`:

```cpp
		/// GUIDs of units the client knows (spawned) but which are currently hidden from it
		/// due to stealth. Kept client-side in memory; toggled via UnitVisibilityList packets.
		std::unordered_set<uint64> m_hiddenGuids;
```

`player.cpp`:

```cpp
	void Player::NotifyUnitVisibilityChanged(GameUnitS& unit, const bool visible)
	{
		const uint64 guid = unit.GetGuid();

		if (!IsObjectKnown(guid))
		{
			// The client doesn't know this unit yet: a unit that just became visible is
			// spawned normally, an unknown invisible unit stays unknown.
			if (visible)
			{
				const std::vector<GameObjectS*> objects{ &unit };
				NotifyObjectsSpawned(objects);
			}
			return;
		}

		if (visible)
		{
			if (m_hiddenGuids.erase(guid) > 0)
			{
				SendPacket([guid](game::OutgoingPacket& outPacket)
				{
					outPacket.Start(game::realm_client_packet::UnitVisibilityList);
					outPacket
						<< io::write<uint16>(1)
						<< io::write_packed_guid(guid)
						<< io::write<uint16>(0);
					outPacket.Finish();
				});

				// Resync movement so the client snaps the unit to its real position.
				unit.GetMover().SendMovementPackets(*this);
			}
			return;
		}

		// Unit became invisible for this client
		if (unit.GetVisibility() == unit_visibility::GroupStealth)
		{
			// Stealth: keep the unit in client memory, just hide it.
			if (m_hiddenGuids.insert(guid).second)
			{
				SendPacket([guid](game::OutgoingPacket& outPacket)
				{
					outPacket.Start(game::realm_client_packet::UnitVisibilityList);
					outPacket
						<< io::write<uint16>(0)
						<< io::write<uint16>(1)
						<< io::write_packed_guid(guid);
					outPacket.Finish();
				});
			}
		}
		else
		{
			// True invisibility keeps its original despawn semantics.
			const std::vector<GameObjectS*> objects{ &unit };
			NotifyObjectsDespawned(objects);
		}
	}
```

In `NotifyObjectsDespawned` (~line 572), alongside `m_spawnedGuids.erase(...)` add `m_hiddenGuids.erase(object->GetGuid());`.

- [ ] **Step 4: Refactor `GameUnitS::UpdateVisibilityAndView`** to parameterless delegation and update `SetVisibility`:

```cpp
	void GameUnitS::SetVisibility(UnitVisibility x)
	{
		if (m_visibility == x)
		{
			return; // No change
		}

		const bool wasStealth = (m_visibility == unit_visibility::GroupStealth);
		m_visibility = x;

		if (m_worldInstance)
		{
			const bool isStealth = (m_visibility == unit_visibility::GroupStealth);
			if (wasStealth != isStealth)
			{
				m_worldInstance->NotifyStealthStateChanged(*this, isStealth);
			}

			UpdateVisibilityAndView();
		}
	}

	void GameUnitS::UpdateVisibilityAndView()
	{
		auto* worldInstance = GetWorldInstance();
		if (!worldInstance)
		{
			return;
		}

		// Each subscriber tracks what its client actually knows (spawned / hidden), so the
		// edge detection lives there — this method just reports the current answer.
		ForEachSubscriberInSight([this](TileSubscriber& subscriber)
		{
			if (&subscriber.GetGameUnit() == this)
			{
				return;
			}

			subscriber.NotifyUnitVisibilityChanged(*this, CanBeSeenBy(subscriber.GetGameUnit()));
		});
	}
```

Update the header declaration (`UpdateVisibilityAndView(UnitVisibility prevVisibility)` → `UpdateVisibilityAndView()`) and any other callers (grep `UpdateVisibilityAndView`).

**Behavior check (invisibility unchanged):** On→Off: subscriber known & !visible & visibility==Off → `NotifyObjectsDespawned` (same as before). Off→On: unknown & visible → spawn (same as before). GM observers: `CanBeSeenBy` returns true → no change.

- [ ] **Step 5: WorldInstance stealth registry + periodic refresh** — `world_instance.h`: add member + methods:

```cpp
		/// Units currently in GroupStealth visibility whose per-observer visibility has to be
		/// re-evaluated periodically (observers and the stealthed unit move around).
		std::set<GameUnitS*> m_stealthedUnits;
		GameTime m_nextStealthRefresh = 0;

	public:
		void NotifyStealthStateChanged(GameUnitS& unit, bool stealthed);
```

`world_instance.cpp`:

```cpp
	void WorldInstance::NotifyStealthStateChanged(GameUnitS& unit, const bool stealthed)
	{
		if (stealthed)
		{
			m_stealthedUnits.insert(&unit);
		}
		else
		{
			m_stealthedUnits.erase(&unit);
		}
	}
```

In `RemoveGameObject`, add (where the unit is removed from the unit finder): `if (remove.IsUnit()) { m_stealthedUnits.erase(&remove.AsUnit()); }`

In `Update(const RegularUpdate& update)` before `m_updating = false;`:

```cpp
		// Periodically re-evaluate per-observer visibility of stealthed units, since it
		// depends on observer position and facing which change with movement.
		constexpr GameTime StealthRefreshInterval = 300;
		if (update.GetTimestamp() >= m_nextStealthRefresh)
		{
			for (auto* stealthedUnit : std::vector(m_stealthedUnits.begin(), m_stealthedUnits.end()))
			{
				stealthedUnit->UpdateVisibilityAndView();
			}
			m_nextStealthRefresh = update.GetTimestamp() + StealthRefreshInterval;
		}
```

- [ ] **Step 6: Update/movement/despawn filters**

`world_instance.cpp` `UpdateObject` (~line 619) — extend the unit filter so hidden-but-known clients get no field updates:

```cpp
				if (object.IsUnit() &&
					(!object.AsUnit().CanBeSeenBy(character) || subscriber.IsObjectHiddenForClient(object.GetGuid())))
				{
					return;
				}
```

`world_instance.cpp` `RemoveGameObject` (~line 425) and the `OnObjectMoved` despawn branch — replace the `CanBeSeenBy` filter with the known check so hidden-but-known units still get destroyed at the client when leaving sight:

```cpp
								if (!subscriber->IsObjectKnown(remove.GetGuid()))
								{
									continue;
								}
```

(In `OnObjectMoved`'s despawn loop the `IsObjectKnown` check already exists — just delete the additional `CanBeSeenBy` continue there.)

`player.cpp` `OnMovement` relay loop (~line 2179) — after the time-sync check add:

```cpp
					// Never leak movement of units the watcher's client currently can't see
					if (watcher->IsObjectHiddenForClient(characterGuid))
					{
						continue;
					}
```

- [ ] **Step 7: Build** world_server + unit tests, run tests. Expected: PASS (no behavioral tests for packets here; verified in Task 6/manual).
- [ ] **Step 8: Commit** — `git commit -m "feat(stealth): UnitVisibilityList protocol, per-client hidden tracking, periodic stealth visibility refresh"`

---

### Task 4: Creature alert AI state

**Files:**
- Create: `src/shared/game_server/ai/creature_ai_alert_state.h`
- Create: `src/shared/game_server/ai/creature_ai_alert_state.cpp`
- Modify: `src/shared/game_server/ai/creature_ai.h/.cpp` (add `EnterAlert`)
- Modify: `src/shared/game_server/ai/creature_ai_idle_state.h/.cpp` (route stealth detections; periodic scan)
- Modify: `src/shared/game_server/objects/game_unit_s.h` (`NetUnitWatcherS::OnStealthDetected`)
- Modify: `src/world_server/player.h/.cpp` (implement `OnStealthDetected` → send packet)

**Interfaces:**
- Consumes: `Countdown` (`base/countdown.h`), `UnitMover::StopMovement()`, `GameObjectS::SetFacing/GetAngle`, `CreatureAI::SetState/EnterCombat/Idle`, `GameUnitS::CanBeSeenBy`, `WorldInstance::GetUnitFinder().FindUnits(Circle, handler)`.
- Produces: `CreatureAIAlertState`, `CreatureAI::EnterAlert(GameUnitS& target)`, `NetUnitWatcherS::OnStealthDetected(uint64 detectorGuid)` (virtual, default no-op to avoid breaking other implementers).

- [ ] **Step 1: `creature_ai_alert_state.h`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "creature_ai_state.h"
#include "base/countdown.h"

#include <memory>

namespace mmo
{
	/// AI state entered when a creature detects a stealthed hostile unit. The creature stops
	/// moving and turns toward the unit. If the unit is still visible after the alert time,
	/// or comes closer while visible, or drops stealth, combat is engaged. Otherwise the
	/// creature returns to its idle state and resumes movement.
	class CreatureAIAlertState final : public CreatureAIState
	{
	public:
		/// Total time the creature stays alerted before deciding (milliseconds).
		static constexpr GameTime AlertDuration = 3000;
		/// Interval between visibility re-checks while alerted (milliseconds).
		static constexpr GameTime AlertCheckInterval = 400;
		/// How much closer (meters) the target has to come to trigger combat before expiry.
		static constexpr float EngageDistanceDelta = 1.0f;

	public:
		explicit CreatureAIAlertState(CreatureAI& ai, GameUnitS& target);
		~CreatureAIAlertState() override;

	public:
		void OnEnter() override;
		void OnLeave() override;
		void OnDamage(GameUnitS& attacker) override;

	private:
		/// Runs one visibility re-check; engages, keeps waiting or gives up.
		void OnCheckTimer();

		std::weak_ptr<GameUnitS> m_target;
		Countdown m_checkCountdown;
		GameTime m_alertEnd = 0;
		float m_spotDistanceSq = 0.0f;
	};
}
```

- [ ] **Step 2: `creature_ai_alert_state.cpp`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_ai_alert_state.h"
#include "creature_ai.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/universe.h"

namespace mmo
{
	CreatureAIAlertState::CreatureAIAlertState(CreatureAI& ai, GameUnitS& target)
		: CreatureAIState(ai)
		, m_target(std::static_pointer_cast<GameUnitS>(target.shared_from_this()))
		, m_checkCountdown(ai.GetControlled().GetTimers())
	{
	}

	CreatureAIAlertState::~CreatureAIAlertState() = default;

	void CreatureAIAlertState::OnEnter()
	{
		CreatureAIState::OnEnter();

		auto& controlled = GetControlled();

		const auto target = m_target.lock();
		if (!target)
		{
			GetAI().Idle();
			return;
		}

		// Stop and face the spot where the stealthed unit was noticed
		controlled.GetMover().StopMovement();
		controlled.SetFacing(controlled.GetAngle(*target));

		m_spotDistanceSq = controlled.GetSquaredDistanceTo(target->GetPosition(), true);
		m_alertEnd = GetAsyncTimeMs() + AlertDuration;

		// Let the spotted player know it has been noticed (client plays an alert sound)
		if (auto* watcher = target->GetNetUnitWatcher())
		{
			watcher->OnStealthDetected(controlled.GetGuid());
		}

		m_connections += m_checkCountdown.ended.connect(*this, &CreatureAIAlertState::OnCheckTimer);
		m_checkCountdown.SetEnd(GetAsyncTimeMs() + AlertCheckInterval);
	}

	void CreatureAIAlertState::OnLeave()
	{
		m_checkCountdown.Cancel();
		m_connections.disconnect();
		CreatureAIState::OnLeave();
	}

	void CreatureAIAlertState::OnDamage(GameUnitS& attacker)
	{
		auto strongAttacker = std::static_pointer_cast<GameUnitS>(attacker.shared_from_this());
		auto strongThis = shared_from_this();
		GetControlled().GetWorldInstance()->GetUniverse().Post([strongAttacker, strongThis]()
		{
			strongThis->GetAI().EnterCombat(*strongAttacker);
		});
	}

	void CreatureAIAlertState::OnCheckTimer()
	{
		auto& controlled = GetControlled();

		const auto target = m_target.lock();
		if (!target || !target->IsAlive() || !controlled.IsAlive())
		{
			GetAI().Idle();
			return;
		}

		// Target dropped stealth right in front of us: engage immediately
		if (target->GetVisibility() != unit_visibility::GroupStealth && controlled.UnitIsEnemy(*target))
		{
			GetAI().EnterCombat(*target);
			return;
		}

		const bool visible = target->CanBeSeenBy(controlled);
		const float distanceSq = controlled.GetSquaredDistanceTo(target->GetPosition(), true);

		if (visible)
		{
			// Came closer while being watched: engage before the timer runs out
			const float engageDistance = ::sqrtf(m_spotDistanceSq) - EngageDistanceDelta;
			if (engageDistance > 0.0f && distanceSq <= engageDistance * engageDistance)
			{
				GetAI().EnterCombat(*target);
				return;
			}

			// Keep facing the unit while it sneaks around in view
			controlled.SetFacing(controlled.GetAngle(*target));
		}

		if (GetAsyncTimeMs() >= m_alertEnd)
		{
			if (visible)
			{
				GetAI().EnterCombat(*target);
			}
			else
			{
				GetAI().Idle();
			}
			return;
		}

		m_checkCountdown.SetEnd(GetAsyncTimeMs() + AlertCheckInterval);
	}
}
```

Check `GameUnitS` for the net watcher accessor name (`GetNetUnitWatcher()` — grep `NetUnitWatcherS* ` in `game_unit_s.h`; if it's `m_netUnitWatcher` with a differently named getter, use that).

- [ ] **Step 3: `CreatureAI::EnterAlert`** — `creature_ai.h` (next to `EnterCombat`): `void EnterAlert(GameUnitS& target);` — `creature_ai.cpp`:

```cpp
	void CreatureAI::EnterAlert(GameUnitS& target)
	{
		auto state = std::make_shared<CreatureAIAlertState>(*this, target);
		SetState(std::move(state));
	}
```

with `#include "creature_ai_alert_state.h"`.

- [ ] **Step 4: Idle state integration** — in the watcher lambda of `CreatureAIIdleState::OnEnter` (`creature_ai_idle_state.cpp:61`), inside the `if (controlled.UnitIsEnemy(unit))` block, **before** the level-based aggro-range check, add:

```cpp
					// Stealthed enemies use the stealth detection rules (CanBeSeenBy passed
					// above) and route into the alert state instead of instant combat.
					if (unit.GetVisibility() == unit_visibility::GroupStealth)
					{
						if (MapData* mapData = GetAI().GetControlled().GetWorldInstance()->GetMapData())
						{
							if (!mapData->IsInLineOfSight(controlled.GetPosition(), unit.GetPosition()))
							{
								return true;
							}
						}

						auto strongUnit = std::static_pointer_cast<GameUnitS>(unit.shared_from_this());
						auto strongThis = shared_from_this();
						GetAI().GetControlled().GetWorldInstance()->GetUniverse().Post([strongUnit, strongThis]()
							{
								strongThis->GetAI().EnterAlert(*strongUnit);
							});
						return true;
					}
```

Additionally add a periodic stealth scan (the unit watcher only fires on unit movement — a creature walking past a *stationary* stealthed unit would never trigger it). In `creature_ai_idle_state.h` add members:

```cpp
		Countdown m_stealthScanCountdown;

		void OnStealthScan();
```

Initialize in the constructor init list: `m_stealthScanCountdown(ai.GetControlled().GetTimers())`. In `OnEnter` (after the watcher start):

```cpp
		m_connections += m_stealthScanCountdown.ended.connect(*this, &CreatureAIIdleState::OnStealthScan);
		m_stealthScanCountdown.SetEnd(GetAsyncTimeMs() + 500);
```

In `OnLeave`: `m_stealthScanCountdown.Cancel();` (before `m_connections.disconnect()`).

Implementation:

```cpp
	void CreatureAIIdleState::OnStealthScan()
	{
		auto& controlled = GetControlled();

		if (controlled.IsAlive() && controlled.GetWorldInstance())
		{
			const auto& location = controlled.GetPosition();

			controlled.GetWorldInstance()->GetUnitFinder().FindUnits(
				Circle(location.x, location.z, stealth::MaxDetectionRange),
				[this, &controlled](GameUnitS& unit) -> bool
				{
					if (&unit == &controlled || !unit.IsAlive())
					{
						return true;
					}

					if (unit.GetVisibility() != unit_visibility::GroupStealth)
					{
						return true;
					}

					if (!controlled.UnitIsEnemy(unit) || !unit.CanBeSeenBy(controlled))
					{
						return true;
					}

					if (MapData* mapData = controlled.GetWorldInstance()->GetMapData())
					{
						if (!mapData->IsInLineOfSight(controlled.GetPosition(), unit.GetPosition()))
						{
							return true;
						}
					}

					auto strongUnit = std::static_pointer_cast<GameUnitS>(unit.shared_from_this());
					auto strongThis = shared_from_this();
					controlled.GetWorldInstance()->GetUniverse().Post([strongUnit, strongThis]()
						{
							strongThis->GetAI().EnterAlert(*strongUnit);
						});
					return false;
				});
		}

		m_stealthScanCountdown.SetEnd(GetAsyncTimeMs() + 500);
	}
```

(Check the exact `FindUnits` handler signature in `tiled_unit_finder.h:12-52` — it is `std::function<bool(GameUnitS&)>`; returning false stops iteration.)

- [ ] **Step 5: `NetUnitWatcherS::OnStealthDetected`** — in `game_unit_s.h` inside `NetUnitWatcherS` (after `OnProficiencyChanged`):

```cpp
		/// Called when a hostile creature has spotted this (stealthed) unit and entered its
		/// alert state, so the client can play a warning sound.
		virtual void OnStealthDetected(uint64 detectorGuid) {}
```

world_server `player.h`: `void OnStealthDetected(uint64 detectorGuid) override;` — `player.cpp`:

```cpp
	void Player::OnStealthDetected(const uint64 detectorGuid)
	{
		SendPacket([detectorGuid](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::StealthDetected);
			outPacket << io::write_packed_guid(detectorGuid);
			outPacket.Finish();
		});
	}
```

- [ ] **Step 6: Build** world_server + game_server lib + unit tests; run unit tests. Expected: PASS.
- [ ] **Step 7: Commit** — `git commit -m "feat(stealth): creature alert AI state with 3s detection window and StealthDetected notification"`

---

### Task 5: Per-creature alert sound data (proto → realm query → client cache → editor)

**Files:**
- Modify: `src/shared/proto_data/units.proto` (`UnitEntry`, next field id 86)
- Modify: `src/realm_server/player.cpp` (CreatureQueryResult writer, ~line 3578)
- Modify: `src/shared/game/creature_data.h/.cpp` (`CreatureInfo` struct + serialization)
- Modify: `src/mmo_client/game_states/world_state.cpp` `OnCreatureQueryResult` (~line 2274)
- Modify: `src/mmo_edit/editor_windows/creature_editor_window.cpp` (~line 477, next to Greeting Text)

**Interfaces:**
- Produces: `UnitEntry.stealth_alert_sound` (optional string, field 86); `CreatureInfo::stealthAlertSound`; CreatureQueryResult payload gains one trailing null-terminated string.

- [ ] **Step 1: proto** — in `units.proto` `UnitEntry`, after `repeated uint32 unitlootentries = 85;`:

```proto
	// Sound file played at the client when this creature detects a stealthed player
	// (e.g. "Sound/Creature/Wolf/WolfAlert.wav"). Empty = default alert sound.
	optional string stealth_alert_sound = 86;
```

- [ ] **Step 2: realm writer** — in the CreatureQueryResult success packet add after the subname write:

```cpp
					<< io::write_range(unit->stealth_alert_sound()) << io::write<uint8>(0)
```

- [ ] **Step 3: `CreatureInfo`** — add `String stealthAlertSound;` after `subname` in `creature_data.h`; extend both operators in `creature_data.cpp` (`io::write_dynamic_range<uint8>(info.stealthAlertSound)` / `io::read_container<uint8>(outInfo.stealthAlertSound)`).
- [ ] **Step 4: client reader** — in `OnCreatureQueryResult` extend the read to `>> io::read_string(entry.name) >> io::read_string(entry.subname) >> io::read_string(entry.stealthAlertSound)`.
- [ ] **Step 5: editor field** — in `creature_editor_window.cpp` next to the Greeting Text input:

```cpp
			ImGui::InputText("Stealth Alert Sound", currentEntry.mutable_stealth_alert_sound());
```

(Match the exact ImGui helper overload used for other single-line string fields in that file — check how e.g. `subname` is edited and copy that call style.)

- [ ] **Step 6: Build** realm_server, mmo_client, mmo_edit (if editor configured). Expected: compiles.
- [ ] **Step 7: Commit** — `git commit -m "feat(stealth): per-creature stealth alert sound in unit data and creature query"`

---### Task 6: Client — visibility list handling + alert sound

**Files:**
- Modify: `src/shared/game_client/game_unit_c.h/.cpp` (`SetStealthHidden`)
- Modify: `src/mmo_client/game_states/world_state.h` (handler declarations ~line 306)
- Modify: `src/mmo_client/game_states/world_state.cpp` (registration ~line 1377; handlers near `OnDestroyObjects` ~line 2040)

**Interfaces:**
- Consumes: `ObjectMgr::Get<GameUnitC>(guid)`, `SceneNode::SetVisible(bool, bool cascade)`, `IAudio::CreateSound/PlaySound/Set3DPosition`, `DBCreatureCache::Get(entry, callback)`, opcodes from Task 3.
- Produces: `GameUnitC::SetStealthHidden(bool)` / `IsStealthHidden()`.

- [ ] **Step 1: `GameUnitC::SetStealthHidden`** — `game_unit_c.h`:

```cpp
		/// Hides or shows the unit's visuals without destroying the object. Used for units
		/// that are stealthed and currently undetected: the object stays in memory so the
		/// server doesn't have to resend spawn packets when visibility flips.
		void SetStealthHidden(bool hidden);

		[[nodiscard]] bool IsStealthHidden() const noexcept { return m_stealthHidden; }
```

member: `bool m_stealthHidden = false;` — `game_unit_c.cpp`:

```cpp
	void GameUnitC::SetStealthHidden(const bool hidden)
	{
		if (m_stealthHidden == hidden)
		{
			return;
		}

		m_stealthHidden = hidden;

		if (m_sceneNode)
		{
			m_sceneNode->SetVisible(!hidden, true);
		}
	}
```

(Verify the root node member name in `game_object_c.h:207` — `m_sceneNode`. Cascade hides child nodes: name plate, quest giver icon, attachments. If unit name components hang off a *different* root, hide that too — check `m_nameComponentNode` parentage in `game_unit_c.cpp` and hide it explicitly if needed.)

- [ ] **Step 2: handlers** — `world_state.h`:

```cpp
		PacketParseResult OnUnitVisibilityList(game::IncomingPacket &packet);

		PacketParseResult OnStealthDetected(game::IncomingPacket &packet);
```

Registration in `world_state.cpp` next to the CreatureQueryResult registration:

```cpp
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::UnitVisibilityList, *this, &WorldState::OnUnitVisibilityList);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::StealthDetected, *this, &WorldState::OnStealthDetected);
```

Implementations:

```cpp
	PacketParseResult WorldState::OnUnitVisibilityList(game::IncomingPacket &packet)
	{
		uint16 visibleCount;
		if (!(packet >> io::read<uint16>(visibleCount)))
		{
			return PacketParseResult::Disconnect;
		}

		for (uint16 i = 0; i < visibleCount; ++i)
		{
			uint64 guid;
			if (!(packet >> io::read_packed_guid(guid)))
			{
				return PacketParseResult::Disconnect;
			}

			if (const auto unit = ObjectMgr::Get<GameUnitC>(guid))
			{
				unit->SetStealthHidden(false);
			}
			else
			{
				WLOG("UnitVisibilityList: unknown unit " << log_hex_digit(guid) << " marked visible");
			}
		}

		uint16 invisibleCount;
		if (!(packet >> io::read<uint16>(invisibleCount)))
		{
			return PacketParseResult::Disconnect;
		}

		for (uint16 i = 0; i < invisibleCount; ++i)
		{
			uint64 guid;
			if (!(packet >> io::read_packed_guid(guid)))
			{
				return PacketParseResult::Disconnect;
			}

			if (const auto unit = ObjectMgr::Get<GameUnitC>(guid))
			{
				// Deselect if this was our target
				if (m_selectedObjectGuid == guid) { /* use the same unselect path as OnDestroyObjects (~line 2056) */ }
				unit->SetStealthHidden(true);
			}
			else
			{
				WLOG("UnitVisibilityList: unknown unit " << log_hex_digit(guid) << " marked invisible");
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnStealthDetected(game::IncomingPacket &packet)
	{
		uint64 detectorGuid;
		if (!(packet >> io::read_packed_guid(detectorGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		const auto detector = ObjectMgr::Get<GameUnitC>(detectorGuid);

		// Default alert sound; replaced by the creature-specific one if set in the data.
		String soundFile = "Sound/Creature/StealthAlert.wav";

		if (detector)
		{
			const uint64 entryId = detector->Get<uint32>(object_fields::Entry);
			if (const CreatureInfo* info = m_cache.GetCreatureCache().Get(entryId, [](uint64, const CreatureInfo&) {}))
			{
				if (!info->stealthAlertSound.empty())
				{
					soundFile = info->stealthAlertSound;
				}
			}
		}

		if (const SoundIndex sound = m_audio.CreateSound(soundFile.c_str(), SoundType::Sound3D); sound != InvalidSound)
		{
			ChannelIndex channel = InvalidChannel;
			m_audio.PlaySound(sound, &channel);
			if (detector && channel != InvalidChannel)
			{
				m_audio.Set3DPosition(channel, detector->GetPosition());
			}
		}

		return PacketParseResult::Pass;
	}
```

Adapt details to the actual APIs while implementing: the unselect path used by `OnDestroyObjects` (~line 2056), `DBCache::Get` semantics (`db_cache.h:56` — returns cached entry or queues query and returns nullptr; the sound then simply falls back to default the first time), exact `InvalidChannel` constant name, and whether `Sound3D` + `Set3DPosition` is the pattern used elsewhere (if no 3D usage exists in world_state, use `SoundType::Sound2D` and skip positioning).

- [ ] **Step 3: Build mmo_client.** Expected: compiles.
- [ ] **Step 4: Commit** — `git commit -m "feat(stealth): client-side unit visibility list handling and stealth alert sound"`

---

### Task 7: Full build, tests, verification notes

- [ ] **Step 1:** `cmake --build build --config Debug` (full solution) — fix any compile fallout.
- [ ] **Step 2:** Run `unit_tests`, `game_server_unit_tests`, `login_server_tests`. Expected: all PASS.
- [ ] **Step 3:** Update spec status; document how to author the stealth spell in the editor (spell effect `ApplyAura` with aura `ModStealth`, aura interrupt flags `Damage | HitBySpell | Attack | Cast` so stealth breaks on damage/attacking, plus optional `ModDecreaseSpeed` effect for the classic stealth slow — all data, no code).
- [ ] **Step 4:** Final commit.
