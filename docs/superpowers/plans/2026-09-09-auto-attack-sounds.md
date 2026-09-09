# Auto Attack Sounds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give melee auto attacks data-driven sound — a swing whoosh, an impact that varies with the weapon swung and the material struck, distinct miss/parry/block/crit sounds, and per-model combat voice for both the attacker and the victim.

**Architecture:** Two proto tables gain sound fields (`ItemSubclassEntry` for weapons/armor/shields, `ModelDataEntry` for creatures and race voices), sharing new messages from a new `combat_sounds.proto`. A pure resolver in `game_client` turns "who swung what at whom, and what happened" into `SoundEntry` ids; a thin player rolls voice chances, enforces a per-unit voice throttle, and hands ids to the existing `SoundEntryPlayer`. `WorldState::OnAttackerStateUpdate` feeds it, deferring impact audio to the existing SwingHit animation notify. Client-side only.

**Tech Stack:** C++17, protobuf 2 syntax, Catch2 (`src/tests/game_client_tests`), ImGui (editor), CMake.

**Spec:** `docs/superpowers/specs/2026-09-09-auto-attack-sounds-design.md`

## Global Constraints

- Every source file starts with `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` and headers use `#pragma once`.
- Allman braces — every `{` and `}` on its own line. Braces always, even for single-line `if` bodies. Tabs for indentation.
- Members `m_camelCase`; methods `PascalCase`; locals and anonymous-namespace free functions `camelCase`; files `snake_case`.
- Doxygen comments on all public members.
- No exceptions (`SIMPLE_NO_EXCEPTIONS`). Use `ASSERT` / `VERIFY` / `DLOG` / `WLOG` / `ELOG` from `base/macros.h`.
- **Every `.proto` change must be applied identically to both mirrors** — `src/shared/proto_data/<name>.proto` and `src/shared/client_data/<name>.proto` — with **identical field numbers**. The client parses editor-exported binary data with the mirrored schema; a field-number mismatch silently corrupts data.
- **No network protocol change in this plan.** Nothing goes on the wire, so do NOT bump `mmo::game::ProtocolVersion` and do NOT run `tools/protocol_version_check.py --update`.
- All new proto fields are `optional` with 0/empty defaults so existing data keeps loading and behaves exactly as today (silent).
- Work happens on the current branch (`claude/auto-attack-sounds-131eb0`). Never push to origin.
- Both `src/shared/proto_data` and `src/shared/game_client` glob their sources, so **new files require a CMake re-configure**, not a CMakeLists edit: `cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON`.

---

## File Structure

**Created**

| File | Responsibility |
|---|---|
| `src/shared/proto_data/combat_sounds.proto` | Shared `ImpactSound` + `CombatSounds` messages (server/editor schema) |
| `src/shared/client_data/combat_sounds.proto` | Identical mirror (client schema) |
| `src/shared/game_client/combat_sound_resolver.h` / `.cpp` | Pure data → sound-id resolution. No audio, no scene, no globals. |
| `src/shared/game_client/combat_voice_gate.h` / `.cpp` | Per-unit voice throttle. Pure, clock injected. |
| `src/shared/game_client/combat_sound_player.h` / `.cpp` | Glue: resolve, roll chance, gate, hand to `SoundEntryPlayer` |
| `src/tests/game_client_tests/test_combat_sound_resolver.cpp` | Resolver tests |
| `src/tests/game_client_tests/test_combat_voice_gate.cpp` | Throttle tests |
| `src/tests/game_client_tests/test_combat_sound_player.cpp` | Player tests against `FakeAudio` |

**Modified**

| File | Change |
|---|---|
| `src/shared/proto_data/item_subclasses.proto` + client mirror | 7 new fields on `ItemSubclassEntry` |
| `src/shared/proto_data/model_data.proto` + client mirror | 1 new field on `ModelDataEntry` |
| `src/shared/game_client/game_unit_c.h` | 2 virtual accessors defaulting to 0 |
| `src/shared/game_client/game_player_c.h` / `.cpp` | Cache main-hand / off-hand / chest item subclass; override the accessors |
| `src/mmo_client/game_states/world_state.h` / `.cpp` | Own a `CombatSoundPlayer`; drive it from `OnAttackerStateUpdate` |
| `src/tests/game_client_tests/CMakeLists.txt` | Compile the three new sources into the suite (inside the client/editor guard) |
| `src/mmo_edit/editor_windows/item_subclass_editor_window.h` / `.cpp` | Combat Sounds section |
| `src/mmo_edit/editor_windows/model_editor_window.h` / `.cpp` | Combat Sounds section |

---

### Task 1: Proto schema

**Files:**
- Create: `src/shared/proto_data/combat_sounds.proto`
- Create: `src/shared/client_data/combat_sounds.proto`
- Modify: `src/shared/proto_data/item_subclasses.proto` (append fields to `ItemSubclassEntry`)
- Modify: `src/shared/client_data/item_subclasses.proto` (identical)
- Modify: `src/shared/proto_data/model_data.proto` (append field to `ModelDataEntry`)
- Modify: `src/shared/client_data/model_data.proto` (identical)

**Interfaces:**
- Consumes: nothing.
- Produces: `mmo::proto::ImpactSound` / `mmo::proto::CombatSounds` and their `mmo::proto_client::` twins. Accessors used by later tasks: `target_material()`, `sound()`, `swing_sound()`, `impact_sounds()`, `crit_layer_sound()`, `miss_sound()`, `parry_sound()`, `block_sound()`, `hit_material()`, `combat_sounds()`, `attack_voice_sound()`, `attack_voice_chance()`, `attack_crit_voice_sound()`, `attack_crit_voice_chance()`, `hit_voice_sound()`, `hit_voice_chance()`, `crit_hit_voice_sound()`, `crit_hit_voice_chance()`, `voice_min_interval_ms()`.

There is no unit test in this task: it produces generated code only, and no test suite links both `proto_data` and `client_data`, so a mirror-drift test is not expressible. Verification is a build of both libraries plus a byte-for-byte diff of the two files' message bodies.

- [ ] **Step 1: Create the shared proto (server/editor schema)**

Create `src/shared/proto_data/combat_sounds.proto`:

```proto
syntax = "proto2";
package mmo.proto;

// NOTE: Keep this file in sync with src/shared/client_data/combat_sounds.proto!
// The client reads the exported binary data with the mirrored proto_client schema,
// so every field number in here has to match the mirror exactly.

// One impact sound for a specific target material.
// target_material 0 = fallback used for any material.
message ImpactSound
{
	// SurfaceType id of the material being struck. 0 = any (default row).
	optional uint32 target_material = 1;

	// SoundEntry id played on impact against that material.
	optional uint32 sound = 2;
}

// Per-model combat audio: natural-weapon sounds, body material, and combat voice.
message CombatSounds
{
	// Natural weapon (creature claw/bite, unarmed player). Used when the swinging hand
	// holds no weapon, and as the fallback when the weapon subclass has no sound.
	optional uint32 swing_sound = 1;
	repeated ImpactSound impact_sounds = 2;
	optional uint32 crit_layer_sound = 3;
	optional uint32 miss_sound = 4;

	// SurfaceType id describing what this body sounds like when struck, unless worn
	// armor overrides it.
	optional uint32 hit_material = 5;

	// Attacker effort voice, rolled per swing.
	optional uint32 attack_voice_sound = 6;
	optional uint32 attack_voice_chance = 7 [default = 0];
	optional uint32 attack_crit_voice_sound = 8;
	optional uint32 attack_crit_voice_chance = 9 [default = 100];

	// Victim pain react, rolled per landed hit.
	optional uint32 hit_voice_sound = 10;
	optional uint32 hit_voice_chance = 11 [default = 100];
	optional uint32 crit_hit_voice_sound = 12;
	optional uint32 crit_hit_voice_chance = 13 [default = 100];

	// Minimum gap between two voice lines from the same unit, in milliseconds.
	// 0 = derive the gap from the played clip's length.
	optional uint32 voice_min_interval_ms = 14;
}
```

- [ ] **Step 2: Create the client mirror**

Create `src/shared/client_data/combat_sounds.proto` with **exactly the same content**, except the header comment, which points the other way:

```proto
syntax = "proto2";
package mmo.proto_client;

// NOTE: Keep this file in sync with src/shared/proto_data/combat_sounds.proto!
// Field numbers must match that file exactly.
```

The rest of the file (both messages, every field, every field number, every default) is character-identical to Step 1.

- [ ] **Step 3: Add the weapon/armor fields to `ItemSubclassEntry`**

In `src/shared/proto_data/item_subclasses.proto`, add the import below the `package` line:

```proto
import "combat_sounds.proto";
```

and append these fields inside `message ItemSubclassEntry`, after `offhand_attackanimation = 7`:

```proto
	// SoundEntry id for the swing whoosh played when an auto attack with a weapon of this
	// subclass starts. 0 = fall back to the attacker model's natural weapon swing.
	optional uint32 swing_sound = 8;

	// Impact sounds by struck material (SurfaceType id). A row with target_material 0 is
	// the fallback used when no row matches. Empty = fall back to the attacker's model.
	repeated ImpactSound impact_sounds = 9;

	// SoundEntry id layered on top of the impact sound on a critical hit. 0 = none.
	optional uint32 crit_layer_sound = 10;

	// SoundEntry id played when a swing with this weapon misses or is dodged. 0 = none.
	optional uint32 miss_sound = 11;

	// SoundEntry id played when a weapon of this subclass parries an incoming swing.
	optional uint32 parry_sound = 12;

	// SoundEntry id played when a shield of this subclass blocks an incoming swing.
	optional uint32 block_sound = 13;

	// Armor subclasses only: SurfaceType id describing what this armor sounds like when
	// struck. Overrides the wearer's body material. 0 = no override.
	optional uint32 hit_material = 14;
```

- [ ] **Step 4: Mirror the `ItemSubclassEntry` fields**

Apply the identical import and the identical seven fields to `src/shared/client_data/item_subclasses.proto`.

- [ ] **Step 5: Add the `CombatSounds` field to `ModelDataEntry`**

In `src/shared/proto_data/model_data.proto`, add below the `package` line:

```proto
import "combat_sounds.proto";
```

and append inside `message ModelDataEntry`, after `goodbye_sound_id = 9`:

```proto
	// Combat audio for units using this display model: natural weapon sounds for
	// creatures and unarmed players, the body's hit material, and the combat voice
	// (attacker effort and victim pain reacts).
	optional CombatSounds combat_sounds = 10;
```

- [ ] **Step 6: Mirror the `ModelDataEntry` field**

Apply the identical import and field to `src/shared/client_data/model_data.proto`.

- [ ] **Step 7: Verify the mirrors are identical**

Run:

```bash
diff <(sed 1,8d src/shared/proto_data/combat_sounds.proto) <(sed 1,7d src/shared/client_data/combat_sounds.proto)
```

Expected: only whitespace differences from the header comment offsets, and no differences in any `message`, field name, field number, or default. If any field number differs, fix it now — this is the single most costly mistake in this plan.

- [ ] **Step 8: Re-configure and build both proto libraries**

Run:

```bash
cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON
```

Then:

```bash
cmake --build build --config Debug -t proto_data client_data
```

Expected: both targets build. `combat_sounds.pb.cc` / `.pb.h` appear under `build/src/shared/proto_data/` and `build/src/shared/client_data/proto_client/`. If protoc reports `combat_sounds.proto: File not found`, the re-configure in the previous command did not happen — run it again.

- [ ] **Step 9: Commit**

```bash
git add src/shared/proto_data/combat_sounds.proto src/shared/client_data/combat_sounds.proto src/shared/proto_data/item_subclasses.proto src/shared/client_data/item_subclasses.proto src/shared/proto_data/model_data.proto src/shared/client_data/model_data.proto
git commit -m "feat: combat sound fields on item subclasses and model data"
```

---

### Task 2: Combat sound resolver

**Files:**
- Create: `src/shared/game_client/combat_sound_resolver.h`
- Create: `src/shared/game_client/combat_sound_resolver.cpp`
- Create: `src/tests/game_client_tests/test_combat_sound_resolver.cpp`
- Modify: `src/tests/game_client_tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the proto fields from Task 1.
- Produces, all in namespace `mmo`:
  - `struct CombatSoundAttacker { uint32 weaponSubclass = 0; uint32 displayId = 0; };`
  - `struct CombatSoundVictim { uint32 mainHandSubclass = 0; uint32 offHandSubclass = 0; uint32 chestSubclass = 0; uint32 displayId = 0; };`
  - `struct CombatSwingOutcome { bool landed, crit, miss, parry, block, fullBlock; };` (all default `false`)
  - `struct ResolvedSwingSounds { uint32 impactSound, critLayerSound, missSound, parrySound, blockSound; };` (all default `0`)
  - `struct CombatVoiceRequest { uint32 sound = 0; uint32 chance = 0; };`
  - `namespace combat_sounds`:
    - `uint32 ResolveVictimMaterial(const proto_client::ItemSubclassManager&, const proto_client::ModelDataManager&, const CombatSoundVictim&)`
    - `uint32 ResolveSwingSound(const proto_client::ItemSubclassManager&, const proto_client::ModelDataManager&, const CombatSoundAttacker&)`
    - `ResolvedSwingSounds ResolveSwingSounds(const proto_client::ItemSubclassManager&, const proto_client::ModelDataManager&, const CombatSoundAttacker&, const CombatSoundVictim&, const CombatSwingOutcome&)`
    - `CombatVoiceRequest ResolveAttackVoice(const proto_client::ModelDataManager&, uint32 displayId, bool crit)`
    - `CombatVoiceRequest ResolveHitVoice(const proto_client::ModelDataManager&, uint32 displayId, bool crit)`

- [ ] **Step 1: Write the failing tests**

Create `src/tests/game_client_tests/test_combat_sound_resolver.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/combat_sound_resolver.h"

using namespace mmo;

namespace
{
	constexpr uint32 kMaterialPlate = 10;
	constexpr uint32 kMaterialFlesh = 11;

	constexpr uint32 kSubclassSword = 1;
	constexpr uint32 kSubclassShield = 2;
	constexpr uint32 kSubclassPlateArmor = 3;

	constexpr uint32 kModelHuman = 100;
	constexpr uint32 kModelBoar = 101;

	/// In-memory project slice holding just the two managers the resolver reads.
	struct ResolverFixture
	{
		proto_client::ItemSubclassManager subclasses;
		proto_client::ModelDataManager models;

		proto_client::ItemSubclassEntry& AddSubclass(const uint32 id)
		{
			auto* entry = subclasses.add(id);
			REQUIRE(entry != nullptr);
			entry->set_name("subclass");
			entry->set_itemclass(0);
			return *entry;
		}

		proto_client::ModelDataEntry& AddModel(const uint32 id)
		{
			auto* entry = models.add(id);
			REQUIRE(entry != nullptr);
			entry->set_name("model");
			entry->set_filename("model.hmsh");
			return *entry;
		}

		static void AddImpact(proto_client::ItemSubclassEntry& entry, const uint32 material, const uint32 sound)
		{
			auto* impact = entry.add_impact_sounds();
			impact->set_target_material(material);
			impact->set_sound(sound);
		}

		static void AddImpact(proto_client::CombatSounds& sounds, const uint32 material, const uint32 sound)
		{
			auto* impact = sounds.add_impact_sounds();
			impact->set_target_material(material);
			impact->set_sound(sound);
		}
	};

	CombatSwingOutcome MakeHit(const bool crit = false)
	{
		CombatSwingOutcome outcome;
		outcome.landed = true;
		outcome.crit = crit;
		return outcome;
	}
}

TEST_CASE("Victim material comes from worn chest armor", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddSubclass(kSubclassPlateArmor).set_hit_material(kMaterialPlate);
	fixture.AddModel(kModelHuman).mutable_combat_sounds()->set_hit_material(kMaterialFlesh);

	CombatSoundVictim victim;
	victim.chestSubclass = kSubclassPlateArmor;
	victim.displayId = kModelHuman;

	CHECK(combat_sounds::ResolveVictimMaterial(fixture.subclasses, fixture.models, victim) == kMaterialPlate);
}

TEST_CASE("Victim material falls back to the model when no armor overrides it", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddModel(kModelBoar).mutable_combat_sounds()->set_hit_material(kMaterialFlesh);

	CombatSoundVictim victim;
	victim.displayId = kModelBoar;

	CHECK(combat_sounds::ResolveVictimMaterial(fixture.subclasses, fixture.models, victim) == kMaterialFlesh);
}

TEST_CASE("Victim material is zero when neither armor nor model define one", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddModel(kModelBoar);

	CombatSoundVictim victim;
	victim.displayId = kModelBoar;

	CHECK(combat_sounds::ResolveVictimMaterial(fixture.subclasses, fixture.models, victim) == 0);
}

TEST_CASE("Impact uses the row matching the victim material", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	ResolverFixture::AddImpact(sword, kMaterialPlate, 501);
	fixture.AddSubclass(kSubclassPlateArmor).set_hit_material(kMaterialPlate);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.chestSubclass = kSubclassPlateArmor;
	victim.displayId = kModelHuman;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit());
	CHECK(resolved.impactSound == 501);
}

TEST_CASE("Impact falls back to the default row when no material matches", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	ResolverFixture::AddImpact(sword, kMaterialPlate, 501);
	fixture.AddModel(kModelBoar).mutable_combat_sounds()->set_hit_material(kMaterialFlesh);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.displayId = kModelBoar;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit());
	CHECK(resolved.impactSound == 500);
}

TEST_CASE("An unarmed attacker uses its model's natural weapon sounds", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto* boarSounds = fixture.AddModel(kModelBoar).mutable_combat_sounds();
	boarSounds->set_swing_sound(600);
	ResolverFixture::AddImpact(*boarSounds, 0, 601);

	CombatSoundAttacker attacker;
	attacker.displayId = kModelBoar;

	CombatSoundVictim victim;

	CHECK(combat_sounds::ResolveSwingSound(fixture.subclasses, fixture.models, attacker) == 600);

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit());
	CHECK(resolved.impactSound == 601);
}

TEST_CASE("A weapon without sounds falls back to the attacker model", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddSubclass(kSubclassSword);
	auto* humanSounds = fixture.AddModel(kModelHuman).mutable_combat_sounds();
	humanSounds->set_swing_sound(610);
	ResolverFixture::AddImpact(*humanSounds, 0, 611);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;

	CHECK(combat_sounds::ResolveSwingSound(fixture.subclasses, fixture.models, attacker) == 610);

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit());
	CHECK(resolved.impactSound == 611);
}

TEST_CASE("A critical hit layers the crit sound on top of the impact", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	sword.set_crit_layer_sound(502);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.displayId = kModelHuman;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, MakeHit(true));
	CHECK(resolved.impactSound == 500);
	CHECK(resolved.critLayerSound == 502);
}

TEST_CASE("A miss plays the attacker's whiff and no impact", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	sword.set_miss_sound(503);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.displayId = kModelHuman;

	CombatSwingOutcome outcome;
	outcome.miss = true;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, outcome);
	CHECK(resolved.missSound == 503);
	CHECK(resolved.impactSound == 0);
}

TEST_CASE("A parry sound comes from the victim's main hand, not the attacker's", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& attackerSword = fixture.AddSubclass(kSubclassSword);
	attackerSword.set_miss_sound(503);
	attackerSword.set_parry_sound(504);

	constexpr uint32 kSubclassAxe = 4;
	fixture.AddSubclass(kSubclassAxe).set_parry_sound(505);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.mainHandSubclass = kSubclassAxe;
	victim.displayId = kModelHuman;

	CombatSwingOutcome outcome;
	outcome.parry = true;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, outcome);
	CHECK(resolved.parrySound == 505);
}

TEST_CASE("A parry without a victim weapon sound falls back to the attacker's whiff", "[combat_sounds]")
{
	ResolverFixture fixture;
	fixture.AddSubclass(kSubclassSword).set_miss_sound(503);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.displayId = kModelHuman;

	CombatSwingOutcome outcome;
	outcome.parry = true;

	const auto resolved = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, outcome);
	CHECK(resolved.parrySound == 503);
}

TEST_CASE("A full block replaces the impact, a partial block layers over it", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto& sword = fixture.AddSubclass(kSubclassSword);
	ResolverFixture::AddImpact(sword, 0, 500);
	fixture.AddSubclass(kSubclassShield).set_block_sound(506);
	fixture.AddModel(kModelHuman);

	CombatSoundAttacker attacker;
	attacker.weaponSubclass = kSubclassSword;
	attacker.displayId = kModelHuman;

	CombatSoundVictim victim;
	victim.offHandSubclass = kSubclassShield;
	victim.displayId = kModelHuman;

	CombatSwingOutcome fullBlock;
	fullBlock.block = true;
	fullBlock.fullBlock = true;

	const auto blocked = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, fullBlock);
	CHECK(blocked.blockSound == 506);
	CHECK(blocked.impactSound == 0);

	CombatSwingOutcome partialBlock;
	partialBlock.block = true;
	partialBlock.landed = true;

	const auto partial = combat_sounds::ResolveSwingSounds(fixture.subclasses, fixture.models, attacker, victim, partialBlock);
	CHECK(partial.blockSound == 506);
	CHECK(partial.impactSound == 500);
}

TEST_CASE("Voice resolution picks the crit variant only on a crit", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto* sounds = fixture.AddModel(kModelHuman).mutable_combat_sounds();
	sounds->set_attack_voice_sound(700);
	sounds->set_attack_voice_chance(20);
	sounds->set_attack_crit_voice_sound(701);
	sounds->set_attack_crit_voice_chance(100);
	sounds->set_hit_voice_sound(702);
	sounds->set_hit_voice_chance(80);
	sounds->set_crit_hit_voice_sound(703);

	const auto normalAttack = combat_sounds::ResolveAttackVoice(fixture.models, kModelHuman, false);
	CHECK(normalAttack.sound == 700);
	CHECK(normalAttack.chance == 20);

	const auto critAttack = combat_sounds::ResolveAttackVoice(fixture.models, kModelHuman, true);
	CHECK(critAttack.sound == 701);
	CHECK(critAttack.chance == 100);

	const auto normalHit = combat_sounds::ResolveHitVoice(fixture.models, kModelHuman, false);
	CHECK(normalHit.sound == 702);
	CHECK(normalHit.chance == 80);

	const auto critHit = combat_sounds::ResolveHitVoice(fixture.models, kModelHuman, true);
	CHECK(critHit.sound == 703);
}

TEST_CASE("A crit without an authored crit voice uses the normal voice", "[combat_sounds]")
{
	ResolverFixture fixture;
	auto* sounds = fixture.AddModel(kModelHuman).mutable_combat_sounds();
	sounds->set_attack_voice_sound(700);
	sounds->set_attack_voice_chance(20);

	const auto critAttack = combat_sounds::ResolveAttackVoice(fixture.models, kModelHuman, true);
	CHECK(critAttack.sound == 700);
	CHECK(critAttack.chance == 20);
}

TEST_CASE("An unknown model yields no voice", "[combat_sounds]")
{
	ResolverFixture fixture;

	const auto voice = combat_sounds::ResolveAttackVoice(fixture.models, kModelHuman, false);
	CHECK(voice.sound == 0);
	CHECK(voice.chance == 0);
}
```

- [ ] **Step 2: Wire the new sources into the test suite**

In `src/tests/game_client_tests/CMakeLists.txt`, extend the existing `if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)` block. Replace this block:

```cmake
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	target_sources(game_client_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/sound_entry_player.cpp
	)
	target_link_libraries(game_client_tests client_data)
else()
	set_source_files_properties(
		${CMAKE_CURRENT_SOURCE_DIR}/test_crossfading_sound_loop.cpp
		PROPERTIES HEADER_FILE_ONLY TRUE
	)
endif()
```

with:

```cmake
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	target_sources(game_client_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/sound_entry_player.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_sound_resolver.cpp
	)
	target_link_libraries(game_client_tests client_data)
else()
	set_source_files_properties(
		${CMAKE_CURRENT_SOURCE_DIR}/test_crossfading_sound_loop.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_sound_resolver.cpp
		PROPERTIES HEADER_FILE_ONLY TRUE
	)
endif()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run:

```bash
cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON && cmake --build build --config Debug -t game_client_tests
```

Expected: FAIL at compile time with `Cannot open include file: 'game_client/combat_sound_resolver.h'`.

- [ ] **Step 4: Write the resolver header**

Create `src/shared/game_client/combat_sound_resolver.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "client_data/project.h"

namespace mmo
{
	/// @brief Identity of the swinging unit, as far as combat audio is concerned.
	struct CombatSoundAttacker
	{
		/// Item subclass id of the weapon in the swinging hand. 0 = unarmed or creature.
		uint32 weaponSubclass = 0;

		/// Display model id, used for natural weapon sounds and the combat voice.
		uint32 displayId = 0;
	};

	/// @brief Identity of the unit being swung at, as far as combat audio is concerned.
	struct CombatSoundVictim
	{
		/// Item subclass id of the victim's main hand weapon, which owns the parry sound.
		uint32 mainHandSubclass = 0;

		/// Item subclass id of the victim's off hand item, which owns the block sound.
		uint32 offHandSubclass = 0;

		/// Item subclass id of the victim's chest armor, which owns the hit material.
		uint32 chestSubclass = 0;

		/// Display model id, used for the body material and the pain react voice.
		uint32 displayId = 0;
	};

	/// @brief What happened to a single auto attack swing.
	struct CombatSwingOutcome
	{
		/// The swing connected. True for a fully absorbed hit as well, which deals no damage
		/// but should still sound like metal meeting armor.
		bool landed = false;

		bool crit = false;

		/// Missed outright or was dodged. Both whiff.
		bool miss = false;

		bool parry = false;

		bool block = false;

		/// The block stopped everything, so no impact sound plays.
		bool fullBlock = false;
	};

	/// @brief SoundEntry ids resolved for one swing. 0 means play nothing for that slot.
	struct ResolvedSwingSounds
	{
		uint32 impactSound = 0;
		uint32 critLayerSound = 0;
		uint32 missSound = 0;
		uint32 parrySound = 0;
		uint32 blockSound = 0;
	};

	/// @brief A voice line candidate together with the chance that it plays at all.
	struct CombatVoiceRequest
	{
		/// SoundEntry id, 0 = nothing to play.
		uint32 sound = 0;

		/// Chance in percent that the line plays when rolled.
		uint32 chance = 0;
	};

	namespace combat_sounds
	{
		/// @brief Resolves what the victim's body sounds like when struck.
		///	Worn chest armor wins over the model's own material.
		/// @return SurfaceType id, or 0 when neither defines one.
		uint32 ResolveVictimMaterial(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundVictim& victim);

		/// @brief Resolves the swing whoosh of the attacker's weapon, falling back to the
		///	attacker model's natural weapon.
		/// @return SoundEntry id, or 0.
		uint32 ResolveSwingSound(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker);

		/// @brief Resolves every sound the given swing outcome calls for.
		ResolvedSwingSounds ResolveSwingSounds(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker,
			const CombatSoundVictim& victim, const CombatSwingOutcome& outcome);

		/// @brief Resolves the attacker's effort voice for a swing.
		CombatVoiceRequest ResolveAttackVoice(const proto_client::ModelDataManager& models, uint32 displayId, bool crit);

		/// @brief Resolves the victim's pain react voice for a landed hit.
		CombatVoiceRequest ResolveHitVoice(const proto_client::ModelDataManager& models, uint32 displayId, bool crit);
	}
}
```

- [ ] **Step 5: Write the resolver implementation**

Create `src/shared/game_client/combat_sound_resolver.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combat_sound_resolver.h"

namespace mmo
{
	namespace
	{
		/// Picks the impact sound for a material out of an authored row list. An exact
		/// material match wins; the row with target_material 0 is the fallback.
		template<typename TRows>
		uint32 findImpactSound(const TRows& rows, const uint32 material)
		{
			uint32 fallback = 0;
			for (const auto& row : rows)
			{
				if (row.target_material() == 0)
				{
					fallback = row.sound();
				}
				else if (row.target_material() == material)
				{
					return row.sound();
				}
			}

			return fallback;
		}

		const proto_client::CombatSounds* findModelSounds(const proto_client::ModelDataManager& models, const uint32 displayId)
		{
			const auto* model = models.getById(displayId);
			if (!model)
			{
				return nullptr;
			}

			return &model->combat_sounds();
		}

		uint32 resolveImpact(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker, const uint32 material)
		{
			if (const auto* subclass = subclasses.getById(attacker.weaponSubclass))
			{
				if (const uint32 sound = findImpactSound(subclass->impact_sounds(), material))
				{
					return sound;
				}
			}

			if (const auto* modelSounds = findModelSounds(models, attacker.displayId))
			{
				return findImpactSound(modelSounds->impact_sounds(), material);
			}

			return 0;
		}

		/// Reads one sound id off the attacker's weapon subclass, falling back to the same
		/// slot on the attacker model's natural weapon when the weapon has none authored.
		/// The accessor is a generic lambda because it is applied to two unrelated proto
		/// message types that happen to carry identically named fields.
		template<typename TAccessor>
		uint32 resolveWeaponSound(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker, TAccessor accessor)
		{
			if (const auto* subclass = subclasses.getById(attacker.weaponSubclass))
			{
				if (const uint32 sound = accessor(*subclass))
				{
					return sound;
				}
			}

			if (const auto* modelSounds = findModelSounds(models, attacker.displayId))
			{
				return accessor(*modelSounds);
			}

			return 0;
		}
	}

	namespace combat_sounds
	{
		uint32 ResolveVictimMaterial(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundVictim& victim)
		{
			if (const auto* chest = subclasses.getById(victim.chestSubclass))
			{
				if (chest->hit_material() != 0)
				{
					return chest->hit_material();
				}
			}

			if (const auto* modelSounds = findModelSounds(models, victim.displayId))
			{
				return modelSounds->hit_material();
			}

			return 0;
		}

		uint32 ResolveSwingSound(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker)
		{
			return resolveWeaponSound(subclasses, models, attacker,
				[](const auto& source) { return source.swing_sound(); });
		}

		ResolvedSwingSounds ResolveSwingSounds(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker,
			const CombatSoundVictim& victim, const CombatSwingOutcome& outcome)
		{
			ResolvedSwingSounds resolved;

			if (outcome.landed && !outcome.fullBlock)
			{
				const uint32 material = ResolveVictimMaterial(subclasses, models, victim);
				resolved.impactSound = resolveImpact(subclasses, models, attacker, material);

				if (outcome.crit)
				{
					resolved.critLayerSound = resolveWeaponSound(subclasses, models, attacker,
						[](const auto& source) { return source.crit_layer_sound(); });
				}
			}

			if (outcome.miss)
			{
				resolved.missSound = resolveWeaponSound(subclasses, models, attacker,
					[](const auto& source) { return source.miss_sound(); });
			}

			if (outcome.parry)
			{
				// A parry is weapon on weapon, so the sound belongs to the defender's weapon.
				// Without one authored, the attacker's whiff is the next best thing.
				if (const auto* defenderWeapon = subclasses.getById(victim.mainHandSubclass))
				{
					resolved.parrySound = defenderWeapon->parry_sound();
				}

				if (resolved.parrySound == 0)
				{
					resolved.parrySound = resolveWeaponSound(subclasses, models, attacker,
						[](const auto& source) { return source.miss_sound(); });
				}
			}

			if (outcome.block)
			{
				if (const auto* shield = subclasses.getById(victim.offHandSubclass))
				{
					resolved.blockSound = shield->block_sound();
				}
			}

			return resolved;
		}

		CombatVoiceRequest ResolveAttackVoice(const proto_client::ModelDataManager& models, const uint32 displayId, const bool crit)
		{
			CombatVoiceRequest request;

			const auto* sounds = findModelSounds(models, displayId);
			if (!sounds)
			{
				return request;
			}

			if (crit && sounds->attack_crit_voice_sound() != 0)
			{
				request.sound = sounds->attack_crit_voice_sound();
				request.chance = sounds->attack_crit_voice_chance();
				return request;
			}

			request.sound = sounds->attack_voice_sound();
			request.chance = sounds->attack_voice_chance();
			return request;
		}

		CombatVoiceRequest ResolveHitVoice(const proto_client::ModelDataManager& models, const uint32 displayId, const bool crit)
		{
			CombatVoiceRequest request;

			const auto* sounds = findModelSounds(models, displayId);
			if (!sounds)
			{
				return request;
			}

			if (crit && sounds->crit_hit_voice_sound() != 0)
			{
				request.sound = sounds->crit_hit_voice_sound();
				request.chance = sounds->crit_hit_voice_chance();
				return request;
			}

			request.sound = sounds->hit_voice_sound();
			request.chance = sounds->hit_voice_chance();
			return request;
		}
	}
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run:

```bash
cmake --build build --config Debug -t game_client_tests && ./bin/Debug/game_client_tests.exe "[combat_sounds]"
```

Expected: PASS, 15 test cases, `All tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/shared/game_client/combat_sound_resolver.h src/shared/game_client/combat_sound_resolver.cpp src/tests/game_client_tests/test_combat_sound_resolver.cpp src/tests/game_client_tests/CMakeLists.txt
git commit -m "feat: combat sound resolver for auto attacks"
```

---

### Task 3: Combat voice throttle

**Files:**
- Create: `src/shared/game_client/combat_voice_gate.h`
- Create: `src/shared/game_client/combat_voice_gate.cpp`
- Create: `src/tests/game_client_tests/test_combat_voice_gate.cpp`
- Modify: `src/tests/game_client_tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `mmo::CombatVoiceGate` with
  - `bool TryPlay(ObjectGuid unit, GameTime now, GameTime blockDuration)` — returns true and arms the gate when the unit may speak, false when it is still blocked.
  - `void Clear()` — drops all state.

- [ ] **Step 1: Write the failing tests**

Create `src/tests/game_client_tests/test_combat_voice_gate.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_client/combat_voice_gate.h"

using namespace mmo;

TEST_CASE("The first voice line of a unit always passes", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	CHECK(gate.TryPlay(1, 1000, 500));
}

TEST_CASE("A second line inside the block window is refused", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	CHECK_FALSE(gate.TryPlay(1, 1200, 500));
	CHECK_FALSE(gate.TryPlay(1, 1499, 500));
}

TEST_CASE("A line at or after the block window passes", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	CHECK(gate.TryPlay(1, 1500, 500));
}

TEST_CASE("The gate is per unit", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	CHECK(gate.TryPlay(2, 1000, 500));
}

TEST_CASE("A refused line does not extend the block window", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	CHECK_FALSE(gate.TryPlay(1, 1400, 500));
	CHECK(gate.TryPlay(1, 1500, 500));
}

TEST_CASE("Clear drops all gate state", "[combat_voice_gate]")
{
	CombatVoiceGate gate;
	REQUIRE(gate.TryPlay(1, 1000, 500));
	gate.Clear();
	CHECK(gate.TryPlay(1, 1100, 500));
}
```

- [ ] **Step 2: Wire the new sources into the test suite**

In `src/tests/game_client_tests/CMakeLists.txt`, add `combat_voice_gate.cpp` to the `target_sources` list inside the `if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)` block, and `test_combat_voice_gate.cpp` to the `set_source_files_properties` list in the `else()` branch. The block becomes:

```cmake
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	target_sources(game_client_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/sound_entry_player.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_sound_resolver.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_voice_gate.cpp
	)
	target_link_libraries(game_client_tests client_data)
else()
	set_source_files_properties(
		${CMAKE_CURRENT_SOURCE_DIR}/test_crossfading_sound_loop.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_sound_resolver.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_voice_gate.cpp
		PROPERTIES HEADER_FILE_ONLY TRUE
	)
endif()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run:

```bash
cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON && cmake --build build --config Debug -t game_client_tests
```

Expected: FAIL at compile time with `Cannot open include file: 'game_client/combat_voice_gate.h'`.

- [ ] **Step 4: Write the header**

Create `src/shared/game_client/combat_voice_gate.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <unordered_map>

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Keeps a unit from talking over itself in combat. One gate per unit covers both
	///	the attacker's effort voice and the victim's pain react, so the two never overlap.
	class CombatVoiceGate final
	{
	public:
		/// @brief Asks whether the given unit may play a voice line right now, arming the
		///	gate when it may.
		/// @param unit Guid of the speaking unit.
		/// @param now Current time in milliseconds.
		/// @param blockDuration How long the unit stays silent afterwards, in milliseconds.
		/// @return true when the line may play, false while the unit is still blocked.
		bool TryPlay(ObjectGuid unit, GameTime now, GameTime blockDuration);

		/// @brief Drops all per-unit state, for example on a world change.
		void Clear();

	private:
		/// @brief Drops entries whose block window has passed, so the map cannot grow
		///	without bound over a long session.
		void Prune(GameTime now);

	private:
		/// Earliest time each unit may speak again.
		std::unordered_map<ObjectGuid, GameTime> m_nextAllowed;
	};
}
```

- [ ] **Step 5: Write the implementation**

Create `src/shared/game_client/combat_voice_gate.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combat_voice_gate.h"

namespace mmo
{
	namespace
	{
		/// Above this many tracked units, expired entries are swept before inserting more.
		constexpr size_t pruneThreshold = 256;
	}

	bool CombatVoiceGate::TryPlay(const ObjectGuid unit, const GameTime now, const GameTime blockDuration)
	{
		const auto it = m_nextAllowed.find(unit);
		if (it != m_nextAllowed.end() && now < it->second)
		{
			return false;
		}

		if (m_nextAllowed.size() >= pruneThreshold)
		{
			Prune(now);
		}

		m_nextAllowed[unit] = now + blockDuration;
		return true;
	}

	void CombatVoiceGate::Clear()
	{
		m_nextAllowed.clear();
	}

	void CombatVoiceGate::Prune(const GameTime now)
	{
		for (auto it = m_nextAllowed.begin(); it != m_nextAllowed.end();)
		{
			if (it->second <= now)
			{
				it = m_nextAllowed.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run:

```bash
cmake --build build --config Debug -t game_client_tests && ./bin/Debug/game_client_tests.exe "[combat_voice_gate]"
```

Expected: PASS, 6 test cases, `All tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/shared/game_client/combat_voice_gate.h src/shared/game_client/combat_voice_gate.cpp src/tests/game_client_tests/test_combat_voice_gate.cpp src/tests/game_client_tests/CMakeLists.txt
git commit -m "feat: per-unit combat voice throttle"
```

---

### Task 4: Combat sound player

**Files:**
- Create: `src/shared/game_client/combat_sound_player.h`
- Create: `src/shared/game_client/combat_sound_player.cpp`
- Create: `src/tests/game_client_tests/fake_audio.h` (shared `IAudio` test double)
- Create: `src/tests/game_client_tests/test_combat_sound_player.cpp`
- Modify: `src/tests/game_client_tests/test_crossfading_sound_loop.cpp` (use the shared double)
- Modify: `src/tests/game_client_tests/CMakeLists.txt`

**Interfaces:**
- Consumes: everything produced by Tasks 2 and 3, plus `SoundEntryPlayer` from `game_client/sound_entry_player.h` (`PlayEntry(uint32)`, `PlayEntry(uint32, const Vector3&)`, `GetEntryLength(uint32)`).
- Produces: `mmo::CombatSoundPlayer` with
  - `CombatSoundPlayer(const proto_client::Project& project, SoundEntryPlayer& soundPlayer)`
  - `void PlaySwing(const CombatSoundAttacker&, ObjectGuid attackerGuid, const Vector3& attackerPos, bool crit)`
  - `void PlayOutcome(const CombatSoundAttacker&, const CombatSoundVictim&, const CombatSwingOutcome&, ObjectGuid victimGuid, const Vector3& victimPos)`
  - `void SetRollProvider(std::function<uint32()>)` — provider returns 0..99
  - `void SetClock(std::function<GameTime()>)`
  - `void Clear()`

- [ ] **Step 1: Extract the shared audio test double**

`test_crossfading_sound_loop.cpp` already carries an `IAudio` double. Rather than copying it, lift it into a shared header that records both what the existing suite needs (started and stopped channels) and what the new tests need (played file names).

Create `src/tests/game_client_tests/fake_audio.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <map>
#include <set>
#include <vector>

#include "audio/audio.h"

namespace mmo
{
	/// @brief Fake channel instance which simply records the pitch and volume set on it.
	class FakeChannelInstance final : public IChannelInstance
	{
	public:
		void Clear() override
		{
		}

		void SetPitch(const float value) override
		{
			m_pitch = value;
		}

		float GetPitch() const override
		{
			return m_pitch;
		}

		void SetVolume(const float volume) override
		{
			m_volume = volume;
		}

		float GetVolume() const override
		{
			return m_volume;
		}

	private:
		float m_pitch = 1.0f;
		float m_volume = 1.0f;
	};

	/// @brief Headless IAudio double: hands out incrementing sound / channel indices and
	///	records which channels were started and stopped, and which files were played.
	class FakeAudio final : public IAudio
	{
	public:
		/// Channels handed out by PlaySound / PlaySound3D, in order.
		std::vector<ChannelIndex> startedChannels;

		/// Channels passed to StopSound.
		std::set<ChannelIndex> stoppedChannels;

		/// File names behind every played sound index, in order.
		std::vector<String> playedFiles;

		void Create() override
		{
		}

		void Destroy() override
		{
		}

		void Update(const Vector3&, float) override
		{
		}

		SoundIndex CreateSound(const String& fileName) override
		{
			const SoundIndex index = m_nextSound++;
			m_files[index] = fileName;
			return index;
		}

		SoundIndex CreateStream(const String& fileName) override
		{
			return CreateSound(fileName);
		}

		SoundIndex CreateLoopedSound(const String& fileName) override
		{
			return CreateSound(fileName);
		}

		SoundIndex CreateLoopedStream(const String& fileName) override
		{
			return CreateSound(fileName);
		}

		SoundIndex CreateSound(const String& fileName, SoundType) override
		{
			return CreateSound(fileName);
		}

		void PlaySound(const SoundIndex sound, ChannelIndex* channelIndex, float, SoundCategory) override
		{
			Start(sound, channelIndex);
		}

		void PlaySound3D(const SoundIndex sound, ChannelIndex* channelIndex, const Vector3&, float, float, float, SoundCategory) override
		{
			Start(sound, channelIndex);
		}

		void StopSound(ChannelIndex* channelIndex) override
		{
			if (channelIndex && *channelIndex != InvalidChannel)
			{
				stoppedChannels.insert(*channelIndex);
				*channelIndex = InvalidChannel;
			}
		}

		void StopAllSounds() override
		{
		}

		SoundIndex FindSound(const String&, SoundType) override
		{
			return InvalidSound;
		}

		void Set3DMinMaxDistance(ChannelIndex, float, float) override
		{
		}

		void Set3DPosition(ChannelIndex, const Vector3&) override
		{
		}

		float GetSoundLength(SoundIndex) override
		{
			return 0.0f;
		}

		ISoundInstance* GetSoundInstance(SoundIndex) override
		{
			return nullptr;
		}

		IChannelInstance* GetChannelInstance(const ChannelIndex channel) override
		{
			return &m_channels[channel];
		}

		void SetMasterVolume(float) override
		{
		}

		void SetMasterMuted(bool) override
		{
		}

		void SetCategoryVolume(SoundCategory, float) override
		{
		}

		void SetCategoryMuted(SoundCategory, bool) override
		{
		}

		/// @brief Gets the volume last set on the given channel.
		float GetChannelVolume(const ChannelIndex channel)
		{
			return m_channels[channel].GetVolume();
		}

		/// @brief Whether a sound created from the given file was played at least once.
		bool PlayedFile(const String& fileName) const
		{
			return std::find(playedFiles.begin(), playedFiles.end(), fileName) != playedFiles.end();
		}

	private:
		void Start(const SoundIndex sound, ChannelIndex* channelIndex)
		{
			const auto it = m_files.find(sound);
			playedFiles.push_back(it == m_files.end() ? String() : it->second);

			const ChannelIndex channel = m_nextChannel++;
			startedChannels.push_back(channel);

			if (channelIndex)
			{
				*channelIndex = channel;
			}
		}

	private:
		SoundIndex m_nextSound = 0;
		ChannelIndex m_nextChannel = 0;
		std::map<SoundIndex, String> m_files;
		std::map<ChannelIndex, FakeChannelInstance> m_channels;
	};
}
```

Add `#include <algorithm>` to that header's include block if `std::find` does not resolve.

- [ ] **Step 2: Point the existing suite at the shared double**

In `src/tests/game_client_tests/test_crossfading_sound_loop.cpp`, delete the local `FakeChannelInstance` and `FakeAudio` class definitions from its anonymous namespace (they run from the `/// Fake channel instance which simply records the volume set on it.` comment through the closing `};` of `FakeAudio`), and add above the anonymous namespace:

```cpp
#include "fake_audio.h"
```

The remaining helpers in that file (`addLoopedEntry` and friends) stay where they are. The shared double is a superset of the old one — same member names, same behaviour — so no test body changes.

- [ ] **Step 3: Run the existing suite to prove the extraction is behaviour-neutral**

Run:

```bash
cmake --build build --config Debug -t game_client_tests && ./bin/Debug/game_client_tests.exe "[crossfading_sound_loop]"
```

Expected: PASS, exactly as before the extraction. If the tag name does not match, run the suite with no filter — every pre-existing case must still pass.

- [ ] **Step 4: Write the failing tests**

Create `src/tests/game_client_tests/test_combat_sound_player.cpp`, using the shared double from Step 1.

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_audio.h"

#include "game_client/combat_sound_player.h"
#include "game_client/sound_entry_player.h"

using namespace mmo;

namespace
{
	constexpr uint32 kSubclassSword = 1;
	constexpr uint32 kModelHuman = 100;

	constexpr uint32 kSoundSwing = 500;
	constexpr uint32 kSoundImpact = 501;
	constexpr uint32 kSoundCritLayer = 502;
	constexpr uint32 kSoundAttackVoice = 700;
	constexpr uint32 kSoundHitVoice = 702;

	/// Project slice plus doubles, wired the way the client wires them.
	struct PlayerFixture
	{
		proto_client::Project project;
		FakeAudio audio;
		SoundEntryPlayer soundPlayer{ audio, project.sounds };

		PlayerFixture()
		{
			AddSound(kSoundSwing, "swing.wav");
			AddSound(kSoundImpact, "impact.wav");
			AddSound(kSoundCritLayer, "crit.wav");
			AddSound(kSoundAttackVoice, "attack_voice.wav");
			AddSound(kSoundHitVoice, "hit_voice.wav");

			auto* sword = project.itemSubclasses.add(kSubclassSword);
			sword->set_name("Sword");
			sword->set_itemclass(0);
			sword->set_swing_sound(kSoundSwing);
			sword->set_crit_layer_sound(kSoundCritLayer);
			auto* impact = sword->add_impact_sounds();
			impact->set_target_material(0);
			impact->set_sound(kSoundImpact);

			auto* model = project.models.add(kModelHuman);
			model->set_name("Human");
			model->set_filename("human.hmsh");
			auto* sounds = model->mutable_combat_sounds();
			sounds->set_attack_voice_sound(kSoundAttackVoice);
			sounds->set_attack_voice_chance(50);
			sounds->set_hit_voice_sound(kSoundHitVoice);
			sounds->set_hit_voice_chance(50);
			sounds->set_voice_min_interval_ms(1000);
		}

		void AddSound(const uint32 id, const String& file)
		{
			auto* entry = project.sounds.add(id);
			entry->set_name(file);
			entry->add_files(file);
		}

		bool Played(const String& file) const
		{
			return audio.PlayedFile(file);
		}
	};

	CombatSoundAttacker MakeAttacker()
	{
		CombatSoundAttacker attacker;
		attacker.weaponSubclass = kSubclassSword;
		attacker.displayId = kModelHuman;
		return attacker;
	}

	CombatSoundVictim MakeVictim()
	{
		CombatSoundVictim victim;
		victim.displayId = kModelHuman;
		return victim;
	}
}

TEST_CASE("A swing plays the weapon whoosh", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 99u; });   // never passes a 50% chance
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK(fixture.Played("swing.wav"));
	CHECK_FALSE(fixture.Played("attack_voice.wav"));
}

TEST_CASE("A landed hit plays impact, and a crit layers the crit sound", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 99u; });
	player.SetClock([] { return GameTime(0); });

	CombatSwingOutcome outcome;
	outcome.landed = true;
	outcome.crit = true;

	player.PlayOutcome(MakeAttacker(), MakeVictim(), outcome, 2, Vector3::Zero);

	CHECK(fixture.Played("impact.wav"));
	CHECK(fixture.Played("crit.wav"));
}

TEST_CASE("A voice line plays when the roll is under the chance", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 10u; });   // under the authored 50%
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK(fixture.Played("attack_voice.wav"));
}

TEST_CASE("A voice line is refused when the roll is at or over the chance", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 50u; });   // exactly at the authored 50%
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK_FALSE(fixture.Played("attack_voice.wav"));
}

TEST_CASE("A chance of zero never plays, whatever the roll", "[combat_sound_player]")
{
	PlayerFixture fixture;
	auto* sounds = fixture.project.models.getById(kModelHuman)->mutable_combat_sounds();
	sounds->set_attack_voice_chance(0);

	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 0u; });   // the lowest possible roll
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK_FALSE(fixture.Played("attack_voice.wav"));
	CHECK(fixture.Played("swing.wav"));
}

TEST_CASE("A chance of one hundred always plays, whatever the roll", "[combat_sound_player]")
{
	PlayerFixture fixture;
	auto* sounds = fixture.project.models.getById(kModelHuman)->mutable_combat_sounds();
	sounds->set_attack_voice_chance(100);

	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 99u; });   // the highest possible roll
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK(fixture.Played("attack_voice.wav"));
}

TEST_CASE("A unit does not talk over itself inside the throttle window", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 0u; });

	GameTime now = 0;
	player.SetClock([&now] { return now; });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);
	REQUIRE(fixture.Played("attack_voice.wav"));

	fixture.audio.playedFiles.clear();

	now = 500;   // inside the authored 1000ms interval
	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);
	CHECK_FALSE(fixture.Played("attack_voice.wav"));

	now = 1000;
	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);
	CHECK(fixture.Played("attack_voice.wav"));
}

TEST_CASE("The attacker and the victim are gated independently", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 0u; });
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);
	REQUIRE(fixture.Played("attack_voice.wav"));

	fixture.audio.playedFiles.clear();

	CombatSwingOutcome outcome;
	outcome.landed = true;

	// Victim guid 2 has its own gate, so its pain react is not blocked by the attacker's line.
	player.PlayOutcome(MakeAttacker(), MakeVictim(), outcome, 2, Vector3::Zero);
	CHECK(fixture.Played("hit_voice.wav"));
}
```

- [ ] **Step 5: Wire the new sources into the test suite**

In `src/tests/game_client_tests/CMakeLists.txt`, add `combat_sound_player.cpp` to `target_sources` and `test_combat_sound_player.cpp` to the `else()` branch's property list. The block becomes:

```cmake
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	target_sources(game_client_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/sound_entry_player.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_sound_resolver.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_voice_gate.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_sound_player.cpp
	)
	target_link_libraries(game_client_tests client_data)
else()
	set_source_files_properties(
		${CMAKE_CURRENT_SOURCE_DIR}/test_crossfading_sound_loop.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_sound_resolver.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_voice_gate.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_sound_player.cpp
		PROPERTIES HEADER_FILE_ONLY TRUE
	)
endif()
```

- [ ] **Step 6: Run the tests to verify they fail**

Run:

```bash
cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON && cmake --build build --config Debug -t game_client_tests
```

Expected: FAIL at compile time with `Cannot open include file: 'game_client/combat_sound_player.h'`.

The `FakeAudio` override set above was written against `src/shared/audio/audio.h` as it stands. If it fails with "cannot instantiate abstract class", the interface has gained a method since — open that header, add the missing override with an empty body returning a default, and re-run.

- [ ] **Step 7: Write the header**

Create `src/shared/game_client/combat_sound_player.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "client_data/project.h"
#include "combat_sound_resolver.h"
#include "combat_voice_gate.h"
#include "math/vector3.h"

namespace mmo
{
	class SoundEntryPlayer;

	/// @brief Plays the audio of a melee auto attack: the swing whoosh and the attacker's
	///	effort voice at swing time, and the impact, crit layer, whiff, parry, block and the
	///	victim's pain react once the weapon connects.
	class CombatSoundPlayer final : public NonCopyable
	{
	public:
		/// @param project Client data the sounds are resolved from.
		/// @param soundPlayer Sound entry playback used for every sound this class plays.
		CombatSoundPlayer(const proto_client::Project& project, SoundEntryPlayer& soundPlayer);

	public:
		/// @brief Plays the swing whoosh and rolls the attacker's effort voice.
		/// @param attacker Weapon and model of the swinging unit.
		/// @param attackerGuid Guid of the swinging unit, used for the voice throttle.
		/// @param attackerPos World position the swing sound plays at.
		/// @param crit Whether this swing is going to be a critical hit.
		void PlaySwing(const CombatSoundAttacker& attacker, ObjectGuid attackerGuid, const Vector3& attackerPos, bool crit);

		/// @brief Plays everything the swing outcome calls for, and rolls the victim's
		///	pain react. Call this when the weapon visually connects.
		void PlayOutcome(const CombatSoundAttacker& attacker, const CombatSoundVictim& victim,
			const CombatSwingOutcome& outcome, ObjectGuid victimGuid, const Vector3& victimPos);

		/// @brief Drops the voice throttle state, for example on a world change.
		void Clear();

		/// @brief Overrides the chance roll source. The provider returns values in [0, 99].
		///	Tests inject a deterministic one.
		void SetRollProvider(std::function<uint32()> roll);

		/// @brief Overrides the clock. Tests inject a controllable one.
		void SetClock(std::function<GameTime()> clock);

	private:
		/// @brief Rolls the chance, checks the throttle and plays the line at the position.
		void TryPlayVoice(const CombatVoiceRequest& request, ObjectGuid unit, uint32 displayId, const Vector3& position);

		/// @brief How long the unit stays silent after playing the given sound entry.
		[[nodiscard]] GameTime GetVoiceBlockDuration(uint32 displayId, uint32 sound) const;

		/// @brief Plays a sound entry positionally, ignoring 0.
		void Play(uint32 sound, const Vector3& position) const;

	private:
		const proto_client::Project& m_project;
		SoundEntryPlayer& m_soundPlayer;
		CombatVoiceGate m_voiceGate;
		std::function<uint32()> m_roll;
		std::function<GameTime()> m_clock;
	};
}
```

- [ ] **Step 8: Write the implementation**

Create `src/shared/game_client/combat_sound_player.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combat_sound_player.h"

#include <random>

#include "base/clock.h"
#include "sound_entry_player.h"

namespace mmo
{
	namespace
	{
		/// Extra silence appended after a voice clip so two lines never butt up against
		/// each other, matching what the cast error voice does.
		constexpr GameTime voiceTailMs = 250;

		uint32 rollPercent()
		{
			static std::random_device rd;
			static std::mt19937 gen(rd());
			std::uniform_int_distribution<uint32> dis(0, 99);
			return dis(gen);
		}
	}

	CombatSoundPlayer::CombatSoundPlayer(const proto_client::Project& project, SoundEntryPlayer& soundPlayer)
		: m_project(project)
		, m_soundPlayer(soundPlayer)
		, m_roll(rollPercent)
		, m_clock(GetAsyncTimeMs)
	{
	}

	void CombatSoundPlayer::PlaySwing(const CombatSoundAttacker& attacker, const ObjectGuid attackerGuid,
		const Vector3& attackerPos, const bool crit)
	{
		Play(combat_sounds::ResolveSwingSound(m_project.itemSubclasses, m_project.models, attacker), attackerPos);

		TryPlayVoice(combat_sounds::ResolveAttackVoice(m_project.models, attacker.displayId, crit),
			attackerGuid, attacker.displayId, attackerPos);
	}

	void CombatSoundPlayer::PlayOutcome(const CombatSoundAttacker& attacker, const CombatSoundVictim& victim,
		const CombatSwingOutcome& outcome, const ObjectGuid victimGuid, const Vector3& victimPos)
	{
		const ResolvedSwingSounds resolved = combat_sounds::ResolveSwingSounds(
			m_project.itemSubclasses, m_project.models, attacker, victim, outcome);

		Play(resolved.impactSound, victimPos);
		Play(resolved.critLayerSound, victimPos);
		Play(resolved.missSound, victimPos);
		Play(resolved.parrySound, victimPos);
		Play(resolved.blockSound, victimPos);

		if (outcome.landed)
		{
			TryPlayVoice(combat_sounds::ResolveHitVoice(m_project.models, victim.displayId, outcome.crit),
				victimGuid, victim.displayId, victimPos);
		}
	}

	void CombatSoundPlayer::Clear()
	{
		m_voiceGate.Clear();
	}

	void CombatSoundPlayer::SetRollProvider(std::function<uint32()> roll)
	{
		m_roll = std::move(roll);
	}

	void CombatSoundPlayer::SetClock(std::function<GameTime()> clock)
	{
		m_clock = std::move(clock);
	}

	void CombatSoundPlayer::TryPlayVoice(const CombatVoiceRequest& request, const ObjectGuid unit,
		const uint32 displayId, const Vector3& position)
	{
		if (request.sound == 0 || request.chance == 0)
		{
			return;
		}

		if (request.chance < 100 && m_roll() >= request.chance)
		{
			return;
		}

		if (!m_voiceGate.TryPlay(unit, m_clock(), GetVoiceBlockDuration(displayId, request.sound)))
		{
			return;
		}

		Play(request.sound, position);
	}

	GameTime CombatSoundPlayer::GetVoiceBlockDuration(const uint32 displayId, const uint32 sound) const
	{
		if (const auto* model = m_project.models.getById(displayId))
		{
			if (const uint32 authored = model->combat_sounds().voice_min_interval_ms())
			{
				return authored;
			}
		}

		const float length = m_soundPlayer.GetEntryLength(sound);
		return static_cast<GameTime>(length * 1000.0f) + voiceTailMs;
	}

	void CombatSoundPlayer::Play(const uint32 sound, const Vector3& position) const
	{
		if (sound == 0)
		{
			return;
		}

		m_soundPlayer.PlayEntry(sound, position);
	}
}
```

- [ ] **Step 9: Run the tests to verify they pass**

Run:

```bash
cmake --build build --config Debug -t game_client_tests && ./bin/Debug/game_client_tests.exe "[combat_sound_player]"
```

Expected: PASS, 8 test cases, `All tests passed`.

- [ ] **Step 10: Run the whole suite**

Run:

```bash
./bin/Debug/game_client_tests.exe
```

Expected: `All tests passed` — the pre-existing cases must still pass.

- [ ] **Step 11: Commit**

```bash
git add src/shared/game_client/combat_sound_player.h src/shared/game_client/combat_sound_player.cpp src/tests/game_client_tests/fake_audio.h src/tests/game_client_tests/test_combat_sound_player.cpp src/tests/game_client_tests/test_crossfading_sound_loop.cpp src/tests/game_client_tests/CMakeLists.txt
git commit -m "feat: combat sound player with voice chance and throttle"
```

---

### Task 5: Expose equipped item subclasses on units

**Files:**
- Modify: `src/shared/game_client/game_unit_c.h` (add two virtuals near `SetWeaponAttackAnimations`, around line 678)
- Modify: `src/shared/game_client/game_player_c.h`
- Modify: `src/shared/game_client/game_player_c.cpp:118-155` and the reset path at `:497`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces, used by Task 6:
  - `virtual uint32 GameUnitC::GetWeaponItemSubclass(bool offhand) const` — returns 0 on the base class.
  - `virtual uint32 GameUnitC::GetChestItemSubclass() const` — returns 0 on the base class.
  - `GamePlayerC` overrides both from cached equipment state.

This task has no unit test: `GamePlayerC` requires a `Scene` and a live field map, which the headless suite cannot build. Verification is a client build plus the in-game check in Task 6.

- [ ] **Step 1: Add the base accessors**

In `src/shared/game_client/game_unit_c.h`, directly above the existing `void SetWeaponAttackAnimations(const std::vector<String>& animNames);` declaration, add:

```cpp
		/// @brief Gets the item subclass id of the weapon in the given hand, used to resolve
		///	auto attack sounds. Units without equipment (creatures) report 0, which makes the
		///	sound resolution fall back to the unit model's natural weapon.
		/// @param offhand True for the off-hand slot, false for the main hand.
		[[nodiscard]] virtual uint32 GetWeaponItemSubclass(bool offhand) const { return 0; }

		/// @brief Gets the item subclass id of the equipped chest armor, which decides what
		///	this unit sounds like when struck. 0 = fall back to the model's body material.
		[[nodiscard]] virtual uint32 GetChestItemSubclass() const { return 0; }
```

- [ ] **Step 2: Add the cached members and overrides to `GamePlayerC`**

In `src/shared/game_client/game_player_c.h`, inside the class's `public:` section, add:

```cpp
		[[nodiscard]] uint32 GetWeaponItemSubclass(bool offhand) const override
		{
			return offhand ? m_offHandSubclass : m_mainHandSubclass;
		}

		[[nodiscard]] uint32 GetChestItemSubclass() const override
		{
			return m_chestSubclass;
		}
```

and in the `private:` member section (the one at line 189):

```cpp
		/// Item subclass of the equipped main hand weapon. 0 = unarmed.
		uint32 m_mainHandSubclass = 0;

		/// Item subclass of the equipped off hand weapon or shield. 0 = empty.
		uint32 m_offHandSubclass = 0;

		/// Item subclass of the equipped chest armor. 0 = none.
		uint32 m_chestSubclass = 0;
```

- [ ] **Step 3: Cache the subclasses when item data arrives**

In `src/shared/game_client/game_player_c.cpp`, inside `NotifyItemData`, in the block that already resolves the main-hand subclass, add the assignment right after `SetWeaponReadyAnimation(readyAnimation);`:

```cpp
				// Remember the subclass itself: the auto attack sound resolution needs it
				// on every swing, not just when the equipment changes.
				m_mainHandSubclass = data.itemSubclass;
```

In the off-hand block, right after `SetOffhandWeaponAttackAnimations(offhandAttackAnimations);`:

```cpp
				m_offHandSubclass = data.itemSubclass;
```

Then, still inside the same braced block and after the off-hand section, add the chest slot:

```cpp
			// The chest slot decides what the wearer sounds like when struck.
			const uint32 chestEntry = Get<uint32>(object_fields::VisibleItem1_0 + player_equipment_slots::Chest * kVisFields);
			if (chestEntry == static_cast<uint32>(data.id))
			{
				m_chestSubclass = data.itemSubclass;
			}
```

- [ ] **Step 4: Reset the cache where the animations are reset**

At `src/shared/game_client/game_player_c.cpp:497`, where `SetWeaponAttackAnimations({});` and `SetOffhandWeaponAttackAnimations({});` are called, add below them:

```cpp
		m_mainHandSubclass = 0;
		m_offHandSubclass = 0;
		m_chestSubclass = 0;
```

- [ ] **Step 5: Build**

Run:

```bash
cmake --build build --config Debug -t game_client
```

Expected: builds clean, no warnings about the new members.

- [ ] **Step 6: Commit**

```bash
git add src/shared/game_client/game_unit_c.h src/shared/game_client/game_player_c.h src/shared/game_client/game_player_c.cpp
git commit -m "feat: cache equipped item subclasses for combat sound resolution"
```

---

### Task 6: Drive the sounds from the attack packet

**Files:**
- Modify: `src/mmo_client/game_states/world_state.h` (include + member near `m_soundEntryPlayer` at line 678)
- Modify: `src/mmo_client/game_states/world_state.cpp` (constructor init list around line 284; `OnAttackerStateUpdate` at line 3981)

**Interfaces:**
- Consumes: `CombatSoundPlayer`, `CombatSoundAttacker`, `CombatSoundVictim`, `CombatSwingOutcome` from Tasks 2 and 4; `GetWeaponItemSubclass` / `GetChestItemSubclass` from Task 5.
- Produces: nothing consumed by later tasks.

No unit test: `WorldState` needs the full client. Verification is a build plus an in-game check.

- [ ] **Step 1: Add the member**

In `src/mmo_client/game_states/world_state.h`, add near the other `game_client` includes:

```cpp
#include "game_client/combat_sound_player.h"
```

and directly below the existing `SoundEntryPlayer &m_soundEntryPlayer;` member:

```cpp
		/// Plays auto attack audio resolved from the equipped weapon and the victim's material.
		CombatSoundPlayer m_combatSoundPlayer;
```

- [ ] **Step 2: Construct it**

In `src/mmo_client/game_states/world_state.cpp`, in the constructor initializer list, directly after `, m_soundEntryPlayer(soundEntryPlayer)`:

```cpp
		, m_combatSoundPlayer(m_project, soundEntryPlayer)
```

`m_project` is declared before `m_soundEntryPlayer` in the class, so it is already initialized at this point. If the compiler warns about initialization order, move the member declaration in the header to sit after both, rather than reordering the list.

- [ ] **Step 3: Add the context builders**

In `src/mmo_client/game_states/world_state.cpp`, in the anonymous namespace at the top of the file (create one directly below the `namespace mmo\n{` opening brace if none exists), add:

```cpp
	namespace
	{
		/// Builds the attacker side of the combat sound context from a live unit.
		CombatSoundAttacker makeCombatAttacker(const GameUnitC& unit, const bool offhand)
		{
			CombatSoundAttacker attacker;
			attacker.weaponSubclass = unit.GetWeaponItemSubclass(offhand);
			attacker.displayId = unit.Get<uint32>(object_fields::DisplayId);
			return attacker;
		}

		/// Builds the victim side of the combat sound context from a live unit.
		CombatSoundVictim makeCombatVictim(const GameUnitC& unit)
		{
			CombatSoundVictim victim;
			victim.mainHandSubclass = unit.GetWeaponItemSubclass(false);
			victim.offHandSubclass = unit.GetWeaponItemSubclass(true);
			victim.chestSubclass = unit.GetChestItemSubclass();
			victim.displayId = unit.Get<uint32>(object_fields::DisplayId);
			return victim;
		}
	}
```

- [ ] **Step 4: Play the swing sound**

In `OnAttackerStateUpdate`, replace this block:

```cpp
		std::shared_ptr<GameUnitC> attacker = ObjectMgr::Get<GameUnitC>(attackerGuid);
		const bool offhandSwing = (hitInfo & hit_info::LeftSwing) != 0;
		bool attackAnimationStarted = false;
		if (attacker)
		{
			// LeftSwing marks an off-hand (dual wield) swing so the dedicated off-hand attack
			// animation is played instead of the main-hand one.
			attackAnimationStarted = attacker->NotifyAttackSwingEvent(offhandSwing);
		}
```

with:

```cpp
		std::shared_ptr<GameUnitC> attacker = ObjectMgr::Get<GameUnitC>(attackerGuid);
		const bool offhandSwing = (hitInfo & hit_info::LeftSwing) != 0;
		const bool isCriticalSwing = (hitInfo & hit_info::CriticalHit) != 0;
		bool attackAnimationStarted = false;
		if (attacker)
		{
			// LeftSwing marks an off-hand (dual wield) swing so the dedicated off-hand attack
			// animation is played instead of the main-hand one.
			attackAnimationStarted = attacker->NotifyAttackSwingEvent(offhandSwing);

			// The swing itself is audible right away; everything that depends on the weapon
			// connecting is queued on the SwingHit notify further down.
			m_combatSoundPlayer.PlaySwing(makeCombatAttacker(*attacker, offhandSwing),
				attackerGuid, attacker->GetPosition(), isCriticalSwing);
		}
```

- [ ] **Step 5: Queue the impact sounds for every swing, not just the local player's**

Still in `OnAttackerStateUpdate`, directly after the `if (attacked) { attacked->NotifyHitEvent(); }` block and **before** the `// Build the damage display callback.` comment, insert:

```cpp
		// Combat audio runs for every swing in range, not only the local player's fights, so
		// it deliberately sits outside the isActivePlayerInvolved gate below. Out-of-range
		// sounds are culled by each SoundEntry's max distance.
		if (attacker && attacked)
		{
			CombatSwingOutcome outcome;
			outcome.crit = isCriticalSwing;

			// victimState is checked before the generic Miss flag: the server sets
			// hit_info::Miss for dodges and parries too, as a "no damage landed" marker.
			if ((hitInfo & hit_info::Immune) == 0 && victimState != victim_state::IsImmune)
			{
				if (victimState == victim_state::Dodge)
				{
					outcome.miss = true;
				}
				else if (victimState == victim_state::Parry)
				{
					outcome.parry = true;
				}
				else if (victimState == victim_state::Blocks)
				{
					outcome.block = true;
					outcome.fullBlock = totalDamage == 0 && (hitInfo & hit_info::Absorb) == 0;
					outcome.landed = !outcome.fullBlock;
				}
				else if (hitInfo & hit_info::Miss)
				{
					outcome.miss = true;
				}
				else
				{
					// A fully absorbed hit deals no damage but still connects.
					outcome.landed = true;
				}
			}

			auto soundCallback = [this,
				attackerContext = makeCombatAttacker(*attacker, offhandSwing),
				victimContext = makeCombatVictim(*attacked),
				outcome, attackedGuid, victimPos = attacked->GetPosition()]()
			{
				m_combatSoundPlayer.PlayOutcome(attackerContext, victimContext, outcome, attackedGuid, victimPos);
			};

			if (attackAnimationStarted)
			{
				attacker->QueueSwingHitCallback(std::move(soundCallback));
			}
			else
			{
				soundCallback();
			}
		}
```

- [ ] **Step 6: Build the client**

Run:

```bash
cmake --build build --config Debug -t mmo_client
```

Expected: builds clean.

- [ ] **Step 7: Run the unit suites**

Run:

```bash
cd build && ctest -C Debug --output-on-failure
```

Expected: all suites pass. Nothing here changes server behaviour, so no E2E run is required.

- [ ] **Step 8: Commit**

```bash
git add src/mmo_client/game_states/world_state.h src/mmo_client/game_states/world_state.cpp
git commit -m "feat: play auto attack sounds from the attacker state update"
```

---

### Task 7: Editor authoring UI

**Files:**
- Modify: `src/mmo_edit/editor_windows/item_subclass_editor_window.h` (filter members)
- Modify: `src/mmo_edit/editor_windows/item_subclass_editor_window.cpp` (new section after the off-hand animations section, at the end of `DrawDetailsImpl`)
- Modify: `src/mmo_edit/editor_windows/model_editor_window.h` (filter members near line 55)
- Modify: `src/mmo_edit/editor_windows/model_editor_window.cpp` (new section after the `"Audio"` section, around line 763)

**Interfaces:**
- Consumes: the proto fields from Task 1. Uses the existing `DrawSoundEntryCombo` (`editor_windows/sound_entry_combo.h:21`) and `DrawSurfaceTypeCombo` (`editor_windows/surface_type_combo.h:21`).
- Produces: nothing consumed by later tasks.

No unit test: ImGui windows are not unit-testable here. Verification is an editor build plus a manual click-through.

- [ ] **Step 1: Add the filter members to the item subclass window**

In `src/mmo_edit/editor_windows/item_subclass_editor_window.h`, add the include:

```cpp
#include <imgui.h>
```

and in the `private:` section next to `EditorHost& m_host;`:

```cpp
		ImGuiTextFilter m_swingSoundFilter;
		ImGuiTextFilter m_critSoundFilter;
		ImGuiTextFilter m_missSoundFilter;
		ImGuiTextFilter m_parrySoundFilter;
		ImGuiTextFilter m_blockSoundFilter;
		ImGuiTextFilter m_hitMaterialFilter;
		ImGuiTextFilter m_impactSoundFilter;
		ImGuiTextFilter m_impactMaterialFilter;
```

- [ ] **Step 2: Add the Combat Sounds section to the item subclass window**

In `src/mmo_edit/editor_windows/item_subclass_editor_window.cpp`, add the includes below the existing ones:

```cpp
#include "sound_entry_combo.h"
#include "surface_type_combo.h"
```

and append this section at the end of `DrawDetailsImpl`, after the `"Off-Hand Attack Animations"` section's closing brace:

```cpp
		if (const auto section = ScopedEditorSection("Combat Sounds", ImGuiTreeNodeFlags_None))
		{
			ImGui::TextDisabled("Auto attack audio. Weapon subclasses fill the swing / impact / crit / miss\nfields, shields fill Block Sound, armor subclasses fill Hit Material.");

			DrawSoundEntryCombo(m_project.sounds, "Swing Sound", currentEntry.swing_sound(), m_swingSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_swing_sound(id); });
			ImGui::TextDisabled("Whoosh played when a swing with this weapon starts. Empty = the attacker model's natural weapon swing.");

			DrawSoundEntryCombo(m_project.sounds, "Crit Layer Sound", currentEntry.crit_layer_sound(), m_critSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_crit_layer_sound(id); });
			ImGui::TextDisabled("Played on top of the impact sound on a critical hit.");

			DrawSoundEntryCombo(m_project.sounds, "Miss Sound", currentEntry.miss_sound(), m_missSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_miss_sound(id); });
			ImGui::TextDisabled("Whiff played when a swing with this weapon misses or is dodged.");

			DrawSoundEntryCombo(m_project.sounds, "Parry Sound", currentEntry.parry_sound(), m_parrySoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_parry_sound(id); });
			ImGui::TextDisabled("Played when a weapon of this subclass parries an incoming swing.");

			DrawSoundEntryCombo(m_project.sounds, "Block Sound", currentEntry.block_sound(), m_blockSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_block_sound(id); });
			ImGui::TextDisabled("Shield subclasses: played when a shield of this subclass blocks a swing.");

			DrawSurfaceTypeCombo(m_project.surfaceTypes, "Hit Material", currentEntry.hit_material(), m_hitMaterialFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_hit_material(id); });
			ImGui::TextDisabled("Armor subclasses: what this armor sounds like when struck. Overrides the wearer's body material.");

			ImGui::Separator();
			ImGui::TextDisabled("Impact sounds by struck material. A row with material \"(Any)\" is the fallback\nused when no other row matches.");

			int removeImpactIndex = -1;
			for (int i = 0; i < currentEntry.impact_sounds_size(); ++i)
			{
				ImGui::PushID(2000 + i);

				auto* impact = currentEntry.mutable_impact_sounds(i);

				DrawSurfaceTypeCombo(m_project.surfaceTypes, "##impactmaterial", impact->target_material(), m_impactMaterialFilter,
					[impact](const uint32 id) { impact->set_target_material(id); }, "(Any)");

				ImGui::SameLine();

				DrawSoundEntryCombo(m_project.sounds, "##impactsound", impact->sound(), m_impactSoundFilter,
					[impact](const uint32 id) { impact->set_sound(id); });

				ImGui::SameLine();
				if (ImGui::Button("Remove"))
				{
					removeImpactIndex = i;
				}

				ImGui::PopID();
			}

			if (removeImpactIndex >= 0)
			{
				currentEntry.mutable_impact_sounds()->DeleteSubrange(removeImpactIndex, 1);
			}

			if (ImGui::Button("Add Impact Sound"))
			{
				currentEntry.add_impact_sounds();
			}
		}
```

- [ ] **Step 3: Add the filter members to the model editor window**

In `src/mmo_edit/editor_windows/model_editor_window.h`, below the existing `ImGuiTextFilter m_goodbyeSoundFilter;`:

```cpp
		ImGuiTextFilter m_naturalSwingSoundFilter;
		ImGuiTextFilter m_naturalCritSoundFilter;
		ImGuiTextFilter m_naturalMissSoundFilter;
		ImGuiTextFilter m_naturalImpactSoundFilter;
		ImGuiTextFilter m_naturalImpactMaterialFilter;
		ImGuiTextFilter m_bodyMaterialFilter;
		ImGuiTextFilter m_attackVoiceFilter;
		ImGuiTextFilter m_attackCritVoiceFilter;
		ImGuiTextFilter m_hitVoiceFilter;
		ImGuiTextFilter m_critHitVoiceFilter;
```

- [ ] **Step 4: Add the Combat Sounds section to the model editor window**

In `src/mmo_edit/editor_windows/model_editor_window.cpp`, add the include if it is not already there:

```cpp
#include "surface_type_combo.h"
```

and append this section directly after the existing `"Audio"` section's closing brace, still inside `DrawDetailsImpl`:

```cpp
		if (const auto section = ScopedEditorSection("Combat Sounds", ImGuiTreeNodeFlags_None))
		{
			auto* combatSounds = currentEntry.mutable_combat_sounds();

			// Each sub-group gets its own id scope: an ImGui widget carrying the same label
			// as its enclosing header swallows clicks.
			ImGui::PushID("natural_weapon");
			ImGui::TextDisabled("Natural weapon - used for creatures and for unarmed players wearing this model.");

			DrawSoundEntryCombo(m_project.sounds, "Natural Swing Sound", combatSounds->swing_sound(), m_naturalSwingSoundFilter,
				[combatSounds](const uint32 id) { combatSounds->set_swing_sound(id); });

			DrawSoundEntryCombo(m_project.sounds, "Natural Crit Layer Sound", combatSounds->crit_layer_sound(), m_naturalCritSoundFilter,
				[combatSounds](const uint32 id) { combatSounds->set_crit_layer_sound(id); });

			DrawSoundEntryCombo(m_project.sounds, "Natural Miss Sound", combatSounds->miss_sound(), m_naturalMissSoundFilter,
				[combatSounds](const uint32 id) { combatSounds->set_miss_sound(id); });

			ImGui::TextDisabled("Impact sounds by struck material. A row with material \"(Any)\" is the fallback.");

			int removeImpactIndex = -1;
			for (int i = 0; i < combatSounds->impact_sounds_size(); ++i)
			{
				ImGui::PushID(i);

				auto* impact = combatSounds->mutable_impact_sounds(i);

				DrawSurfaceTypeCombo(m_project.surfaceTypes, "##impactmaterial", impact->target_material(), m_naturalImpactMaterialFilter,
					[impact](const uint32 id) { impact->set_target_material(id); }, "(Any)");

				ImGui::SameLine();

				DrawSoundEntryCombo(m_project.sounds, "##impactsound", impact->sound(), m_naturalImpactSoundFilter,
					[impact](const uint32 id) { impact->set_sound(id); });

				ImGui::SameLine();
				if (ImGui::Button("Remove"))
				{
					removeImpactIndex = i;
				}

				ImGui::PopID();
			}

			if (removeImpactIndex >= 0)
			{
				combatSounds->mutable_impact_sounds()->DeleteSubrange(removeImpactIndex, 1);
			}

			if (ImGui::Button("Add Natural Impact Sound"))
			{
				combatSounds->add_impact_sounds();
			}
			ImGui::PopID();

			ImGui::Separator();

			ImGui::PushID("body_material");
			DrawSurfaceTypeCombo(m_project.surfaceTypes, "Body Material", combatSounds->hit_material(), m_bodyMaterialFilter,
				[combatSounds](const uint32 id) { combatSounds->set_hit_material(id); });
			ImGui::TextDisabled("What this body sounds like when struck. Worn chest armor overrides it.");
			ImGui::PopID();

			ImGui::Separator();

			ImGui::PushID("voice");
			ImGui::TextDisabled("Combat voice. Chance is rolled per event; a unit never talks over itself.");

			DrawSoundEntryCombo(m_project.sounds, "Attack Voice", combatSounds->attack_voice_sound(), m_attackVoiceFilter,
				[combatSounds](const uint32 id) { combatSounds->set_attack_voice_sound(id); });
			int attackVoiceChance = static_cast<int>(combatSounds->attack_voice_chance());
			if (ImGui::SliderInt("Attack Voice Chance", &attackVoiceChance, 0, 100))
			{
				combatSounds->set_attack_voice_chance(static_cast<uint32>(attackVoiceChance));
			}

			DrawSoundEntryCombo(m_project.sounds, "Attack Crit Voice", combatSounds->attack_crit_voice_sound(), m_attackCritVoiceFilter,
				[combatSounds](const uint32 id) { combatSounds->set_attack_crit_voice_sound(id); });
			int attackCritVoiceChance = static_cast<int>(combatSounds->attack_crit_voice_chance());
			if (ImGui::SliderInt("Attack Crit Voice Chance", &attackCritVoiceChance, 0, 100))
			{
				combatSounds->set_attack_crit_voice_chance(static_cast<uint32>(attackCritVoiceChance));
			}

			DrawSoundEntryCombo(m_project.sounds, "Hit Voice", combatSounds->hit_voice_sound(), m_hitVoiceFilter,
				[combatSounds](const uint32 id) { combatSounds->set_hit_voice_sound(id); });
			int hitVoiceChance = static_cast<int>(combatSounds->hit_voice_chance());
			if (ImGui::SliderInt("Hit Voice Chance", &hitVoiceChance, 0, 100))
			{
				combatSounds->set_hit_voice_chance(static_cast<uint32>(hitVoiceChance));
			}

			DrawSoundEntryCombo(m_project.sounds, "Crit Hit Voice", combatSounds->crit_hit_voice_sound(), m_critHitVoiceFilter,
				[combatSounds](const uint32 id) { combatSounds->set_crit_hit_voice_sound(id); });
			int critHitVoiceChance = static_cast<int>(combatSounds->crit_hit_voice_chance());
			if (ImGui::SliderInt("Crit Hit Voice Chance", &critHitVoiceChance, 0, 100))
			{
				combatSounds->set_crit_hit_voice_chance(static_cast<uint32>(critHitVoiceChance));
			}

			int voiceInterval = static_cast<int>(combatSounds->voice_min_interval_ms());
			if (ImGui::InputInt("Voice Min Interval (ms)", &voiceInterval))
			{
				combatSounds->set_voice_min_interval_ms(static_cast<uint32>(std::max(0, voiceInterval)));
			}
			ImGui::TextDisabled("Minimum silence between two voice lines. 0 = derive it from the clip's length.");
			ImGui::PopID();
		}
```

- [ ] **Step 5: Build the editor**

Run:

```bash
cmake --build build --config Debug -t mmo_edit
```

Expected: builds clean. If `std::max` is unresolved, add `#include <algorithm>` to the top of `model_editor_window.cpp`.

- [ ] **Step 6: Verify by hand in the editor**

Launch `bin/Debug/mmo_edit.exe`, then:

1. Open the Surface Type editor and add a row named `Flesh` and one named `Plate`. Save.
2. Open the Item Subclass editor, pick a weapon subclass, expand **Combat Sounds**, set a Swing Sound, add two impact rows (one `(Any)`, one `Plate`), and confirm both combos open, filter, and store their picks.
3. Open the Model editor, pick any model, expand **Combat Sounds**, and confirm all three sub-groups draw, the sliders move, and no widget swallows clicks (the known ImGui id trap — if a combo will not open, a `PushID` scope is missing).
4. Save the project, close the editor, reopen it, and confirm every value round-trips.

Expected: all four steps behave as described.

- [ ] **Step 7: Commit**

```bash
git add src/mmo_edit/editor_windows/item_subclass_editor_window.h src/mmo_edit/editor_windows/item_subclass_editor_window.cpp src/mmo_edit/editor_windows/model_editor_window.h src/mmo_edit/editor_windows/model_editor_window.cpp
git commit -m "feat: editor UI for auto attack combat sounds"
```

---

## Final verification

- [ ] **Run the full unit suite**

```bash
cmake --build build --config Debug -t all_tests && cd build && ctest -C Debug --output-on-failure
```

Expected: every suite passes.

- [ ] **In-game check**

Author a minimal content slice in the editor (one `SoundEntry` per slot is enough — the existing generated SFX under `Sound/` work), export client data, then start the dev stack and attack a creature. Confirm: a swing whoosh at the start of the animation, an impact sound landing on the frame the weapon connects (not before), a different impact after swapping to a different weapon subclass, a whiff on a miss, and the voice lines firing at their authored chance without overlapping.

- [ ] **Gate**

Run `/gate`, which runs `tools/gate/verify.ps1` (Debug build + unit tests + E2E) and then reviews the branch diff. The E2E stack is unaffected by this change but still has to stay green.
