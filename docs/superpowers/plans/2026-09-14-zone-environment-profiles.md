# Zone Environment Profiles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every zone gets a data-driven day-cycle environment profile (sky, sun/moon, ambient, fog, shafts, exposure, bloom) that the client applies to the player's zone with timed border fades. The world editor previews the same result live.

**Architecture:** Profiles live in a new `environment_profiles` game-data table (proto_data + client_data mirror). A graphics-independent `EnvironmentController` in `scene_graph` works out which profile applies, evaluates its curves at the shared clock's time and blends profiles over time. The output is a flat `EnvironmentState`. `SkyComponent` loses its curve files and only applies that state. The client and the world editor run the same per-frame order.

**Tech Stack:** C++17, protobuf 2 (proto2 syntax), Catch2, ImGui, D3D11 deferred renderer.

Spec: [docs/superpowers/specs/2026-09-14-zone-environment-profiles-design.md](../specs/2026-09-14-zone-environment-profiles-design.md)

## Global Constraints

- Branch: all work stays on `feature/volumetric-atmosphere` (user decision). Never push. Never run `/code-review ultra`.
- No exceptions. Use `ASSERT`/`WLOG`/`ELOG` from `base/macros.h` / `log/default_log_levels.h`.
- Allman braces; braces on every `if`; tabs; `m_camelCase` members; `PascalCase` methods; `camelCase` locals.
- New source files start with `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`; headers use `#pragma once` and Doxygen `///` on public members.
- Proto field numbers are part of the ClientDB contract. The `proto_data` and `client_data` copies must have identical numbers, and numbers must never be reused or renumbered.
- `data/editor` / `data/client` changes: commit inside the submodule only. Never bump the superproject submodule pointers.
- Commit messages end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- **Re-run CMake configure after adding files:** `scene_graph`, `proto_data`, `client_data` and the test suites glob their directories, so new files are only picked up after `cmake -S . -B build`.
- **Protobuf descriptors:** no executable may link both `proto_data` and `client_data` (identical `.proto` file names collide in the protobuf descriptor pool). Tests that need generated types use `proto_client` only, via `client_data`.
- Removed cvars: `gxFogDensity`, `gxFogHeightFalloff`, `gxFogBaseHeight`, `gxFogAnisotropy`, `gxShaftStrength`, `gxBloomIntensity`, `gxBloomThreshold`. `gxExposure` stays as the player Brightness multiplier (Options slider 0.5–2.0, default 1.0).
- Profile defaults: fog_density 0.004, fog_height_falloff 0.05, fog_base_height -10, fog_anisotropy 0.7, shaft_strength 1.25, exposure 1, bloom_intensity 0.08, bloom_threshold 0.8, transition_seconds 3.

## Build and test commands

```powershell
cmake -S . -B build                                             # after adding files
cmake --build build --config Debug -t client_data_tests scene_graph_tests
bin/Debug/client_data_tests.exe "[environment_profiles]"
bin/Debug/scene_graph_tests.exe "[environment]"
cmake --build build --config Debug -t mmo_client mmo_edit       # integration tasks
```

A running dev stack locks `bin/Debug/*_server.exe`; building only the targets above avoids LNK1168.

## File map

| File | Responsibility |
|---|---|
| `src/shared/proto_data/environment_profiles.proto` (new) | Authoring schema |
| `src/shared/client_data/environment_profiles.proto` (new) | Client mirror, identical field numbers |
| `src/shared/{proto_data,client_data}/zones.proto` | `environment_profile = 12` |
| `src/shared/{proto_data,client_data}/maps.proto` | `environment_profile = 9` |
| `src/shared/{proto_data,client_data}/project.h` | Register `environmentProfiles` |
| `src/mmo_edit/main_window.cpp` | Export to Client list |
| `src/tests/client_data_tests/test_environment_profiles.cpp` (new) | Schema round trip + default parity |
| `src/shared/scene_graph/environment_profile.h/.cpp` (new) | Runtime profile + built-in Default |
| `src/shared/scene_graph/environment_profile_proto.h` (new) | Templates: proto ↔ runtime, id resolution, profile cache |
| `src/shared/scene_graph/environment_state.h/.cpp` (new) | Flat output, evaluate, lerp |
| `src/shared/scene_graph/environment_controller.h/.cpp` (new) | Weighted timed blending |
| `src/tests/scene_graph_tests/test_environment_*.cpp` (new) | Unit tests |
| `src/shared/graphics/sky_component.h/.cpp` | Drop curves; `ApplyEnvironment` |
| `src/shared/terrain/terrain.h/.cpp` | `TryGetArea` |
| `src/mmo_client/game_states/world_state.h/.cpp` | Controller, zone resolution, cvar removal |
| `src/mmo_edit/environment_preview.h` (new) | Editor-wide preview override + change counter |
| `src/mmo_edit/editor_windows/environment_profile_editor_window.h/.cpp` (new) | Profile authoring window |
| `src/mmo_edit/editor_windows/environment_profile_combo.h/.cpp` (new) | Shared profile picker combo |
| `src/mmo_edit/editor_windows/editor_entry_window_base.h` | `CanRemoveEntry` hook |
| `src/mmo_edit/editor_windows/{zone,map}_editor_window.h/.cpp` | Environment combos |
| `src/mmo_edit/editors/world_editor/world_editor_instance.h/.cpp` | Controller + zone/preview targeting |
| `src/mmo_edit/editors/world_editor/world_settings_panel.cpp` | Remove fog sliders |
| `src/mmo_edit/mmo_edit.cpp` | Register the window |
| `docs/console_commands.md`, `docs/rendering-atmosphere.md` | Cvar and model docs |

---

### Task 1: Environment profile schema and registration

**Files:**
- Create: `src/shared/proto_data/environment_profiles.proto`
- Create: `src/shared/client_data/environment_profiles.proto`
- Modify: `src/shared/proto_data/zones.proto`, `src/shared/client_data/zones.proto` (ZoneEntry)
- Modify: `src/shared/proto_data/maps.proto`, `src/shared/client_data/maps.proto` (MapEntry)
- Modify: `src/shared/proto_data/project.h` (include ~line 65, typedef ~121, member ~215, load ~348, save ~437)
- Modify: `src/shared/client_data/project.h` (include line 36, typedef 67, member 117, load 186, save 241)
- Modify: `src/mmo_edit/main_window.cpp:991` (`sharedManagers`)
- Test: `src/tests/client_data_tests/test_environment_profiles.cpp`

**Interfaces:**
- Produces:
  - Generated types `mmo::proto::EnvironmentProfile`, `mmo::proto::EnvironmentProfiles`, `mmo::proto::ColorCurveData`, `mmo::proto::ColorCurveKey`, and the same names in `mmo::proto_client`.
  - Accessors `environment_profile()` on `ZoneEntry` and `MapEntry`.
  - `proto::Project::environmentProfiles` / `proto_client::Project::environmentProfiles`, typedef `EnvironmentProfileManager` in both namespaces.

- [ ] **Step 1: Write the failing test**

Create `src/tests/client_data_tests/test_environment_profiles.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "shared/client_data/proto_client/environment_profiles.pb.h"
#include "shared/client_data/proto_client/maps.pb.h"
#include "shared/client_data/proto_client/zones.pb.h"
#include "scene_graph/atmosphere_settings.h"
#include "deferred_shading/bloom_settings.h"
#include "deferred_shading/tonemap_settings.h"

using namespace mmo;

TEST_CASE("EnvironmentProfile_Defaults_Match_Engine_Defaults", "[environment_profiles]")
{
	// An unauthored profile must render exactly like the engine defaults, so the proto defaults
	// and the runtime structs can never drift apart silently.
	const proto_client::EnvironmentProfile profile;
	const AtmosphereParameters atmosphere;
	const BloomSettings bloom;
	const TonemapSettings tonemap;

	CHECK(profile.fog_density() == Approx(atmosphere.density));
	CHECK(profile.fog_height_falloff() == Approx(atmosphere.heightFalloff));
	CHECK(profile.fog_base_height() == Approx(atmosphere.baseHeight));
	CHECK(profile.fog_anisotropy() == Approx(atmosphere.anisotropy));
	CHECK(profile.shaft_strength() == Approx(atmosphere.shaftStrength));
	CHECK(profile.exposure() == Approx(tonemap.exposure));
	CHECK(profile.bloom_intensity() == Approx(bloom.intensity));
	CHECK(profile.bloom_threshold() == Approx(bloom.threshold));
	CHECK(profile.transition_seconds() == Approx(3.0f));
}

TEST_CASE("EnvironmentProfile_Round_Trips_Curves_And_Values", "[environment_profiles]")
{
	proto_client::EnvironmentProfiles profiles;
	proto_client::EnvironmentProfile* swamp = profiles.add_entry();
	swamp->set_id(7);
	swamp->set_name("Swamp");
	swamp->set_fog_density(0.02f);
	swamp->set_transition_seconds(5.0f);

	proto_client::ColorCurveKey* key = swamp->mutable_fog()->add_key();
	key->set_time(0.75f);
	key->set_r(0.2f);
	key->set_g(0.5f);
	key->set_b(0.1f);
	key->set_a(2.0f);
	key->set_tangent_mode(1);
	for (int i = 0; i < 4; ++i)
	{
		key->add_in_tangent(0.5f);
		key->add_out_tangent(-0.5f);
	}

	std::string bytes;
	REQUIRE(profiles.SerializeToString(&bytes));

	proto_client::EnvironmentProfiles parsed;
	REQUIRE(parsed.ParseFromString(bytes));
	REQUIRE(parsed.entry_size() == 1);

	const proto_client::EnvironmentProfile& got = parsed.entry(0);
	CHECK(got.id() == 7u);
	CHECK(got.name() == "Swamp");
	CHECK(got.fog_density() == Approx(0.02f));
	CHECK(got.transition_seconds() == Approx(5.0f));
	CHECK_FALSE(got.has_sky_horizon());
	REQUIRE(got.fog().key_size() == 1);
	CHECK(got.fog().key(0).time() == Approx(0.75f));
	CHECK(got.fog().key(0).a() == Approx(2.0f));
	CHECK(got.fog().key(0).tangent_mode() == 1u);
	CHECK(got.fog().key(0).in_tangent_size() == 4);
	CHECK(got.fog().key(0).out_tangent(3) == Approx(-0.5f));
}

TEST_CASE("Zone_And_Map_Environment_Profile_Default_To_Zero", "[environment_profiles]")
{
	// 0 means "inherit" on a zone and "built-in Default" on a map.
	proto_client::ZoneEntry zone;
	proto_client::MapEntry map;
	CHECK(zone.environment_profile() == 0u);
	CHECK(map.environment_profile() == 0u);

	zone.set_environment_profile(3);
	map.set_environment_profile(4);
	CHECK(zone.environment_profile() == 3u);
	CHECK(map.environment_profile() == 4u);
}

TEST_CASE("ColorCurveKey_Alpha_Defaults_To_One", "[environment_profiles]")
{
	const proto_client::ColorCurveKey key;
	CHECK(key.a() == Approx(1.0f));
	CHECK(key.tangent_mode() == 0u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S . -B build; cmake --build build --config Debug -t client_data_tests`
Expected: compile error, `environment_profiles.pb.h` not found.

- [ ] **Step 3: Create the authoring schema**

Create `src/shared/proto_data/environment_profiles.proto`:

```proto
syntax = "proto2";
package mmo.proto;

// Per-zone lighting and mood: day-cycle curves plus fixed fog, shaft, exposure and bloom values.
//
// NOTE: Keep this file in sync with src/shared/client_data/environment_profiles.proto!
// The client reads the exported binary data with the mirrored proto_client schema, so every
// field number in here has to match the mirror exactly. Field numbers are part of the ClientDB
// contract and must never be reused or renumbered.
//
// Nothing on the server consumes these values; they exist here because mmo_edit authors the
// proto_data project, and the client mirror is what ClientDB export produces.

// One key of a colour curve over the normalized day (0 = midnight, 0.5 = noon).
message ColorCurveKey
{
	required float time = 1;
	required float r = 2;
	required float g = 3;
	required float b = 4;
	optional float a = 5 [default = 1];

	// Exactly 4 values when tangent_mode is 1 (user tangents); empty for auto tangents.
	repeated float in_tangent = 6 [packed = true];
	repeated float out_tangent = 7 [packed = true];

	// ColorKey::tangentMode: 0 = auto, 1 = user.
	optional uint32 tangent_mode = 8;
}

message ColorCurveData
{
	repeated ColorCurveKey key = 1;
}

message EnvironmentProfile
{
	required uint32 id = 1;
	required string name = 2;

	// Day curves. An empty curve means "use the built-in Default curve", never black.
	optional ColorCurveData sky_horizon = 3;
	optional ColorCurveData sky_zenith = 4;
	optional ColorCurveData clouds = 5;
	optional ColorCurveData ambient = 6;
	optional ColorCurveData sun = 7;           // rgb colour, a intensity
	optional ColorCurveData moon = 8;          // rgb colour, a intensity
	optional ColorCurveData fog = 9;           // rgb fog tint, a density multiplier
	optional ColorCurveData sun_scatter = 10;  // rgb shaft tint, a shaft multiplier

	// Fixed values.
	optional float fog_density = 11 [default = 0.004];
	optional float fog_height_falloff = 12 [default = 0.05];
	optional float fog_base_height = 13 [default = -10];
	optional float fog_anisotropy = 14 [default = 0.7];
	optional float shaft_strength = 15 [default = 1.25];
	optional float exposure = 16 [default = 1];
	optional float bloom_intensity = 17 [default = 0.08];
	optional float bloom_threshold = 18 [default = 0.8];

	// Fade time in seconds when blending INTO this profile. 0 snaps.
	optional float transition_seconds = 19 [default = 3];

	// Fields 20 and up are reserved for wind-driven volumetric fog.
}

message EnvironmentProfiles
{
	repeated EnvironmentProfile entry = 1;
}
```

- [ ] **Step 4: Create the client mirror**

Create `src/shared/client_data/environment_profiles.proto`. It is byte-identical to Step 3 except for these two lines:
- `package mmo.proto_client;`
- The NOTE comment's first line reads `// NOTE: Keep this file in sync with src/shared/proto_data/environment_profiles.proto!`

- [ ] **Step 5: Add the zone and map fields (all four files)**

In both `src/shared/proto_data/zones.proto` and `src/shared/client_data/zones.proto`, inside `message ZoneEntry`, directly after `optional bool inherit_parent_audio = 11 [default = true];`:

```proto
	// Environment profile id for this zone. 0 = inherit from the parent zone, then the map's
	// default profile, then the built-in Default.
	optional uint32 environment_profile = 12;
```

In both `src/shared/proto_data/maps.proto` and `src/shared/client_data/maps.proto`, inside `message MapEntry`, as the last field:

```proto
	// Environment profile used where no zone sets one. 0 = built-in Default.
	optional uint32 environment_profile = 9;
```

- [ ] **Step 6: Register the table in `src/shared/client_data/project.h`**

Add after line 36:
```cpp
#include "shared/client_data/proto_client/environment_profiles.pb.h"
```
Add after line 67:
```cpp
		typedef TemplateManager<mmo::proto_client::EnvironmentProfiles, mmo::proto_client::EnvironmentProfile> EnvironmentProfileManager;
```
Add after the `WaterProfileManager waterProfiles;` member:
```cpp

			/// Per-zone lighting and mood profiles (sky, sun/moon, fog, shafts, exposure, bloom).
			EnvironmentProfileManager environmentProfiles;
```
In `load()`, after the `water_profiles` entry:
```cpp
				managers.push_back(ManagerEntry("environment_profiles", environmentProfiles, true));
```
In `save()`, after the `water_profiles` entry:
```cpp
				managers.emplace_back("environment_profiles", "environment_profiles", environmentProfiles);
```

- [ ] **Step 7: Register the table in `src/shared/proto_data/project.h`**

Each addition goes next to the matching `water_profiles` line, following that file's syntax:
```cpp
#include "shared/proto_data/environment_profiles.pb.h"
```
```cpp
		typedef TemplateManager<mmo::proto::EnvironmentProfiles, mmo::proto::EnvironmentProfile> EnvironmentProfileManager;
```
```cpp
			/// Per-zone lighting and mood profiles. Authored here and consumed by the client and
			/// the world editor viewport.
			EnvironmentProfileManager environmentProfiles;
```
```cpp
				managers.push_back(ManagerEntry("environment_profiles", environmentProfiles, true));
```
```cpp
				managers.push_back(ManagerEntry("environment_profiles", "environment_profiles", environmentProfiles));
```

- [ ] **Step 8: Add the table to Export to Client**

In `src/mmo_edit/main_window.cpp`, `sharedManagers`, after `"water_profiles",`:
```cpp
			"environment_profiles",
```

- [ ] **Step 9: Run the tests to verify they pass**

Run: `cmake -S . -B build; cmake --build build --config Debug -t client_data_tests; bin/Debug/client_data_tests.exe "[environment_profiles]"`
Expected: `All tests passed (4 test cases)`.

- [ ] **Step 10: Build the editor to confirm both project.h files compile**

Run: `cmake --build build --config Debug -t mmo_edit mmo_client`
Expected: build succeeds.

- [ ] **Step 11: Commit**

```bash
git add src/shared/proto_data/environment_profiles.proto src/shared/client_data/environment_profiles.proto src/shared/proto_data/zones.proto src/shared/client_data/zones.proto src/shared/proto_data/maps.proto src/shared/client_data/maps.proto src/shared/proto_data/project.h src/shared/client_data/project.h src/mmo_edit/main_window.cpp src/tests/client_data_tests/test_environment_profiles.cpp
git commit -m "feat(data): environment profile table, zone and map profile fields

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Runtime profile, built-in Default, and proto templates

**Files:**
- Create: `src/shared/scene_graph/environment_profile.h`
- Create: `src/shared/scene_graph/environment_profile.cpp`
- Create: `src/shared/scene_graph/environment_profile_proto.h`
- Test: `src/tests/scene_graph_tests/test_environment_profile.cpp`

**Interfaces:**
- Consumes (Task 1): `proto_client::EnvironmentProfile`, `proto_client::ColorCurveData`, `ZoneEntry::environment_profile()`, `MapEntry::environment_profile()`, `proto_client::EnvironmentProfileManager`, `proto_client::ZoneManager`, `proto_client::MapManager`.
- Produces:
  - `ColorCurve MakeEnvironmentCurve(std::initializer_list<std::pair<float, Vector4>> keys)`
  - `struct EnvironmentProfile` with `ColorCurve skyHorizon, skyZenith, clouds, ambient, sun, moon, fog, sunScatter; AtmosphereParameters atmosphere; float exposure, bloomIntensity, bloomThreshold, transitionSeconds;`
  - `static EnvironmentProfile EnvironmentProfile::MakeDefault()`
  - `static const std::shared_ptr<const EnvironmentProfile>& EnvironmentProfile::GetDefault()`
  - `template<class TCurveData> bool LoadColorCurve(const TCurveData&, ColorCurve& outCurve)`
  - `template<class TCurveData> void StoreColorCurve(const ColorCurve&, TCurveData& outData)`
  - `template<class TProfile> EnvironmentProfile LoadEnvironmentProfile(const TProfile&)`
  - `template<class TZones, class TMaps> uint32 ResolveEnvironmentProfileId(const TZones&, const TMaps&, uint32 zoneId, uint32 mapId)`
  - `template<class TProfiles> class EnvironmentProfileCache` with `std::shared_ptr<const EnvironmentProfile> Get(uint32 id)` and `void Clear()`

- [ ] **Step 1: Write the failing tests**

Create `src/tests/scene_graph_tests/test_environment_profile.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/project.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"

using namespace mmo;

namespace
{
	void CheckColor(const Vector4& actual, const Vector4& expected)
	{
		CHECK(actual.x == Approx(expected.x).margin(1e-4));
		CHECK(actual.y == Approx(expected.y).margin(1e-4));
		CHECK(actual.z == Approx(expected.z).margin(1e-4));
		CHECK(actual.w == Approx(expected.w).margin(1e-4));
	}

	proto_client::ColorCurveKey* AddKey(proto_client::ColorCurveData& data, const float time, const float r, const float g, const float b, const float a)
	{
		proto_client::ColorCurveKey* key = data.add_key();
		key->set_time(time);
		key->set_r(r);
		key->set_g(g);
		key->set_b(b);
		key->set_a(a);
		return key;
	}
}

TEST_CASE("Default environment reproduces the legacy sky curves at their keys", "[environment]")
{
	const EnvironmentProfile profile = EnvironmentProfile::MakeDefault();

	// Seeded from the shipped Models/*.hccv files.
	CheckColor(profile.skyHorizon.Evaluate(0.2975f), Vector4(0.9964f, 0.6f, 0.2f, 1.0f));
	CheckColor(profile.skyZenith.Evaluate(0.4321f), Vector4(0.0431f, 0.0863f, 0.2039f, 1.0f));
	CheckColor(profile.ambient.Evaluate(0.5003f), Vector4(0.0293f, 0.0412f, 0.06f, 0.999f));
	CheckColor(profile.clouds.Evaluate(0.5003f), Vector4(0.9479f, 0.9479f, 0.9479f, 1.0f));

	// Seeded from the SkyComponent fallback keys (no files ship for these two).
	CheckColor(profile.fog.Evaluate(0.5f), Vector4(0.55f, 0.7f, 0.9f, 1.0f));
	CheckColor(profile.sunScatter.Evaluate(0.23f), Vector4(1.0f, 0.42f, 0.18f, 0.8f));

	// Formerly hardcoded light colours, now constant curves.
	CheckColor(profile.sun.Evaluate(0.37f), Vector4(1.0f, 0.95f, 0.9f, 1.0f));
	CheckColor(profile.moon.Evaluate(0.91f), Vector4(0.3f, 0.4f, 0.65f, 0.12f));

	const AtmosphereParameters atmosphere;
	CHECK(profile.atmosphere.density == Approx(atmosphere.density));
	CHECK(profile.exposure == Approx(1.0f));
	CHECK(profile.bloomIntensity == Approx(0.08f));
	CHECK(profile.bloomThreshold == Approx(0.8f));
	CHECK(profile.transitionSeconds == Approx(3.0f));
}

TEST_CASE("Default environment is shared and never null", "[environment]")
{
	const auto& a = EnvironmentProfile::GetDefault();
	const auto& b = EnvironmentProfile::GetDefault();
	REQUIRE(a != nullptr);
	CHECK(a.get() == b.get());
}

TEST_CASE("Loading a profile keeps authored curves and fills empty ones from Default", "[environment]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(2);
	record.set_name("Swamp");
	AddKey(*record.mutable_fog(), 0.0f, 0.1f, 0.4f, 0.1f, 3.0f);
	AddKey(*record.mutable_fog(), 1.0f, 0.1f, 0.4f, 0.1f, 3.0f);
	record.set_fog_density(0.02f);
	record.set_exposure(1.5f);
	record.set_transition_seconds(6.0f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	const EnvironmentProfile defaults = EnvironmentProfile::MakeDefault();

	CheckColor(profile.fog.Evaluate(0.5f), Vector4(0.1f, 0.4f, 0.1f, 3.0f));
	CheckColor(profile.skyHorizon.Evaluate(0.2975f), defaults.skyHorizon.Evaluate(0.2975f));
	CHECK(profile.atmosphere.density == Approx(0.02f));
	CHECK(profile.atmosphere.heightFalloff == Approx(0.05f));
	CHECK(profile.exposure == Approx(1.5f));
	CHECK(profile.transitionSeconds == Approx(6.0f));
}

TEST_CASE("Loading a profile clamps out-of-range fixed values", "[environment]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(3);
	record.set_name("Broken");
	record.set_fog_density(-1.0f);
	record.set_fog_anisotropy(2.0f);
	record.set_exposure(100.0f);
	record.set_bloom_intensity(5.0f);
	record.set_transition_seconds(-2.0f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	CHECK(profile.atmosphere.density == Approx(0.0f));
	CHECK(profile.atmosphere.anisotropy == Approx(0.95f));
	CHECK(profile.exposure == Approx(8.0f));
	CHECK(profile.bloomIntensity == Approx(1.0f));
	CHECK(profile.transitionSeconds == Approx(0.0f));
}

TEST_CASE("User tangents survive a store and load round trip, malformed ones fall back to auto", "[environment]")
{
	ColorCurve curve = MakeEnvironmentCurve({ { 0.0f, Vector4(0.0f, 0.0f, 0.0f, 1.0f) }, { 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f) } });
	ColorKey userKey = curve.GetKey(0);
	userKey.inTangent = Vector4(2.0f, 2.0f, 2.0f, 0.0f);
	userKey.outTangent = Vector4(2.0f, 2.0f, 2.0f, 0.0f);
	userKey.tangentMode = 1;
	curve.UpdateKey(0, userKey);

	proto_client::ColorCurveData data;
	StoreColorCurve(curve, data);
	REQUIRE(data.key_size() == 2);
	CHECK(data.key(0).in_tangent_size() == 4);
	CHECK(data.key(1).in_tangent_size() == 0);

	ColorCurve loaded;
	REQUIRE(LoadColorCurve(data, loaded));
	CHECK(loaded.GetKey(0).tangentMode == 1);
	CHECK(loaded.GetKey(0).outTangent.x == Approx(2.0f));

	// Three tangent values are malformed: the key must load with auto tangents.
	data.mutable_key(0)->mutable_in_tangent()->RemoveLast();
	ColorCurve fallback;
	REQUIRE(LoadColorCurve(data, fallback));
	CHECK(fallback.GetKey(0).tangentMode == 0);
}

TEST_CASE("An empty curve record leaves the target curve untouched", "[environment]")
{
	const proto_client::ColorCurveData empty;
	ColorCurve curve = MakeEnvironmentCurve({ { 0.0f, Vector4(0.5f, 0.5f, 0.5f, 1.0f) } });
	CHECK_FALSE(LoadColorCurve(empty, curve));
	CheckColor(curve.Evaluate(0.3f), Vector4(0.5f, 0.5f, 0.5f, 1.0f));
}

TEST_CASE("Profile id resolves zone, parent chain, map default, then Default", "[environment]")
{
	proto_client::ZoneManager zones;
	proto_client::MapManager maps;

	proto_client::MapEntry* map = maps.add(1);
	map->set_name("Map");
	map->set_directory("Map");
	map->set_environment_profile(40);

	proto_client::ZoneEntry* root = zones.add(10);
	root->set_name("Root");
	root->set_environment_profile(20);

	proto_client::ZoneEntry* child = zones.add(11);
	child->set_name("Child");
	child->set_parentzone(10);

	proto_client::ZoneEntry* own = zones.add(12);
	own->set_name("Own");
	own->set_parentzone(10);
	own->set_environment_profile(30);

	proto_client::ZoneEntry* orphan = zones.add(13);
	orphan->set_name("Orphan");

	CHECK(ResolveEnvironmentProfileId(zones, maps, 12, 1) == 30u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 11, 1) == 20u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 13, 1) == 40u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 0, 1) == 40u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 999, 1) == 40u);
	CHECK(ResolveEnvironmentProfileId(zones, maps, 13, 2) == 0u);
}

TEST_CASE("Profile id resolution survives a parent cycle", "[environment]")
{
	proto_client::ZoneManager zones;
	proto_client::MapManager maps;

	proto_client::ZoneEntry* a = zones.add(1);
	a->set_name("A");
	a->set_parentzone(2);
	proto_client::ZoneEntry* b = zones.add(2);
	b->set_name("B");
	b->set_parentzone(1);

	CHECK(ResolveEnvironmentProfileId(zones, maps, 1, 0) == 0u);
}

TEST_CASE("Profile cache maps unknown ids and id 0 to the shared Default", "[environment]")
{
	proto_client::EnvironmentProfileManager profiles;
	proto_client::EnvironmentProfile* dusk = profiles.add(5);
	dusk->set_name("Dusk");
	dusk->set_exposure(0.5f);

	EnvironmentProfileCache<proto_client::EnvironmentProfileManager> cache(profiles);
	CHECK(cache.Get(0).get() == EnvironmentProfile::GetDefault().get());
	CHECK(cache.Get(77).get() == EnvironmentProfile::GetDefault().get());

	const auto first = cache.Get(5);
	REQUIRE(first != nullptr);
	CHECK(first->exposure == Approx(0.5f));
	CHECK(cache.Get(5).get() == first.get());

	// Clear drops cached conversions so edited records are converted again.
	dusk->set_exposure(2.0f);
	cache.Clear();
	CHECK(cache.Get(5)->exposure == Approx(2.0f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake -S . -B build; cmake --build build --config Debug -t scene_graph_tests`
Expected: compile error, `scene_graph/environment_profile.h` not found.

- [ ] **Step 3: Write `environment_profile.h`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "graphics/color_curve.h"
#include "math/vector4.h"
#include "scene_graph/atmosphere_settings.h"

#include <initializer_list>
#include <memory>
#include <utility>

namespace mmo
{
	/// @brief Builds a colour curve with auto tangents from (time, colour) pairs.
	/// @param keys Keys over the normalized day, 0 = midnight, 0.5 = noon.
	/// @return The curve. An empty list yields an empty curve.
	[[nodiscard]] ColorCurve MakeEnvironmentCurve(std::initializer_list<std::pair<float, Vector4>> keys);

	/// @brief The lighting and mood of one zone over the day, ready to evaluate.
	/// @remark Every curve is always populated: loading fills unauthored curves from the Default.
	struct EnvironmentProfile
	{
		/// @brief Sky colour at the horizon.
		ColorCurve skyHorizon;

		/// @brief Sky colour overhead.
		ColorCurve skyZenith;

		/// @brief Cloud tint.
		ColorCurve clouds;

		/// @brief Scene ambient colour (rgb; alpha unused).
		ColorCurve ambient;

		/// @brief Sun colour (rgb) and intensity (a).
		ColorCurve sun;

		/// @brief Moon colour (rgb) and intensity (a).
		ColorCurve moon;

		/// @brief Fog tint (rgb) and density multiplier (a).
		ColorCurve fog;

		/// @brief Sun colour inside the fog (rgb) and light shaft multiplier (a).
		ColorCurve sunScatter;

		/// @brief Base fog and shaft values the fog and sunScatter alphas multiply.
		AtmosphereParameters atmosphere;

		/// @brief Linear exposure before tone mapping, [0.1, 8].
		float exposure = 1.0f;

		/// @brief Bloom weight, [0, 1].
		float bloomIntensity = 0.08f;

		/// @brief Bloom soft threshold, [0, 16].
		float bloomThreshold = 0.8f;

		/// @brief Seconds a fade INTO this profile takes. 0 snaps.
		float transitionSeconds = 3.0f;

		/// @brief Builds the built-in Default: the look the world had before profiles existed.
		[[nodiscard]] static EnvironmentProfile MakeDefault();

		/// @brief The shared, immutable built-in Default.
		[[nodiscard]] static const std::shared_ptr<const EnvironmentProfile>& GetDefault();
	};
}
```

- [ ] **Step 4: Write `environment_profile.cpp`**

The horizon, zenith, ambient and cloud keys are the contents of the shipped `data/client/Models/*.hccv` files. The fog and sun scatter keys are today's `SkyComponent` fallbacks. The sun and moon colours are today's hardcoded values.

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_profile.h"

namespace mmo
{
	ColorCurve MakeEnvironmentCurve(const std::initializer_list<std::pair<float, Vector4>> keys)
	{
		ColorCurve curve;
		curve.Clear();

		for (const auto& [time, color] : keys)
		{
			curve.AddKey(time, color);
		}

		curve.CalculateTangents();
		return curve;
	}

	EnvironmentProfile EnvironmentProfile::MakeDefault()
	{
		EnvironmentProfile profile;

		// Seeded from Models/HorizonColor.hccv.
		profile.skyHorizon = MakeEnvironmentCurve({
			{ 0.1501f, Vector4(0.0076f, 0.0082f, 0.0120f, 1.0f) },
			{ 0.2468f, Vector4(0.4745f, 0.1373f, 0.1137f, 1.0f) },
			{ 0.2975f, Vector4(0.9964f, 0.6000f, 0.2000f, 1.0f) },
			{ 0.3762f, Vector4(0.9964f, 0.6039f, 0.3000f, 1.0f) },
			{ 0.4426f, Vector4(0.6980f, 0.9451f, 1.0000f, 1.0f) },
			{ 0.5074f, Vector4(0.4834f, 0.8898f, 1.0000f, 1.0f) },
			{ 0.8072f, Vector4(0.0464f, 0.1636f, 0.2796f, 1.0f) },
			{ 0.9688f, Vector4(0.0078f, 0.0078f, 0.0118f, 1.0f) },
		});

		// Seeded from Models/ZenithColor.hccv.
		profile.skyZenith = MakeEnvironmentCurve({
			{ 0.0000f, Vector4(0.0000f, 0.0000f, 0.0000f, 1.0f) },
			{ 0.0533f, Vector4(0.0000f, 0.0000f, 0.0000f, 1.0f) },
			{ 0.1882f, Vector4(0.0784f, 0.0627f, 0.0784f, 1.0f) },
			{ 0.2337f, Vector4(0.2235f, 0.0980f, 0.0941f, 1.0f) },
			{ 0.2856f, Vector4(0.2902f, 0.1412f, 0.1176f, 1.0f) },
			{ 0.3453f, Vector4(0.0980f, 0.1373f, 0.1804f, 1.0f) },
			{ 0.4321f, Vector4(0.0431f, 0.0863f, 0.2039f, 1.0f) },
			{ 0.7243f, Vector4(0.0235f, 0.0471f, 0.1216f, 1.0f) },
			{ 0.9893f, Vector4(0.0000f, 0.0000f, 0.0000f, 1.0f) },
		});

		// Seeded from Models/AmbientColor.hccv.
		profile.ambient = MakeEnvironmentCurve({
			{ 0.1029f, Vector4(0.0140f, 0.0140f, 0.0140f, 0.9973f) },
			{ 0.1707f, Vector4(0.0237f, 0.0163f, 0.0094f, 1.0000f) },
			{ 0.2159f, Vector4(0.0427f, 0.0345f, 0.0224f, 1.0000f) },
			{ 0.3617f, Vector4(0.0319f, 0.0360f, 0.0427f, 0.9973f) },
			{ 0.5003f, Vector4(0.0293f, 0.0412f, 0.0600f, 0.9990f) },
			{ 1.0000f, Vector4(0.0157f, 0.0157f, 0.0157f, 0.9961f) },
		});

		// Seeded from Models/CloudColor.hccv.
		profile.clouds = MakeEnvironmentCurve({
			{ 0.1356f, Vector4(0.0550f, 0.0581f, 0.0853f, 1.0f) },
			{ 0.2046f, Vector4(0.6351f, 0.2746f, 0.0632f, 1.0f) },
			{ 0.5003f, Vector4(0.9479f, 0.9479f, 0.9479f, 1.0f) },
			{ 1.0000f, Vector4(0.0549f, 0.0588f, 0.0863f, 1.0f) },
		});

		// Formerly hardcoded in SkyComponent::UpdateLighting. SkyComponent still blends sun and
		// moon by the sun's height.
		profile.sun = MakeEnvironmentCurve({ { 0.0f, Vector4(1.0f, 0.95f, 0.9f, 1.0f) }, { 1.0f, Vector4(1.0f, 0.95f, 0.9f, 1.0f) } });
		profile.moon = MakeEnvironmentCurve({ { 0.0f, Vector4(0.3f, 0.4f, 0.65f, 0.12f) }, { 1.0f, Vector4(0.3f, 0.4f, 0.65f, 0.12f) } });

		// rgb = fog ambient radiance, a = density multiplier.
		profile.fog = MakeEnvironmentCurve({
			{ 0.0f, Vector4(0.02f, 0.04f, 0.08f, 1.1f) },   // Night
			{ 0.2f, Vector4(0.05f, 0.06f, 0.1f, 1.2f) },    // Pre-dawn
			{ 0.27f, Vector4(0.85f, 0.6f, 0.45f, 1.5f) },   // Dawn
			{ 0.5f, Vector4(0.55f, 0.7f, 0.9f, 1.0f) },     // Midday
			{ 0.73f, Vector4(0.85f, 0.55f, 0.4f, 1.3f) },   // Dusk
			{ 0.8f, Vector4(0.05f, 0.06f, 0.1f, 1.15f) },   // After dusk
			{ 1.0f, Vector4(0.02f, 0.04f, 0.08f, 1.1f) },   // Night
		});

		// rgb = sun colour inside the fog, a = shaft multiplier. Low-sun keys are strongly saturated
		// on purpose: the shaft core is bright enough for ACES to push it toward white, so a pale
		// tint would read as plain white light.
		profile.sunScatter = MakeEnvironmentCurve({
			{ 0.0f, Vector4(0.3f, 0.4f, 0.65f, 0.3f) },     // Night (moon)
			{ 0.2f, Vector4(0.3f, 0.4f, 0.65f, 0.3f) },     // Pre-dawn (moon)
			{ 0.23f, Vector4(1.0f, 0.42f, 0.18f, 0.8f) },   // Sunrise
			{ 0.3f, Vector4(1.0f, 0.62f, 0.32f, 1.0f) },    // Morning
			{ 0.5f, Vector4(1.0f, 0.93f, 0.8f, 0.35f) },    // Midday
			{ 0.7f, Vector4(1.0f, 0.58f, 0.28f, 1.0f) },    // Evening
			{ 0.77f, Vector4(1.0f, 0.4f, 0.16f, 0.8f) },    // Sunset
			{ 0.8f, Vector4(0.3f, 0.4f, 0.65f, 0.3f) },     // After dusk (moon)
			{ 1.0f, Vector4(0.3f, 0.4f, 0.65f, 0.3f) },     // Night (moon)
		});

		return profile;
	}

	const std::shared_ptr<const EnvironmentProfile>& EnvironmentProfile::GetDefault()
	{
		static const std::shared_ptr<const EnvironmentProfile> s_default = std::make_shared<const EnvironmentProfile>(MakeDefault());
		return s_default;
	}
}
```

- [ ] **Step 5: Write `environment_profile_proto.h`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "scene_graph/environment_profile.h"

#include <algorithm>
#include <map>
#include <memory>

namespace mmo
{
	// Templates rather than functions: the editor works on mmo::proto types and the client on
	// mmo::proto_client types. The generated classes mirror each other field for field, so one
	// template serves both without this header depending on either protobuf library.

	/// @brief Converts an authored curve record into a runtime curve.
	/// @tparam TCurveData proto::ColorCurveData or proto_client::ColorCurveData.
	/// @param data The authored keys.
	/// @param outCurve Receives the curve. Left untouched when the record has no keys.
	/// @return True when the record had keys and replaced outCurve.
	/// @remark User tangents are only honoured with exactly 4 in and 4 out values; anything else
	///         logs a warning and falls back to auto tangents.
	template <typename TCurveData>
	bool LoadColorCurve(const TCurveData& data, ColorCurve& outCurve)
	{
		if (data.key_size() == 0)
		{
			return false;
		}

		ColorCurve curve;
		curve.Clear();

		for (const auto& key : data.key())
		{
			ColorKey colorKey(key.time(), Vector4(key.r(), key.g(), key.b(), key.a()));

			if (key.tangent_mode() != 0)
			{
				if (key.in_tangent_size() == 4 && key.out_tangent_size() == 4)
				{
					colorKey.inTangent = Vector4(key.in_tangent(0), key.in_tangent(1), key.in_tangent(2), key.in_tangent(3));
					colorKey.outTangent = Vector4(key.out_tangent(0), key.out_tangent(1), key.out_tangent(2), key.out_tangent(3));
					colorKey.tangentMode = static_cast<uint8>(key.tangent_mode());
				}
				else
				{
					WLOG("Environment curve key at time " << key.time() << " has malformed user tangents; using auto tangents");
				}
			}

			curve.AddKey(colorKey);
		}

		curve.CalculateTangents();
		outCurve = std::move(curve);
		return true;
	}

	/// @brief Writes a runtime curve into an authored curve record, replacing its keys.
	/// @remark Tangents are stored only for user-tangent keys; auto tangents are recomputed on load.
	template <typename TCurveData>
	void StoreColorCurve(const ColorCurve& curve, TCurveData& outData)
	{
		outData.clear_key();

		for (const ColorKey& colorKey : curve.GetKeys())
		{
			auto* key = outData.add_key();
			key->set_time(colorKey.time);
			key->set_r(colorKey.color.x);
			key->set_g(colorKey.color.y);
			key->set_b(colorKey.color.z);
			key->set_a(colorKey.color.w);

			if (colorKey.tangentMode != 0)
			{
				key->set_tangent_mode(colorKey.tangentMode);
				key->add_in_tangent(colorKey.inTangent.x);
				key->add_in_tangent(colorKey.inTangent.y);
				key->add_in_tangent(colorKey.inTangent.z);
				key->add_in_tangent(colorKey.inTangent.w);
				key->add_out_tangent(colorKey.outTangent.x);
				key->add_out_tangent(colorKey.outTangent.y);
				key->add_out_tangent(colorKey.outTangent.z);
				key->add_out_tangent(colorKey.outTangent.w);
			}
		}
	}

	/// @brief Converts an authored profile into a runtime profile.
	/// @tparam TProfile proto::EnvironmentProfile or proto_client::EnvironmentProfile.
	/// @return The profile. Unauthored curves come from the Default; fixed values are clamped.
	template <typename TProfile>
	[[nodiscard]] EnvironmentProfile LoadEnvironmentProfile(const TProfile& profile)
	{
		EnvironmentProfile result = EnvironmentProfile::MakeDefault();

		LoadColorCurve(profile.sky_horizon(), result.skyHorizon);
		LoadColorCurve(profile.sky_zenith(), result.skyZenith);
		LoadColorCurve(profile.clouds(), result.clouds);
		LoadColorCurve(profile.ambient(), result.ambient);
		LoadColorCurve(profile.sun(), result.sun);
		LoadColorCurve(profile.moon(), result.moon);
		LoadColorCurve(profile.fog(), result.fog);
		LoadColorCurve(profile.sun_scatter(), result.sunScatter);

		// Unset optional fields return their proto defaults, which equal the engine defaults.
		result.atmosphere.SetDensity(profile.fog_density());
		result.atmosphere.SetHeightFalloff(profile.fog_height_falloff());
		result.atmosphere.SetBaseHeight(profile.fog_base_height());
		result.atmosphere.SetAnisotropy(profile.fog_anisotropy());
		result.atmosphere.SetShaftStrength(profile.shaft_strength());

		result.exposure = std::clamp(profile.exposure(), 0.1f, 8.0f);
		result.bloomIntensity = std::clamp(profile.bloom_intensity(), 0.0f, 1.0f);
		result.bloomThreshold = std::clamp(profile.bloom_threshold(), 0.0f, 16.0f);
		result.transitionSeconds = std::clamp(profile.transition_seconds(), 0.0f, 30.0f);

		return result;
	}

	/// @brief Resolves which environment profile applies in a zone of a map.
	/// @tparam TZones A ZoneManager (proto or proto_client).
	/// @tparam TMaps A MapManager (proto or proto_client).
	/// @param zoneId The zone the viewer stands in. 0 or unknown ids skip straight to the map.
	/// @param mapId The current map.
	/// @return The zone's own profile, else the nearest parent's (depth cap 8, so parent cycles
	///         terminate), else the map's default, else 0 for the built-in Default.
	template <typename TZones, typename TMaps>
	[[nodiscard]] uint32 ResolveEnvironmentProfileId(const TZones& zones, const TMaps& maps, const uint32 zoneId, const uint32 mapId)
	{
		const auto* zone = zoneId != 0 ? zones.getById(zoneId) : nullptr;
		for (int depth = 0; zone && depth < 8; ++depth)
		{
			if (zone->environment_profile() != 0)
			{
				return zone->environment_profile();
			}

			if (zone->parentzone() == 0)
			{
				break;
			}

			zone = zones.getById(zone->parentzone());
		}

		if (const auto* map = maps.getById(mapId))
		{
			return map->environment_profile();
		}

		return 0;
	}

	/// @brief Converts authored profiles on first use and hands out shared runtime profiles.
	/// @tparam TProfiles An EnvironmentProfileManager (proto or proto_client).
	/// @remark Id 0 and ids missing from the table resolve to EnvironmentProfile::GetDefault().
	///         Main thread only.
	template <typename TProfiles>
	class EnvironmentProfileCache final
	{
	public:
		/// @brief Creates a cache over a profile table that must outlive the cache.
		explicit EnvironmentProfileCache(const TProfiles& profiles)
			: m_profiles(profiles)
		{
		}

		/// @brief Gets the runtime profile for an id, converting it on first use.
		[[nodiscard]] std::shared_ptr<const EnvironmentProfile> Get(const uint32 id)
		{
			if (id == 0)
			{
				return EnvironmentProfile::GetDefault();
			}

			if (const auto it = m_cache.find(id); it != m_cache.end())
			{
				return it->second;
			}

			const auto* record = m_profiles.getById(id);
			if (!record)
			{
				return EnvironmentProfile::GetDefault();
			}

			auto profile = std::make_shared<const EnvironmentProfile>(LoadEnvironmentProfile(*record));
			m_cache.emplace(id, profile);
			return profile;
		}

		/// @brief Drops every cached conversion, e.g. after the table was edited.
		void Clear()
		{
			m_cache.clear();
		}

	private:
		const TProfiles& m_profiles;
		std::map<uint32, std::shared_ptr<const EnvironmentProfile>> m_cache;
	};
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake -S . -B build; cmake --build build --config Debug -t scene_graph_tests; bin/Debug/scene_graph_tests.exe "[environment]"`
Expected: `All tests passed (9 test cases)`.

- [ ] **Step 7: Commit**

```bash
git add src/shared/scene_graph/environment_profile.h src/shared/scene_graph/environment_profile.cpp src/shared/scene_graph/environment_profile_proto.h src/tests/scene_graph_tests/test_environment_profile.cpp
git commit -m "feat(render): runtime environment profile, built-in Default and proto conversion

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Environment state and the blending controller

**Files:**
- Create: `src/shared/scene_graph/environment_state.h`
- Create: `src/shared/scene_graph/environment_state.cpp`
- Create: `src/shared/scene_graph/environment_controller.h`
- Create: `src/shared/scene_graph/environment_controller.cpp`
- Test: `src/tests/scene_graph_tests/test_environment_controller.cpp`

**Interfaces:**
- Consumes (Task 2): `EnvironmentProfile`, `EnvironmentProfile::GetDefault()`, `MakeEnvironmentCurve`.
- Produces:
  - `struct EnvironmentState` with fields `Vector4 skyHorizon, skyZenith, clouds; Vector3 ambient; Vector3 sunColor; float sunIntensity; Vector3 moonColor; float moonIntensity; AtmosphereParameters atmosphere; AtmosphereTimeOfDay timeOfDay; float exposure, bloomIntensity, bloomThreshold;`
  - `EnvironmentState EvaluateEnvironment(const EnvironmentProfile&, float normalizedTime)`
  - `EnvironmentState LerpEnvironment(const EnvironmentState& a, const EnvironmentState& b, float t)`
  - `class EnvironmentController` with:
    - `void SetTarget(std::shared_ptr<const EnvironmentProfile> profile, bool immediate)`
    - `void Update(float deltaSeconds, float normalizedTime)`
    - `const EnvironmentState& GetState() const`
    - `size_t GetBlendEntryCount() const`
    - `float GetWeight(const EnvironmentProfile* profile) const`
    - `static constexpr size_t MaxBlendEntries = 4`

- [ ] **Step 1: Write the failing tests**

Create `src/tests/scene_graph_tests/test_environment_controller.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/environment_controller.h"
#include "scene_graph/environment_state.h"

#include <memory>

using namespace mmo;

namespace
{
	/// A profile whose every curve is one constant colour and whose exposure is a marker value,
	/// so a blended state reveals the weights that produced it.
	std::shared_ptr<const EnvironmentProfile> MakeFlatProfile(const float value, const float transitionSeconds)
	{
		EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
		const ColorCurve flat = MakeEnvironmentCurve({ { 0.0f, Vector4(value, value, value, value) }, { 1.0f, Vector4(value, value, value, value) } });
		profile.skyHorizon = flat;
		profile.skyZenith = flat;
		profile.clouds = flat;
		profile.ambient = flat;
		profile.sun = flat;
		profile.moon = flat;
		profile.fog = flat;
		profile.sunScatter = flat;
		profile.exposure = value;
		profile.transitionSeconds = transitionSeconds;
		return std::make_shared<const EnvironmentProfile>(std::move(profile));
	}

	float SumOfWeights(const EnvironmentController& controller, std::initializer_list<const EnvironmentProfile*> profiles)
	{
		float sum = 0.0f;
		for (const EnvironmentProfile* profile : profiles)
		{
			sum += controller.GetWeight(profile);
		}
		return sum;
	}
}

TEST_CASE("EvaluateEnvironment copies curve values and fixed values", "[environment]")
{
	const auto profile = MakeFlatProfile(0.25f, 3.0f);
	const EnvironmentState state = EvaluateEnvironment(*profile, 0.4f);

	CHECK(state.skyHorizon.x == Approx(0.25f));
	CHECK(state.ambient.y == Approx(0.25f));
	CHECK(state.sunColor.z == Approx(0.25f));
	CHECK(state.sunIntensity == Approx(0.25f));
	CHECK(state.moonIntensity == Approx(0.25f));
	CHECK(state.timeOfDay.fogTint[0] == Approx(0.25f));
	CHECK(state.timeOfDay.densityMultiplier == Approx(0.25f));
	CHECK(state.timeOfDay.sunScatterColor[2] == Approx(0.25f));
	CHECK(state.timeOfDay.shaftMultiplier == Approx(0.25f));
	CHECK(state.exposure == Approx(0.25f));
	CHECK(state.atmosphere.density == Approx(profile->atmosphere.density));
}

TEST_CASE("LerpEnvironment returns the inputs at its endpoints", "[environment]")
{
	const EnvironmentState a = EvaluateEnvironment(*MakeFlatProfile(0.0f, 3.0f), 0.5f);
	const EnvironmentState b = EvaluateEnvironment(*MakeFlatProfile(1.0f, 3.0f), 0.5f);

	CHECK(LerpEnvironment(a, b, 0.0f).exposure == Approx(0.0f));
	CHECK(LerpEnvironment(a, b, 1.0f).exposure == Approx(1.0f));
	CHECK(LerpEnvironment(a, b, 0.25f).skyZenith.x == Approx(0.25f));
	CHECK(LerpEnvironment(a, b, 0.25f).timeOfDay.fogTint[1] == Approx(0.25f));
}

TEST_CASE("Controller starts on the Default profile", "[environment]")
{
	EnvironmentController controller;
	controller.Update(0.016f, 0.5f);

	const EnvironmentState expected = EvaluateEnvironment(*EnvironmentProfile::GetDefault(), 0.5f);
	CHECK(controller.GetBlendEntryCount() == 1);
	CHECK(controller.GetState().skyHorizon.x == Approx(expected.skyHorizon.x));
}

TEST_CASE("A fade takes the incoming profile's transition time and weights sum to one", "[environment]")
{
	const auto from = MakeFlatProfile(0.0f, 1.0f);
	const auto to = MakeFlatProfile(1.0f, 2.0f);

	EnvironmentController controller;
	controller.SetTarget(from, true);
	controller.SetTarget(to, false);

	controller.Update(0.5f, 0.5f);
	CHECK(controller.GetWeight(to.get()) == Approx(0.25f));
	CHECK(SumOfWeights(controller, { from.get(), to.get() }) == Approx(1.0f));
	CHECK(controller.GetState().exposure == Approx(0.25f));

	controller.Update(1.5f, 0.5f);
	CHECK(controller.GetWeight(to.get()) == Approx(1.0f));
	CHECK(controller.GetBlendEntryCount() == 1);
	CHECK(controller.GetState().exposure == Approx(1.0f));
}

TEST_CASE("Returning to a fading profile continues from its current weight", "[environment]")
{
	const auto a = MakeFlatProfile(0.0f, 4.0f);
	const auto b = MakeFlatProfile(1.0f, 4.0f);

	EnvironmentController controller;
	controller.SetTarget(a, true);
	controller.SetTarget(b, false);
	controller.Update(1.0f, 0.5f);
	REQUIRE(controller.GetWeight(a.get()) == Approx(0.75f));

	controller.SetTarget(a, false);
	CHECK(controller.GetWeight(a.get()) == Approx(0.75f));

	controller.Update(0.5f, 0.5f);
	CHECK(controller.GetWeight(a.get()) == Approx(0.875f));
	CHECK(SumOfWeights(controller, { a.get(), b.get() }) == Approx(1.0f));
}

TEST_CASE("Setting the current target again changes nothing", "[environment]")
{
	const auto a = MakeFlatProfile(0.0f, 4.0f);
	const auto b = MakeFlatProfile(1.0f, 4.0f);

	EnvironmentController controller;
	controller.SetTarget(a, true);
	controller.SetTarget(b, false);
	controller.Update(1.0f, 0.5f);

	controller.SetTarget(b, false);
	CHECK(controller.GetWeight(b.get()) == Approx(0.25f));
	CHECK(controller.GetBlendEntryCount() == 2);
}

TEST_CASE("Immediate and zero-length transitions snap", "[environment]")
{
	const auto a = MakeFlatProfile(0.0f, 4.0f);
	const auto snap = MakeFlatProfile(1.0f, 0.0f);

	EnvironmentController controller;
	controller.SetTarget(a, true);
	CHECK(controller.GetState().exposure == Approx(0.0f));
	CHECK(controller.GetBlendEntryCount() == 1);

	controller.SetTarget(snap, false);
	CHECK(controller.GetBlendEntryCount() == 1);
	CHECK(controller.GetState().exposure == Approx(1.0f));
}

TEST_CASE("The blend list never exceeds its capacity", "[environment]")
{
	EnvironmentController controller;
	std::vector<std::shared_ptr<const EnvironmentProfile>> profiles;
	for (int i = 0; i < 8; ++i)
	{
		profiles.push_back(MakeFlatProfile(static_cast<float>(i) / 8.0f, 10.0f));
		controller.SetTarget(profiles.back(), false);
		controller.Update(0.5f, 0.5f);
		CHECK(controller.GetBlendEntryCount() <= EnvironmentController::MaxBlendEntries);
	}

	// The starting Default keeps the highest weight, so it is never the lowest-weight entry that
	// gets evicted: include it in the sum.
	float sum = controller.GetWeight(EnvironmentProfile::GetDefault().get());
	for (const auto& profile : profiles)
	{
		sum += controller.GetWeight(profile.get());
	}
	CHECK(sum == Approx(1.0f));
}

TEST_CASE("A null target means the Default profile", "[environment]")
{
	const auto a = MakeFlatProfile(0.0f, 4.0f);

	EnvironmentController controller;
	controller.SetTarget(a, true);
	controller.SetTarget(nullptr, true);
	CHECK(controller.GetWeight(EnvironmentProfile::GetDefault().get()) == Approx(1.0f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake -S . -B build; cmake --build build --config Debug -t scene_graph_tests`
Expected: compile error, `scene_graph/environment_controller.h` not found.

- [ ] **Step 3: Write `environment_state.h`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "math/vector3.h"
#include "math/vector4.h"
#include "scene_graph/atmosphere_settings.h"
#include "scene_graph/environment_profile.h"

namespace mmo
{
	/// @brief Everything the sky, the lights, the fog and the post-process chain need for one frame.
	struct EnvironmentState
	{
		/// @brief Sky material horizon colour.
		Vector4 skyHorizon{ 0.5f, 0.7f, 1.0f, 1.0f };

		/// @brief Sky material zenith colour.
		Vector4 skyZenith{ 0.1f, 0.3f, 0.9f, 1.0f };

		/// @brief Sky material cloud colour.
		Vector4 clouds{ 1.0f, 1.0f, 1.0f, 1.0f };

		/// @brief Scene ambient colour.
		Vector3 ambient{ 0.03f, 0.04f, 0.06f };

		/// @brief Sun light colour.
		Vector3 sunColor{ 1.0f, 0.95f, 0.9f };

		/// @brief Sun light intensity.
		float sunIntensity = 1.0f;

		/// @brief Moon light colour.
		Vector3 moonColor{ 0.3f, 0.4f, 0.65f };

		/// @brief Moon light intensity.
		float moonIntensity = 0.12f;

		/// @brief Base fog and shaft values.
		AtmosphereParameters atmosphere;

		/// @brief Time-of-day fog tint, density multiplier, shaft tint and shaft multiplier.
		AtmosphereTimeOfDay timeOfDay;

		/// @brief Exposure before tone mapping (the player's brightness is applied on top).
		float exposure = 1.0f;

		/// @brief Bloom weight.
		float bloomIntensity = 0.08f;

		/// @brief Bloom threshold.
		float bloomThreshold = 0.8f;
	};

	/// @brief Samples a profile at a time of day.
	/// @param profile The profile.
	/// @param normalizedTime 0 = midnight, 0.5 = noon.
	[[nodiscard]] EnvironmentState EvaluateEnvironment(const EnvironmentProfile& profile, float normalizedTime);

	/// @brief Component-wise linear blend. t = 0 returns a, t = 1 returns b.
	[[nodiscard]] EnvironmentState LerpEnvironment(const EnvironmentState& a, const EnvironmentState& b, float t);
}
```

- [ ] **Step 4: Write `environment_state.cpp`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_state.h"

namespace mmo
{
	namespace
	{
		float LerpFloat(const float a, const float b, const float t)
		{
			return a + (b - a) * t;
		}

		Vector3 ToVector3(const Vector4& value)
		{
			return Vector3(value.x, value.y, value.z);
		}
	}

	EnvironmentState EvaluateEnvironment(const EnvironmentProfile& profile, const float normalizedTime)
	{
		EnvironmentState state;

		state.skyHorizon = profile.skyHorizon.Evaluate(normalizedTime);
		state.skyZenith = profile.skyZenith.Evaluate(normalizedTime);
		state.clouds = profile.clouds.Evaluate(normalizedTime);
		state.ambient = ToVector3(profile.ambient.Evaluate(normalizedTime));

		const Vector4 sun = profile.sun.Evaluate(normalizedTime);
		state.sunColor = ToVector3(sun);
		state.sunIntensity = sun.w;

		const Vector4 moon = profile.moon.Evaluate(normalizedTime);
		state.moonColor = ToVector3(moon);
		state.moonIntensity = moon.w;

		const Vector4 fog = profile.fog.Evaluate(normalizedTime);
		state.timeOfDay.fogTint[0] = fog.x;
		state.timeOfDay.fogTint[1] = fog.y;
		state.timeOfDay.fogTint[2] = fog.z;
		state.timeOfDay.densityMultiplier = fog.w;

		const Vector4 scatter = profile.sunScatter.Evaluate(normalizedTime);
		state.timeOfDay.sunScatterColor[0] = scatter.x;
		state.timeOfDay.sunScatterColor[1] = scatter.y;
		state.timeOfDay.sunScatterColor[2] = scatter.z;
		state.timeOfDay.shaftMultiplier = scatter.w;

		state.atmosphere = profile.atmosphere;
		state.exposure = profile.exposure;
		state.bloomIntensity = profile.bloomIntensity;
		state.bloomThreshold = profile.bloomThreshold;

		return state;
	}

	EnvironmentState LerpEnvironment(const EnvironmentState& a, const EnvironmentState& b, const float t)
	{
		EnvironmentState state;

		state.skyHorizon = a.skyHorizon + (b.skyHorizon - a.skyHorizon) * t;
		state.skyZenith = a.skyZenith + (b.skyZenith - a.skyZenith) * t;
		state.clouds = a.clouds + (b.clouds - a.clouds) * t;
		state.ambient = a.ambient + (b.ambient - a.ambient) * t;
		state.sunColor = a.sunColor + (b.sunColor - a.sunColor) * t;
		state.sunIntensity = LerpFloat(a.sunIntensity, b.sunIntensity, t);
		state.moonColor = a.moonColor + (b.moonColor - a.moonColor) * t;
		state.moonIntensity = LerpFloat(a.moonIntensity, b.moonIntensity, t);

		// Both inputs are already inside the setter ranges, so a lerp stays inside them.
		state.atmosphere.density = LerpFloat(a.atmosphere.density, b.atmosphere.density, t);
		state.atmosphere.heightFalloff = LerpFloat(a.atmosphere.heightFalloff, b.atmosphere.heightFalloff, t);
		state.atmosphere.baseHeight = LerpFloat(a.atmosphere.baseHeight, b.atmosphere.baseHeight, t);
		state.atmosphere.anisotropy = LerpFloat(a.atmosphere.anisotropy, b.atmosphere.anisotropy, t);
		state.atmosphere.shaftStrength = LerpFloat(a.atmosphere.shaftStrength, b.atmosphere.shaftStrength, t);

		for (int i = 0; i < 3; ++i)
		{
			state.timeOfDay.fogTint[i] = LerpFloat(a.timeOfDay.fogTint[i], b.timeOfDay.fogTint[i], t);
			state.timeOfDay.sunScatterColor[i] = LerpFloat(a.timeOfDay.sunScatterColor[i], b.timeOfDay.sunScatterColor[i], t);
		}
		state.timeOfDay.densityMultiplier = LerpFloat(a.timeOfDay.densityMultiplier, b.timeOfDay.densityMultiplier, t);
		state.timeOfDay.shaftMultiplier = LerpFloat(a.timeOfDay.shaftMultiplier, b.timeOfDay.shaftMultiplier, t);

		state.exposure = LerpFloat(a.exposure, b.exposure, t);
		state.bloomIntensity = LerpFloat(a.bloomIntensity, b.bloomIntensity, t);
		state.bloomThreshold = LerpFloat(a.bloomThreshold, b.bloomThreshold, t);

		return state;
	}
}
```

- [ ] **Step 5: Write `environment_controller.h`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_state.h"

#include <memory>
#include <vector>

namespace mmo
{
	/// @brief Blends environment profiles over time and produces the state for the current hour.
	/// @remark Pure logic: no graphics device, no signals, no resource managers. Main thread only.
	///         A new target fades in over its own transitionSeconds while every other profile fades
	///         out proportionally, so the weights always sum to one. Every weighted profile is
	///         evaluated at the current time, so the day keeps moving during a fade.
	class EnvironmentController final : public NonCopyable
	{
	public:
		/// @brief Most profiles blended at once. A fifth drops the lowest-weight one.
		static constexpr size_t MaxBlendEntries = 4;

	public:
		/// @brief Starts fully on EnvironmentProfile::GetDefault() at noon.
		EnvironmentController();

		/// @brief Chooses the profile to fade to.
		/// @param profile The profile. Null means the built-in Default.
		/// @param immediate Snap without fading (world enter, teleport, editor selection). A
		///        profile with transitionSeconds <= 0 always snaps.
		void SetTarget(std::shared_ptr<const EnvironmentProfile> profile, bool immediate);

		/// @brief Advances the fade and re-evaluates the state.
		/// @param deltaSeconds Real time since the last update.
		/// @param normalizedTime The shared clock, 0 = midnight, 0.5 = noon.
		void Update(float deltaSeconds, float normalizedTime);

		/// @brief The state produced by the last Update or SetTarget.
		[[nodiscard]] const EnvironmentState& GetState() const { return m_state; }

		/// @brief Number of profiles currently blended.
		[[nodiscard]] size_t GetBlendEntryCount() const { return m_entries.size(); }

		/// @brief Current blend weight of a profile, 0 if it is not blended.
		[[nodiscard]] float GetWeight(const EnvironmentProfile* profile) const;

	private:
		struct Entry
		{
			std::shared_ptr<const EnvironmentProfile> profile;
			float weight = 0.0f;
		};

		/// @brief Rescales every weight so the sum is one.
		void Normalize();

		/// @brief Evaluates and combines every entry at m_normalizedTime.
		void Evaluate();

	private:
		/// @brief Blended profiles. The last entry is always the target.
		std::vector<Entry> m_entries;
		float m_normalizedTime = 0.5f;
		EnvironmentState m_state;
	};
}
```

- [ ] **Step 6: Write `environment_controller.cpp`**

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_controller.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Weights below this are dropped so finished fades stop costing curve evaluations.
		constexpr float MinWeight = 1.0e-4f;
	}

	EnvironmentController::EnvironmentController()
	{
		m_entries.push_back({ EnvironmentProfile::GetDefault(), 1.0f });
		Evaluate();
	}

	void EnvironmentController::SetTarget(std::shared_ptr<const EnvironmentProfile> profile, const bool immediate)
	{
		if (!profile)
		{
			profile = EnvironmentProfile::GetDefault();
		}

		if (immediate || profile->transitionSeconds <= 0.0f)
		{
			m_entries.clear();
			m_entries.push_back({ std::move(profile), 1.0f });
			Evaluate();
			return;
		}

		if (m_entries.back().profile == profile)
		{
			return;
		}

		// A profile that is still fading out keeps its weight and becomes the target again.
		const auto existing = std::find_if(m_entries.begin(), m_entries.end(),
			[&profile](const Entry& entry) { return entry.profile == profile; });

		Entry target{ std::move(profile), 0.0f };
		if (existing != m_entries.end())
		{
			target.weight = existing->weight;
			m_entries.erase(existing);
		}

		m_entries.push_back(std::move(target));

		if (m_entries.size() > MaxBlendEntries)
		{
			// Never drop the target (last entry).
			const auto lowest = std::min_element(m_entries.begin(), m_entries.end() - 1,
				[](const Entry& a, const Entry& b) { return a.weight < b.weight; });
			m_entries.erase(lowest);
			Normalize();
		}
	}

	void EnvironmentController::Update(const float deltaSeconds, const float normalizedTime)
	{
		m_normalizedTime = normalizedTime;

		Entry& target = m_entries.back();
		if (target.weight < 1.0f)
		{
			const float duration = target.profile->transitionSeconds;
			const float newWeight = duration > 0.0f ? std::min(1.0f, target.weight + deltaSeconds / duration) : 1.0f;

			// Shrink every other entry by the same factor so the sum stays one.
			const float othersBefore = 1.0f - target.weight;
			const float othersAfter = 1.0f - newWeight;
			const float scale = othersBefore > 0.0f ? othersAfter / othersBefore : 0.0f;
			for (size_t i = 0; i + 1 < m_entries.size(); ++i)
			{
				m_entries[i].weight *= scale;
			}
			target.weight = newWeight;

			m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end() - 1,
				[](const Entry& entry) { return entry.weight < MinWeight; }), m_entries.end() - 1);

			if (m_entries.size() == 1)
			{
				m_entries.back().weight = 1.0f;
			}
		}

		Evaluate();
	}

	float EnvironmentController::GetWeight(const EnvironmentProfile* profile) const
	{
		for (const Entry& entry : m_entries)
		{
			if (entry.profile.get() == profile)
			{
				return entry.weight;
			}
		}

		return 0.0f;
	}

	void EnvironmentController::Normalize()
	{
		float sum = 0.0f;
		for (const Entry& entry : m_entries)
		{
			sum += entry.weight;
		}

		if (sum <= 0.0f)
		{
			// Only possible when every remaining entry had weight 0: give it all to the target.
			for (Entry& entry : m_entries)
			{
				entry.weight = 0.0f;
			}
			m_entries.back().weight = 1.0f;
			return;
		}

		for (Entry& entry : m_entries)
		{
			entry.weight /= sum;
		}
	}

	void EnvironmentController::Evaluate()
	{
		// Running blend: after folding in entry i the accumulated state is the weighted average of
		// entries 0..i, so the fold weight is w_i / (w_0 + ... + w_i).
		float accumulated = 0.0f;
		bool first = true;

		for (const Entry& entry : m_entries)
		{
			if (entry.weight <= 0.0f)
			{
				continue;
			}

			const EnvironmentState state = EvaluateEnvironment(*entry.profile, m_normalizedTime);
			accumulated += entry.weight;

			if (first)
			{
				m_state = state;
				first = false;
			}
			else
			{
				m_state = LerpEnvironment(m_state, state, entry.weight / accumulated);
			}
		}

		if (first)
		{
			m_state = EvaluateEnvironment(*m_entries.back().profile, m_normalizedTime);
		}
	}
}
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `cmake -S . -B build; cmake --build build --config Debug -t scene_graph_tests; bin/Debug/scene_graph_tests.exe "[environment]"`
Expected: `All tests passed (18 test cases)` (9 from Task 2 + 9 here).

- [ ] **Step 8: Commit**

```bash
git add src/shared/scene_graph/environment_state.h src/shared/scene_graph/environment_state.cpp src/shared/scene_graph/environment_controller.h src/shared/scene_graph/environment_controller.cpp src/tests/scene_graph_tests/test_environment_controller.cpp
git commit -m "feat(render): environment state evaluation and timed profile blending

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: SkyComponent applies the environment state; look cvars removed

After this task the client and the editor render through the controller, on the built-in Default only. Zones come in Task 5 (client) and Task 6 (editor).

**Files:**
- Modify: `src/shared/graphics/sky_component.h`
- Modify: `src/shared/graphics/sky_component.cpp`
- Modify: `src/mmo_client/game_states/world_state.h` (includes ~line 17, handler declarations 275–285, members ~634)
- Modify: `src/mmo_client/game_states/world_state.cpp` (statics 146–157, OnIdle 1017–1031, SetupWorldScene 1453–1457, registration 2108–2144, initial apply 2253–2256, unregister 2290–2298, handlers 6229–6280)
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.h` (member next to `m_skyComponent`, line 559)
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.cpp:516-525`
- Modify: `src/mmo_edit/editors/world_editor/world_settings_panel.cpp:140-198`
- Modify: `docs/console_commands.md:75-78`, `docs/rendering-atmosphere.md`

**Interfaces:**
- Consumes (Task 3): `EnvironmentController`, `EnvironmentState`, `EvaluateEnvironment`, `EnvironmentProfile::GetDefault()`.
- Produces:
  - `void SkyComponent::ApplyEnvironment(const EnvironmentState& state)`.
  - `SkyComponent::Update` no longer touches lighting.
  - `EnvironmentController WorldState::m_environment`, `void WorldState::ApplyEnvironmentToRenderer()`.
  - `EnvironmentController WorldEditorInstance::m_environment`, `void WorldEditorInstance::UpdateEnvironment(float deltaSeconds)`.

This task has no new unit test: `SkyComponent` needs a real scene, sky mesh and material. It is verified by building, the existing suites, and the Task 7 A/B check.

- [ ] **Step 1: Update `sky_component.h`**

Replace `#include "graphics/color_curve.h"` with:
```cpp
#include "scene_graph/environment_state.h"
```

Replace the `LoadColorCurves` declaration block (lines 140–143) with nothing. Add this public method after `SetPosition`:
```cpp
        /**
         * @brief Applies the lighting and mood for this frame and refreshes the lights.
         *
         * Called once per frame after Update, with the state from an EnvironmentController.
         * The sun/moon direction and blend still come from this component's clock.
         *
         * @param state The blended environment state.
         */
        void ApplyEnvironment(const EnvironmentState& state);
```

Replace the six `std::unique_ptr<ColorCurve>` members (lines 164–169) with:
```cpp
        EnvironmentState m_environment;                ///< Last applied environment state
```

Update the `Update` doc comment to: `@brief Advances the clock and rotates the clouds. Lighting is refreshed by ApplyEnvironment.`

- [ ] **Step 2: Update `sky_component.cpp`**

- Remove the includes `assets/asset_registry.h`, `binary_io/stream_source.h`, `binary_io/reader.h`, `graphics/color_curve.h`.
- In the constructor, replace the `LoadColorCurves();` line and its comment with:
```cpp
        // Start from the built-in Default so the first frame is never black, even before a caller
        // applies its first environment state.
        m_environment = EvaluateEnvironment(*EnvironmentProfile::GetDefault(), GetNormalizedTimeOfDay());
```
- Delete `SkyComponent::LoadColorCurves` entirely (lines 66–196).
- In `SkyComponent::Update`, delete the last two lines (`// Update lighting based on time of day` and `UpdateLighting(GetNormalizedTimeOfDay());`).
- Add after `SetPosition`:
```cpp
    void SkyComponent::ApplyEnvironment(const EnvironmentState& state)
    {
        m_environment = state;
        UpdateLighting(GetNormalizedTimeOfDay());
    }
```
- In `UpdateLighting`, replace everything from `// Light color & intensity` (line 360) up to and including `m_scene.SetAmbientColor(...)` (line 402) with:
```cpp
        // Light colour & intensity from the environment; the clock decides how much is sun vs moon.
        const Vector4 sunColor(m_environment.sunColor.x, m_environment.sunColor.y, m_environment.sunColor.z, 1.0f);
        const Vector4 moonColor(m_environment.moonColor.x, m_environment.moonColor.y, m_environment.moonColor.z, 1.0f);

        Vector4 blendedColor = sunColor * blendSun + moonColor * blendMoon;
        float blendedIntensity = m_environment.sunIntensity * blendSun + m_environment.moonIntensity * blendMoon;

        // Apply to shared light
        m_sunLight->SetDirection(lightDir);
        m_sunLight->SetColor(blendedColor);
        m_sunLight->SetIntensity(blendedIntensity);

        // Update light direction in material
        m_skyMatInst->SetVectorParameter("LightDirection", Vector4(lightDir.x, lightDir.y, lightDir.z, 0.0f));
        m_skyMatInst->SetScalarParameter("SunHeight", blendMoon);

        const Vector4& horizonColor = m_environment.skyHorizon;
        const Vector4& zenithColor = m_environment.skyZenith;
        m_skyMatInst->SetVectorParameter("HorizonColor", horizonColor);
        m_skyMatInst->SetVectorParameter("ZenithColor", zenithColor);
        m_skyMatInst->SetVectorParameter("CloudColor", m_environment.clouds);

        m_scene.SetAtmosphereParameters(m_environment.atmosphere);
        m_scene.SetAtmosphereTimeOfDay(m_environment.timeOfDay);
        m_scene.SetAmbientColor(m_environment.ambient);
```
The remaining lines of `UpdateLighting` (primary light, global shader parameters) stay as they are.

- [ ] **Step 3: Update `world_state.h`**

- Add `#include "scene_graph/environment_controller.h"` after `#include "graphics/sky_component.h"`.
- Delete the declarations and doc comments of `OnAtmosphereParametersChanged` (lines 275–276) and `OnExposureChanged` (284–285).
- Change the `OnBloomChanged` comment to `/// @brief Called when gxBloomQuality changed.`
- After `std::unique_ptr<SkyComponent> m_skyComponent;` add:
```cpp

		/// Blends zone environment profiles and produces the per-frame lighting and mood.
		EnvironmentController m_environment;
```
- Next to the other private helpers (e.g. after `GetWorldDeferredRenderer`) add:
```cpp

		/// @brief Pushes the environment's exposure (times the player's gxExposure brightness) and
		///        bloom values to the deferred renderer. Called every frame.
		void ApplyEnvironmentToRenderer();
```

- [ ] **Step 4: Update `world_state.cpp`: statics, registration, handlers**

- Delete the static `ConsoleVar*`s `s_fogDensityVar`, `s_fogHeightFalloffVar`, `s_fogBaseHeightVar`, `s_fogAnisotropyVar`, `s_shaftStrengthVar`, `s_bloomIntensityVar`, `s_bloomThresholdVar` (keep `s_exposureVar`).
- Replace the comment at lines 2108–2109 with:
```cpp
		// Height fog, light shafts and bloom quality. The look itself (fog, shafts, exposure, bloom
		// strength) comes from the zone's environment profile.
```
- Delete the registration blocks of the five fog/shaft cvars (2119–2132) and of `gxBloomIntensity` / `gxBloomThreshold` (2137–2141).
- Replace the `gxExposure` registration (2143–2144) with:
```cpp
		s_exposureVar = ConsoleVarMgr::RegisterConsoleVar("gxExposure", "Player brightness: multiplies the zone's environment exposure before tone mapping (0.5 to 2 in Options).", "1.0");
```
- In the initial-apply block, delete `OnAtmosphereParametersChanged(*s_fogDensityVar, "");` and `OnExposureChanged(*s_exposureVar, "");`.
- In `RemoveGameplayCommands`, delete the unregister lines for `gxFogDensity`, `gxFogHeightFalloff`, `gxFogBaseHeight`, `gxFogAnisotropy`, `gxShaftStrength`, `gxBloomIntensity`, `gxBloomThreshold` (keep `gxExposure`).
- Delete `WorldState::OnAtmosphereParametersChanged` (6229–6246) and `WorldState::OnExposureChanged` (6274–6280).
- Replace the body of `OnBloomChanged` with:
```cpp
		DeferredRenderer* renderer = GetWorldDeferredRenderer();
		if (!renderer || !s_bloomQualityVar)
		{
			return;
		}

		renderer->SetBloomQuality(s_bloomQualityVar->GetIntValue());
```
- Add after `OnBloomChanged`:
```cpp
	void WorldState::ApplyEnvironmentToRenderer()
	{
		DeferredRenderer* renderer = GetWorldDeferredRenderer();
		if (!renderer)
		{
			return;
		}

		const EnvironmentState& state = m_environment.GetState();
		const float brightness = s_exposureVar ? s_exposureVar->GetFloatValue() : 1.0f;

		renderer->SetExposure(state.exposure * brightness);
		renderer->SetBloomIntensity(state.bloomIntensity);
		renderer->SetBloomThreshold(state.bloomThreshold);
	}
```

- [ ] **Step 5: Update `world_state.cpp`: scene setup and frame order**

In `SetupWorldScene`, delete the block:
```cpp
		// A fresh scene starts from AtmosphereParameters defaults; re-apply the player's cvars.
		if (s_fogDensityVar)
		{
			OnAtmosphereParametersChanged(*s_fogDensityVar, "");
		}
```

In `OnIdle`, replace:
```cpp
		// Update sky component (handles day/night cycle and lighting)
		m_skyComponent->Update(deltaSeconds, timestamp);
```
with:
```cpp
		// Day/night clock, then the zone environment for the new hour, then the lights and sky.
		m_skyComponent->Update(deltaSeconds, timestamp);
		m_environment.Update(deltaSeconds, m_skyComponent->GetNormalizedTimeOfDay());
		m_skyComponent->ApplyEnvironment(m_environment.GetState());
		ApplyEnvironmentToRenderer();
```

- [ ] **Step 6: Build the client**

Run: `cmake --build build --config Debug -t mmo_client`
Expected: build succeeds. If an error names one of the removed statics or handlers, a reference in `world_state.cpp` was missed. Search with `Grep` for `s_fogDensityVar|s_bloomIntensityVar|OnAtmosphereParametersChanged|OnExposureChanged` and remove it.

- [ ] **Step 7: Wire the world editor**

In `world_editor_instance.h`, add `#include "scene_graph/environment_controller.h"` after `#include "graphics/sky_component.h"`. Add after `std::unique_ptr<SkyComponent> m_skyComponent;`:
```cpp

		/// Blends environment profiles for the viewport, exactly like the client.
		EnvironmentController m_environment;
```
Declare in the private methods section:
```cpp
		/// @brief Advances the environment blend and applies it to the sky, lights and renderer.
		void UpdateEnvironment(float deltaSeconds);
```

In `world_editor_instance.cpp`, replace the sky block at lines 516–525 with:
```cpp
		// Update sky component for day/night cycle
		if (m_skyComponent)
		{
			m_skyComponent->SetPosition(m_camera->GetDerivedPosition());
			m_skyComponent->Update(deltaTimeSeconds, 0);
			UpdateEnvironment(deltaTimeSeconds);

			// Anchor the fog reference height to the orbit pivot rather than the camera itself, so
			// fog density at a fixed ground point stays stable as the camera zooms or pitches.
			m_scene.SetAtmosphereReferenceHeight(m_cameraAnchor->GetDerivedPosition().y);
		}
```
Add the method (next to `SetMapEntry`):
```cpp
	void WorldEditorInstance::UpdateEnvironment(const float deltaSeconds)
	{
		m_environment.Update(deltaSeconds, m_skyComponent->GetNormalizedTimeOfDay());
		m_skyComponent->ApplyEnvironment(m_environment.GetState());

		if (DeferredRenderer* renderer = GetDeferredRenderer())
		{
			const EnvironmentState& state = m_environment.GetState();
			renderer->SetExposure(state.exposure);
			renderer->SetBloomIntensity(state.bloomIntensity);
			renderer->SetBloomThreshold(state.bloomThreshold);
		}
	}
```

- [ ] **Step 8: Remove the editor fog sliders**

In `world_settings_panel.cpp`, delete the whole `if (showFog) { ... }` block (lines 140–198), keeping the "Show Fog" checkbox above it. Add a tooltip after the checkbox:
```cpp
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Fog, light shafts, exposure and bloom come from the environment profile of the zone under the camera.");
                }
```
If `AtmosphereParameters` is no longer referenced in the file, remove its include if one exists.

- [ ] **Step 9: Update the docs**

In `docs/console_commands.md`, replace lines 75–78 with:
```markdown
- `gxBloomQuality` - Bloom quality (default: 2)
- `gxExposure` - Player brightness, multiplies the zone environment's exposure (default: 1.0)

Fog density, height falloff, base height, anisotropy, shaft strength, exposure and bloom strength are
authored per zone in environment profiles (editor: Environment Profile Editor), not cvars.
```

In `docs/rendering-atmosphere.md`:
- Replace the whole `## Time of day` section with:
```markdown
## Environment profiles

The look comes from environment profiles (`environment_profiles` game-data table, edited in the
editor's Environment Profile Editor). A profile has eight day curves and fixed values:

| Curve | rgb | alpha |
|---|---|---|
| `sky_horizon`, `sky_zenith`, `clouds` | sky material colours | unused |
| `ambient` | scene ambient | unused |
| `sun`, `moon` | light colour | intensity |
| `fog` | fog ambient radiance | density multiplier |
| `sun_scatter` | sun colour inside fog | shaft multiplier |

Fixed values: fog density, height falloff, base height, anisotropy, shaft strength, exposure, bloom
intensity, bloom threshold, transition seconds. An empty curve uses the built-in Default
(`EnvironmentProfile::MakeDefault`), which reproduces the look from before profiles existed.

A zone uses its own profile, else its parent's, else the map's default profile, else the built-in
Default (`ResolveEnvironmentProfileId`). `EnvironmentController` fades to a new profile over that
profile's transition time; `SkyComponent::ApplyEnvironment` pushes the result into the sky, the
sun/moon light, the scene's atmosphere values and the global shader parameters every frame.
```
- In the cvar table, delete the rows `gxFogDensity`, `gxFogHeightFalloff`, `gxFogBaseHeight`, `gxFogAnisotropy`, `gxShaftStrength`, `gxBloomIntensity`, `gxBloomThreshold`. Change the `gxExposure` row's meaning to `player brightness, multiplies the profile exposure`.
- In "Fog model", replace `gxFogDensity · densityMultiplier` with `density · densityMultiplier`. Replace the other cvar names with profile field names (`fog_height_falloff`, `fog_base_height`, `fog_anisotropy`, `shaft_strength`).
- In "Known limitations", delete the first bullet (the fog base following the player until per-zone data exists).

- [ ] **Step 10: Build and run the affected suites**

Run:
```powershell
cmake --build build --config Debug -t mmo_client mmo_edit scene_graph_tests deferred_shading_tests client_data_tests
bin/Debug/scene_graph_tests.exe
bin/Debug/deferred_shading_tests.exe
bin/Debug/client_data_tests.exe
```
Expected: all builds succeed; every suite reports `All tests passed`.

- [ ] **Step 11: Commit**

```bash
git add src/shared/graphics/sky_component.h src/shared/graphics/sky_component.cpp src/mmo_client/game_states/world_state.h src/mmo_client/game_states/world_state.cpp src/mmo_edit/editors/world_editor/world_editor_instance.h src/mmo_edit/editors/world_editor/world_editor_instance.cpp src/mmo_edit/editors/world_editor/world_settings_panel.cpp docs/console_commands.md docs/rendering-atmosphere.md
git commit -m "feat(render): sky and renderer follow the environment controller, look cvars removed

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Client resolves the zone profile, fades at borders, snaps on enter and teleport

**Files:**
- Modify: `src/shared/terrain/terrain.h:460` (declaration after `GetArea`)
- Modify: `src/shared/terrain/terrain.cpp:1851` (definition after `GetArea`)
- Modify: `src/mmo_client/game_states/world_state.h` (members near `m_lastZoneId`, line 622; helper declarations)
- Modify: `src/mmo_client/game_states/world_state.cpp` (`CheckForZoneUpdate` 1241–1319, `SetupWorldScene` 1449+)

**Interfaces:**
- Consumes (Task 2): `ResolveEnvironmentProfileId`, `EnvironmentProfileCache<proto_client::EnvironmentProfileManager>`. From Task 4: `WorldState::m_environment`.
- Produces:
  - `bool terrain::Terrain::TryGetArea(const Vector3& position, uint32& outArea) const`
  - `void WorldState::UpdateEnvironmentTarget(uint32 zoneId, const Vector3& position)`

No new unit test: the terrain suite is disabled (`#if false` in `test_terrain.cpp`) and `WorldState` is not unit-testable. Resolution and blending are covered by Tasks 2–3. The wiring is verified in Task 7 by walking across a zone border.

- [ ] **Step 1: Add `Terrain::TryGetArea`**

In `terrain.h`, after the `GetArea` declaration:
```cpp

			/// @brief Gets the area at a world position, distinguishing "no area" from "not loaded".
			/// @param position The world position.
			/// @param outArea Receives the area id, 0 when none is painted or the position is
			///        outside the terrain.
			/// @return False while the page under the position is not prepared yet; callers should
			///         keep their previous area instead of treating the position as area 0.
			[[nodiscard]] bool TryGetArea(const Vector3 &position, uint32 &outArea) const;
```

In `terrain.cpp`, after `Terrain::GetArea`:
```cpp
		bool Terrain::TryGetArea(const Vector3 &position, uint32 &outArea) const
		{
			outArea = 0;

			int32 tileX, tileY;
			if (!GetTileIndexByWorldPosition(position, tileX, tileY))
			{
				// Outside the terrain there is nothing to stream in: a known "no area".
				return true;
			}

			const Page *page = GetPage(static_cast<uint32>(tileX) / constants::TilesPerPage, static_cast<uint32>(tileY) / constants::TilesPerPage);
			if (!page || !page->IsPrepared())
			{
				return false;
			}

			outArea = page->GetArea(static_cast<uint32>(tileX) % constants::TilesPerPage, static_cast<uint32>(tileY) % constants::TilesPerPage);
			return true;
		}
```

- [ ] **Step 2: Add the client state members**

In `world_state.h`, add `#include "scene_graph/environment_profile_proto.h"` after the `environment_controller.h` include and `#include <optional>` after `#include <map>`.

The cache references `m_project`, which is declared at line 680, below `m_lastZoneId` (line 622). Members initialize in declaration order, so the cache must be declared **after** `const proto_client::Project &m_project;`. Add directly after that line:
```cpp

		/// Runtime environment profiles converted from ClientDB on first use. Declared after
		/// m_project because it binds to m_project's table during construction.
		EnvironmentProfileCache<proto_client::EnvironmentProfileManager> m_environmentProfiles{ m_project.environmentProfiles };
```

After `uint32 m_lastZoneId = UINT32_MAX;` add:
```cpp

		/// Profile id the environment controller is currently targeting. UINT32_MAX = none yet.
		uint32 m_environmentProfileId = UINT32_MAX;

		/// True until the first zone lookup after entering a world, which snaps instead of fading.
		bool m_environmentSnapPending = true;

		/// Controlled unit position at the last environment update, for teleport detection.
		std::optional<Vector3> m_lastEnvironmentPosition;
```
Add the helper declaration next to `CheckForZoneUpdate`:
```cpp

		/// @brief Retargets the environment to the profile of a zone. Snaps on world enter and on
		///        teleports (a move of more than 200 m within one update), fades otherwise.
		/// @param zoneId The zone the controlled unit stands in (0 = none).
		/// @param position The controlled unit's position.
		void UpdateEnvironmentTarget(uint32 zoneId, const Vector3& position);
```

- [ ] **Step 3: Snap to the map default when a world scene is created**

In `SetupWorldScene`, directly after `m_skyComponent = std::make_unique<SkyComponent>(*m_scene, &m_gameTime);`:
```cpp

		// Until the first zone lookup succeeds, show the map's default profile, then snap to the
		// zone's profile on that first lookup instead of fading in from the map default.
		m_environmentSnapPending = true;
		m_lastEnvironmentPosition.reset();
		m_environmentProfileId = ResolveEnvironmentProfileId(m_project.zones, m_project.maps, 0, g_mapId);
		m_environment.SetTarget(m_environmentProfiles.Get(m_environmentProfileId), true);
```

- [ ] **Step 4: Resolve on zone checks**

In `CheckForZoneUpdate`, replace:
```cpp
		const auto pos = unit->GetPosition();
		const uint32 zoneId = m_worldInstance->GetTerrain()->GetArea(pos);
		if (zoneId != m_lastZoneId)
```
with:
```cpp
		const auto pos = unit->GetPosition();
		uint32 zoneId = 0;
		if (!m_worldInstance->GetTerrain()->TryGetArea(pos, zoneId))
		{
			// The page under the player is still streaming in. Keep the current zone, music and
			// environment rather than briefly treating the player as standing in no zone.
			return;
		}

		UpdateEnvironmentTarget(zoneId, pos);

		if (zoneId != m_lastZoneId)
```

Add after `CheckForZoneUpdate`:
```cpp
	void WorldState::UpdateEnvironmentTarget(const uint32 zoneId, const Vector3& position)
	{
		constexpr float TeleportDistance = 200.0f;

		bool immediate = m_environmentSnapPending;
		if (m_lastEnvironmentPosition && (position - *m_lastEnvironmentPosition).GetSquaredLength() > TeleportDistance * TeleportDistance)
		{
			// A jump this large within one update is a teleport; fading across it would show the
			// previous zone's mood at the destination.
			immediate = true;
		}
		m_lastEnvironmentPosition = position;

		const uint32 profileId = ResolveEnvironmentProfileId(m_project.zones, m_project.maps, zoneId, g_mapId);
		if (profileId == m_environmentProfileId && !immediate)
		{
			return;
		}

		m_environmentProfileId = profileId;
		m_environmentSnapPending = false;
		m_environment.SetTarget(m_environmentProfiles.Get(profileId), immediate);
	}
```

- [ ] **Step 5: Build the client**

Run: `cmake --build build --config Debug -t mmo_client terrain`
Expected: build succeeds.

- [ ] **Step 6: Run the unit suites again**

Run: `bin/Debug/scene_graph_tests.exe "[environment]"`
Expected: `All tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/shared/terrain/terrain.h src/shared/terrain/terrain.cpp src/mmo_client/game_states/world_state.h src/mmo_client/game_states/world_state.cpp
git commit -m "feat(client): zone environment profiles with border fades and teleport snaps

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Editor authoring: profile window, zone/map pickers, live world editor preview

**Files:**
- Create: `src/mmo_edit/environment_preview.h`
- Create: `src/mmo_edit/editor_windows/environment_profile_combo.h`
- Create: `src/mmo_edit/editor_windows/environment_profile_combo.cpp`
- Create: `src/mmo_edit/editor_windows/environment_profile_editor_window.h`
- Create: `src/mmo_edit/editor_windows/environment_profile_editor_window.cpp`
- Modify: `src/mmo_edit/editor_windows/editor_entry_window_base.h:40-47, 125-133`
- Modify: `src/mmo_edit/editor_windows/zone_editor_window.h:39`, `zone_editor_window.cpp:206-216`
- Modify: `src/mmo_edit/editor_windows/map_editor_window.h:37`, `map_editor_window.cpp:153`
- Modify: `src/mmo_edit/editors/world_editor/world_editor_instance.h`, `world_editor_instance.cpp` (`UpdateEnvironment` from Task 4)
- Modify: `src/mmo_edit/mmo_edit.cpp:264` (register the window) and its include list

**Interfaces:**
- Consumes:
  - Task 1: `proto::EnvironmentProfile`, `proto::EnvironmentProfileManager`, `proto::Project::environmentProfiles`, `set_environment_profile`.
  - Task 2: `LoadColorCurve`, `StoreColorCurve`, `ResolveEnvironmentProfileId`, `EnvironmentProfileCache<proto::EnvironmentProfileManager>`, `EnvironmentProfile::MakeDefault()`.
  - Task 3: `EnvironmentController::SetTarget`.
  - Task 4: `WorldEditorInstance::UpdateEnvironment`.
  - Task 5: `Terrain::TryGetArea`.
- Produces:
  - `struct EnvironmentPreview { std::optional<uint32> profileId; float normalizedTime; uint64 revision; void NotifyChanged(); }` and `EnvironmentPreview& GetEnvironmentPreview()`.
  - `bool DrawEnvironmentProfileCombo(const proto::EnvironmentProfileManager&, const char* label, uint32 currentId, ImGuiTextFilter& filter, const std::function<void(uint32)>& setter, const char* noneLabel)`.
  - `virtual bool EditorEntryWindowBase::CanRemoveEntry(const T2&) const`.
  - `class EnvironmentProfileEditorWindow`.

This task is editor UI. It has no unit tests; it is verified by building and the Task 7 editor checks.

- [ ] **Step 1: Editor-wide preview state**

Create `src/mmo_edit/environment_preview.h`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <optional>

namespace mmo
{
	/// @brief What the Environment Profile Editor asks every open world editor to show.
	/// @remark Main thread only. World editors poll it each frame.
	struct EnvironmentPreview
	{
		/// @brief When set, world editors snap to this profile instead of the camera zone's.
		std::optional<uint32> profileId;

		/// @brief Time of day shown while profileId is set, 0 = midnight, 0.5 = noon.
		float normalizedTime = 0.5f;

		/// @brief Bumped on every profile, zone or map environment edit so viewers reconvert.
		uint64 revision = 0;

		/// @brief Records that authored environment data changed.
		void NotifyChanged() { ++revision; }
	};

	/// @brief The editor's single preview state.
	inline EnvironmentPreview& GetEnvironmentPreview()
	{
		static EnvironmentPreview s_preview;
		return s_preview;
	}
}
```

- [ ] **Step 2: Let entry windows refuse removal**

In `editor_entry_window_base.h`, after `SupportsDuplicate`:
```cpp

		/// @brief Whether the selected entry may be removed. Windows override this to protect
		///        entries that other data still references.
		virtual bool CanRemoveEntry(const T2& entry) const { return true; }
```
Replace the `Remove Selected` disable condition (line 125):
```cpp
				const bool hasSelection = m_currentItem != -1 && m_currentItem < static_cast<int>(m_manager.count());
				ImGui::BeginDisabled(!hasSelection || !CanRemoveEntry(m_manager.getTemplates().entry().at(m_currentItem)));
```
`CanRemoveEntry` is only evaluated when `hasSelection` is true, because `||` short-circuits.

- [ ] **Step 3: Shared profile combo**

Create `src/mmo_edit/editor_windows/environment_profile_combo.h`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "proto_data/project.h"

#include <functional>

#include <imgui.h>

namespace mmo
{
	/// @brief Draws a filterable combo that picks an environment profile id.
	/// @param profiles The profile table.
	/// @param label The combo label.
	/// @param currentId The selected id, 0 for none.
	/// @param filter Persistent filter state owned by the calling window.
	/// @param setter Receives the chosen id (0 for the none entry).
	/// @param noneLabel Text of the 0 entry, e.g. "(inherit)".
	/// @return True when the selection changed.
	bool DrawEnvironmentProfileCombo(const proto::EnvironmentProfileManager& profiles, const char* label, uint32 currentId,
		ImGuiTextFilter& filter, const std::function<void(uint32)>& setter, const char* noneLabel);
}
```

Create `src/mmo_edit/editor_windows/environment_profile_combo.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_profile_combo.h"

namespace mmo
{
	bool DrawEnvironmentProfileCombo(const proto::EnvironmentProfileManager& profiles, const char* label, const uint32 currentId,
		ImGuiTextFilter& filter, const std::function<void(uint32)>& setter, const char* noneLabel)
	{
		const auto* current = currentId != 0 ? profiles.getById(currentId) : nullptr;

		String preview = noneLabel;
		if (current)
		{
			preview = current->name();
		}
		else if (currentId != 0)
		{
			preview = "(missing #" + std::to_string(currentId) + ")";
		}

		bool changed = false;
		if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLargest))
		{
			filter.Draw("##environment_profile_filter", -1.0f);

			if (ImGui::Selectable(noneLabel, currentId == 0))
			{
				setter(0);
				changed = currentId != 0;
				filter.Clear();
				ImGui::CloseCurrentPopup();
			}

			for (const auto& profile : profiles.getTemplates().entry())
			{
				if (filter.IsActive() && !filter.PassFilter(profile.name().c_str()))
				{
					continue;
				}

				ImGui::PushID(static_cast<int>(profile.id()));
				if (ImGui::Selectable(profile.name().c_str(), profile.id() == currentId))
				{
					setter(profile.id());
					changed = profile.id() != currentId;
					filter.Clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		return changed;
	}
}
```

- [ ] **Step 4: Zone and map pickers**

In `zone_editor_window.h`, add `ImGuiTextFilter m_environmentFilter;` after `m_ambienceSoundFilter`.

In `zone_editor_window.cpp`, add includes `#include "environment_profile_combo.h"` and `#include "environment_preview.h"`. After the `Audio` section's closing brace (line 216) add:
```cpp

		if (const auto section = ScopedEditorSection("Environment", ImGuiTreeNodeFlags_None))
		{
			DrawEnvironmentProfileCombo(m_project.environmentProfiles, "Environment", currentEntry.environment_profile(), m_environmentFilter,
				[&currentEntry](const uint32 id)
				{
					currentEntry.set_environment_profile(id);
					GetEnvironmentPreview().NotifyChanged();
				}, "(inherit)");
			ImGui::TextDisabled("Unset zones use the parent zone's profile, then the map's default environment, then the built-in Default.");
		}
```

In `map_editor_window.h`, add after `m_minimapWorld`:
```cpp

		/// @brief Filter state of the default environment combo.
		ImGuiTextFilter m_environmentFilter;
```

In `map_editor_window.cpp`, add includes `#include "environment_profile_combo.h"` and `#include "environment_preview.h"`. After `DrawHelpMarker("Type of map instance (Global, Dungeon, Raid, etc.)");` (line 153) add:
```cpp

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Environment");

			ImGui::SetNextItemWidth(260);
			DrawEnvironmentProfileCombo(m_project.environmentProfiles, "##DefaultEnvironment", currentEntry.environment_profile(), m_environmentFilter,
				[&currentEntry](const uint32 id)
				{
					currentEntry.set_environment_profile(id);
					GetEnvironmentPreview().NotifyChanged();
				}, "(built-in Default)");
			ImGui::SameLine();
			DrawHelpMarker("Environment profile used wherever no zone (or parent zone) sets one.");
```

- [ ] **Step 5: Profile window header**

Create `src/mmo_edit/editor_windows/environment_profile_editor_window.h`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"
#include "editor_host.h"
#include "editors/color_curve_editor/color_curve_imgui_editor.h"
#include "graphics/color_curve.h"
#include "proto_data/project.h"

#include <array>
#include <memory>

#include <imgui.h>

namespace mmo
{
	/// Allows editing of environment profiles: the day-cycle curves and fixed values that set a
	/// zone's sky, lights, fog, light shafts, exposure and bloom. Offers a live preview in every
	/// open world editor.
	class EnvironmentProfileEditorWindow final
		: public EditorEntryWindowBase<proto::EnvironmentProfiles, proto::EnvironmentProfile>
		, public NonCopyable
	{
	public:
		/// @brief Number of day curves a profile has.
		static constexpr size_t CurveCount = 8;

	public:
		explicit EnvironmentProfileEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~EnvironmentProfileEditorWindow() override = default;

	private:
		void DrawDetailsImpl(proto::EnvironmentProfile& currentEntry) override;

		void OnNewEntry(proto::TemplateManager<proto::EnvironmentProfiles, proto::EnvironmentProfile>::EntryType& entry) override;

		bool CanRemoveEntry(const proto::EnvironmentProfile& entry) const override;

		/// @brief Rebuilds the curve copies and their widgets for the selected profile.
		void BindCurves(const proto::EnvironmentProfile& entry);

		void DrawPreviewBar(const proto::EnvironmentProfile& entry);

		void DrawCurves(proto::EnvironmentProfile& entry);

		void DrawFixedValues(proto::EnvironmentProfile& entry);

		void DrawReferences(const proto::EnvironmentProfile& entry);

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;

		/// Editable copies of the selected profile's curves (Default curves where unauthored).
		std::array<ColorCurve, CurveCount> m_curves;

		/// Widgets bound to m_curves. Recreated whenever a curve is rebound.
		std::array<std::unique_ptr<ColorCurveImGuiEditor>, CurveCount> m_curveEditors;

		/// Id of the profile m_curves belongs to. 0 = nothing bound.
		uint32 m_boundProfileId = 0;
	};
}
```

- [ ] **Step 6: Profile window implementation**

Create `src/mmo_edit/editor_windows/environment_profile_editor_window.cpp`:
```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_profile_editor_window.h"

#include "editor_imgui_helpers.h"
#include "environment_preview.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"

#include <algorithm>

#include <imgui/misc/cpp/imgui_stdlib.h>

namespace mmo
{
	namespace
	{
		/// Binds one day curve slot to its proto accessors and its Default curve.
		struct CurveSlot
		{
			const char* label;
			const char* alphaMeaning;
			bool (proto::EnvironmentProfile::*has)() const;
			const proto::ColorCurveData& (proto::EnvironmentProfile::*get)() const;
			proto::ColorCurveData* (proto::EnvironmentProfile::*mutableGet)();
			void (proto::EnvironmentProfile::*clear)();
			ColorCurve EnvironmentProfile::*defaultCurve;
		};

		const std::array<CurveSlot, EnvironmentProfileEditorWindow::CurveCount> s_curveSlots = { {
			{ "Sky Horizon", "unused", &proto::EnvironmentProfile::has_sky_horizon, &proto::EnvironmentProfile::sky_horizon, &proto::EnvironmentProfile::mutable_sky_horizon, &proto::EnvironmentProfile::clear_sky_horizon, &EnvironmentProfile::skyHorizon },
			{ "Sky Zenith", "unused", &proto::EnvironmentProfile::has_sky_zenith, &proto::EnvironmentProfile::sky_zenith, &proto::EnvironmentProfile::mutable_sky_zenith, &proto::EnvironmentProfile::clear_sky_zenith, &EnvironmentProfile::skyZenith },
			{ "Clouds", "unused", &proto::EnvironmentProfile::has_clouds, &proto::EnvironmentProfile::clouds, &proto::EnvironmentProfile::mutable_clouds, &proto::EnvironmentProfile::clear_clouds, &EnvironmentProfile::clouds },
			{ "Ambient", "unused", &proto::EnvironmentProfile::has_ambient, &proto::EnvironmentProfile::ambient, &proto::EnvironmentProfile::mutable_ambient, &proto::EnvironmentProfile::clear_ambient, &EnvironmentProfile::ambient },
			{ "Sun", "intensity", &proto::EnvironmentProfile::has_sun, &proto::EnvironmentProfile::sun, &proto::EnvironmentProfile::mutable_sun, &proto::EnvironmentProfile::clear_sun, &EnvironmentProfile::sun },
			{ "Moon", "intensity", &proto::EnvironmentProfile::has_moon, &proto::EnvironmentProfile::moon, &proto::EnvironmentProfile::mutable_moon, &proto::EnvironmentProfile::clear_moon, &EnvironmentProfile::moon },
			{ "Fog", "density multiplier", &proto::EnvironmentProfile::has_fog, &proto::EnvironmentProfile::fog, &proto::EnvironmentProfile::mutable_fog, &proto::EnvironmentProfile::clear_fog, &EnvironmentProfile::fog },
			{ "Sun Scatter (Shafts)", "shaft multiplier", &proto::EnvironmentProfile::has_sun_scatter, &proto::EnvironmentProfile::sun_scatter, &proto::EnvironmentProfile::mutable_sun_scatter, &proto::EnvironmentProfile::clear_sun_scatter, &EnvironmentProfile::sunScatter },
		} };

		std::unique_ptr<ColorCurveImGuiEditor> MakeCurveEditor(const char* label, ColorCurve& curve)
		{
			auto editor = std::make_unique<ColorCurveImGuiEditor>(label, curve);
			editor->SetShowAlpha(true);
			editor->SetShowColorPreview(true);
			return editor;
		}
	}

	EnvironmentProfileEditorWindow::EnvironmentProfileEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.environmentProfiles, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Environment Profiles";
	}

	void EnvironmentProfileEditorWindow::OnNewEntry(proto::TemplateManager<proto::EnvironmentProfiles, proto::EnvironmentProfile>::EntryType& entry)
	{
		// Every curve starts empty (= Default) and every fixed value at its proto default, so a
		// new profile renders exactly like the built-in Default until something is changed.
		entry.set_name("New Environment");
		GetEnvironmentPreview().NotifyChanged();
	}

	bool EnvironmentProfileEditorWindow::CanRemoveEntry(const proto::EnvironmentProfile& entry) const
	{
		for (const auto& zone : m_project.zones.getTemplates().entry())
		{
			if (zone.environment_profile() == entry.id())
			{
				return false;
			}
		}

		for (const auto& map : m_project.maps.getTemplates().entry())
		{
			if (map.environment_profile() == entry.id())
			{
				return false;
			}
		}

		return true;
	}

	void EnvironmentProfileEditorWindow::BindCurves(const proto::EnvironmentProfile& entry)
	{
		const EnvironmentProfile defaults = EnvironmentProfile::MakeDefault();

		for (size_t i = 0; i < CurveCount; ++i)
		{
			const CurveSlot& slot = s_curveSlots[i];
			m_curves[i] = defaults.*slot.defaultCurve;
			LoadColorCurve((entry.*slot.get)(), m_curves[i]);
			m_curveEditors[i] = MakeCurveEditor(slot.label, m_curves[i]);
		}

		m_boundProfileId = entry.id();
	}

	void EnvironmentProfileEditorWindow::DrawDetailsImpl(proto::EnvironmentProfile& currentEntry)
	{
		if (m_boundProfileId != currentEntry.id())
		{
			BindCurves(currentEntry);
		}

		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}

			DrawReferences(currentEntry);
		}

		DrawPreviewBar(currentEntry);
		DrawFixedValues(currentEntry);
		DrawCurves(currentEntry);
	}

	void EnvironmentProfileEditorWindow::DrawReferences(const proto::EnvironmentProfile& entry)
	{
		int count = 0;
		for (const auto& zone : m_project.zones.getTemplates().entry())
		{
			if (zone.environment_profile() == entry.id())
			{
				ImGui::BulletText("Zone: %s", zone.name().c_str());
				++count;
			}
		}

		for (const auto& map : m_project.maps.getTemplates().entry())
		{
			if (map.environment_profile() == entry.id())
			{
				ImGui::BulletText("Map default: %s", map.name().c_str());
				++count;
			}
		}

		if (count == 0)
		{
			ImGui::TextDisabled("Not used by any zone or map.");
		}
		else
		{
			ImGui::TextDisabled("Clear these references before removing the profile.");
		}
	}

	void EnvironmentProfileEditorWindow::DrawPreviewBar(const proto::EnvironmentProfile& entry)
	{
		if (const auto section = ScopedEditorSection("Preview", ImGuiTreeNodeFlags_DefaultOpen))
		{
			EnvironmentPreview& preview = GetEnvironmentPreview();

			bool previewing = preview.profileId && *preview.profileId == entry.id();
			if (ImGui::Checkbox("Preview in world editors", &previewing))
			{
				if (previewing)
				{
					preview.profileId = entry.id();
				}
				else
				{
					preview.profileId.reset();
				}
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Every open world editor shows this profile at the time below.\nTurn off to return to the zone under the camera and the editor's own clock.");
			}

			ImGui::BeginDisabled(!previewing);
			ImGui::SliderFloat("Time of Day", &preview.normalizedTime, 0.0f, 1.0f, "%.3f");

			if (ImGui::Button("Dawn (6:00)"))
			{
				preview.normalizedTime = 0.25f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Noon (12:00)"))
			{
				preview.normalizedTime = 0.5f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Dusk (18:00)"))
			{
				preview.normalizedTime = 0.75f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Midnight (0:00)"))
			{
				preview.normalizedTime = 0.0f;
			}
			ImGui::EndDisabled();
		}
	}

	void EnvironmentProfileEditorWindow::DrawFixedValues(proto::EnvironmentProfile& entry)
	{
		if (const auto section = ScopedEditorSection("Fog, Shafts and Post", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool changed = false;

			float density = entry.fog_density();
			if (ImGui::DragFloat("Fog Density", &density, 0.0005f, 0.0f, 1.0f, "%.4f"))
			{
				entry.set_fog_density(std::clamp(density, 0.0f, 1.0f));
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Extinction per metre at the base height. The Fog curve's alpha multiplies it over the day.");
			}

			float falloff = entry.fog_height_falloff();
			if (ImGui::DragFloat("Fog Height Falloff", &falloff, 0.001f, 0.0f, 1.0f, "%.3f"))
			{
				entry.set_fog_height_falloff(std::clamp(falloff, 0.0f, 1.0f));
				changed = true;
			}

			float baseHeight = entry.fog_base_height();
			if (ImGui::DragFloat("Fog Base Offset", &baseHeight, 0.5f, -10000.0f, 10000.0f, "%.1f"))
			{
				entry.set_fog_base_height(std::clamp(baseHeight, -10000.0f, 10000.0f));
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Fog base height relative to the player (client) / camera pivot (editor). Negative = below it.");
			}

			float anisotropy = entry.fog_anisotropy();
			if (ImGui::SliderFloat("Sun Glow Tightness", &anisotropy, 0.0f, 0.95f, "%.2f"))
			{
				entry.set_fog_anisotropy(anisotropy);
				changed = true;
			}

			float shaftStrength = entry.shaft_strength();
			if (ImGui::DragFloat("Light Shaft Strength", &shaftStrength, 0.01f, 0.0f, 16.0f, "%.2f"))
			{
				entry.set_shaft_strength(std::clamp(shaftStrength, 0.0f, 16.0f));
				changed = true;
			}

			float exposure = entry.exposure();
			if (ImGui::DragFloat("Exposure", &exposure, 0.01f, 0.1f, 8.0f, "%.2f"))
			{
				entry.set_exposure(std::clamp(exposure, 0.1f, 8.0f));
				changed = true;
			}

			float bloomIntensity = entry.bloom_intensity();
			if (ImGui::DragFloat("Bloom Intensity", &bloomIntensity, 0.005f, 0.0f, 1.0f, "%.3f"))
			{
				entry.set_bloom_intensity(std::clamp(bloomIntensity, 0.0f, 1.0f));
				changed = true;
			}

			float bloomThreshold = entry.bloom_threshold();
			if (ImGui::DragFloat("Bloom Threshold", &bloomThreshold, 0.01f, 0.0f, 8.0f, "%.2f"))
			{
				entry.set_bloom_threshold(std::clamp(bloomThreshold, 0.0f, 8.0f));
				changed = true;
			}

			float transition = entry.transition_seconds();
			if (ImGui::DragFloat("Transition Seconds", &transition, 0.1f, 0.0f, 30.0f, "%.1f"))
			{
				entry.set_transition_seconds(std::clamp(transition, 0.0f, 30.0f));
				changed = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("How long the fade into this profile takes when a player crosses into its zone. 0 snaps.");
			}

			if (changed)
			{
				GetEnvironmentPreview().NotifyChanged();
			}
		}
	}

	void EnvironmentProfileEditorWindow::DrawCurves(proto::EnvironmentProfile& entry)
	{
		const EnvironmentProfile defaults = EnvironmentProfile::MakeDefault();

		for (size_t i = 0; i < CurveCount; ++i)
		{
			const CurveSlot& slot = s_curveSlots[i];
			ImGui::PushID(static_cast<int>(i));

			const bool authored = (entry.*slot.has)() && (entry.*slot.get)().key_size() > 0;
			const String header = String(slot.label) + (authored ? "" : "  (Default)") + "###curve";

			if (ImGui::CollapsingHeader(header.c_str()))
			{
				ImGui::TextDisabled("rgb: colour, alpha: %s", slot.alphaMeaning);

				ImGui::BeginDisabled(!authored);
				if (ImGui::Button("Reset to Default"))
				{
					(entry.*slot.clear)();
					m_curves[i] = defaults.*slot.defaultCurve;
					m_curveEditors[i] = MakeCurveEditor(slot.label, m_curves[i]);
					GetEnvironmentPreview().NotifyChanged();
				}
				ImGui::EndDisabled();

				if (m_curveEditors[i]->Draw())
				{
					StoreColorCurve(m_curves[i], *(entry.*slot.mutableGet)());
					GetEnvironmentPreview().NotifyChanged();
				}
			}

			ImGui::PopID();
		}
	}
}
```

If MSVC rejects taking the address of a generated protobuf accessor (error C2276 on `&proto::EnvironmentProfile::sky_horizon`), replace the member pointers in `CurveSlot` with lambdas. Use `std::function<const proto::ColorCurveData&(const proto::EnvironmentProfile&)>` and matching `has`, `mutableGet` and `clear` members, then rebuild.

- [ ] **Step 7: Register the window**

In `src/mmo_edit/mmo_edit.cpp`, add `#include "editor_windows/environment_profile_editor_window.h"` next to the water profile window include. After the `WaterProfileEditorWindow` line (264) add:
```cpp
	mainWindow.AddEditorWindow(std::make_unique<mmo::EnvironmentProfileEditorWindow>("Environment Profile Editor", project, mainWindow));
```

- [ ] **Step 8: World editor follows the camera zone or the preview**

In `world_editor_instance.h`, add `#include "scene_graph/environment_profile_proto.h"` next to the Task 4 include. After `EnvironmentController m_environment;` add:
```cpp

		/// Runtime profiles converted from the project on first use; cleared on every preview revision.
		std::unique_ptr<EnvironmentProfileCache<proto::EnvironmentProfileManager>> m_environmentProfiles;

		/// Profile id the controller targets. UINT32_MAX = none yet.
		uint32 m_environmentProfileId = UINT32_MAX;

		/// Last EnvironmentPreview::revision this instance converted profiles for.
		uint64 m_environmentRevision = 0;

		/// Whether the last update showed the Environment Profile Editor's preview.
		bool m_environmentPreviewActive = false;
```

In `world_editor_instance.cpp`, add `#include "environment_preview.h"`. In the constructor, right after `m_skyComponent->SetTimeSpeed(0.0f);`:
```cpp
		m_environmentProfiles = std::make_unique<EnvironmentProfileCache<proto::EnvironmentProfileManager>>(m_editor.GetProject().environmentProfiles);
```

Replace the Task 4 body of `UpdateEnvironment` with:
```cpp
	void WorldEditorInstance::UpdateEnvironment(const float deltaSeconds)
	{
		EnvironmentPreview& preview = GetEnvironmentPreview();

		// Any authored change reconverts profiles and snaps, so edits show up immediately.
		bool dataChanged = false;
		if (preview.revision != m_environmentRevision)
		{
			m_environmentRevision = preview.revision;
			m_environmentProfiles->Clear();
			dataChanged = true;
		}

		if (preview.profileId)
		{
			m_skyComponent->SetNormalizedTimeOfDay(preview.normalizedTime);

			if (dataChanged || !m_environmentPreviewActive || m_environmentProfileId != *preview.profileId)
			{
				m_environmentProfileId = *preview.profileId;
				m_environment.SetTarget(m_environmentProfiles->Get(m_environmentProfileId), true);
			}

			m_environmentPreviewActive = true;
		}
		else
		{
			const bool leftPreview = m_environmentPreviewActive;
			m_environmentPreviewActive = false;

			uint32 zoneId = 0;
			if (m_terrain && m_terrain->TryGetArea(m_cameraAnchor->GetDerivedPosition(), zoneId))
			{
				const proto::MapEntry* map = m_spawnEditMode ? m_spawnEditMode->GetMapEntry() : nullptr;
				const proto::Project& project = m_editor.GetProject();
				const uint32 profileId = ResolveEnvironmentProfileId(project.zones, project.maps, zoneId, map ? map->id() : 0);

				if (dataChanged || leftPreview || profileId != m_environmentProfileId)
				{
					m_environmentProfileId = profileId;
					m_environment.SetTarget(m_environmentProfiles->Get(profileId), dataChanged || leftPreview);
				}
			}
		}

		m_environment.Update(deltaSeconds, m_skyComponent->GetNormalizedTimeOfDay());
		m_skyComponent->ApplyEnvironment(m_environment.GetState());

		if (DeferredRenderer* renderer = GetDeferredRenderer())
		{
			const EnvironmentState& state = m_environment.GetState();
			renderer->SetExposure(state.exposure);
			renderer->SetBloomIntensity(state.bloomIntensity);
			renderer->SetBloomThreshold(state.bloomThreshold);
		}
	}
```
`m_spawnEditMode` is created later in the constructor (line ~371), and `UpdateEnvironment` only runs from the per-frame update. It is still null-checked because a map without a world entry has no spawn mode data.

- [ ] **Step 9: Build the editor**

Run: `cmake -S . -B build; cmake --build build --config Debug -t mmo_edit`
Expected: build succeeds.

- [ ] **Step 10: Run the unit suites**

Run: `bin/Debug/scene_graph_tests.exe "[environment]"; bin/Debug/client_data_tests.exe`
Expected: `All tests passed`.

- [ ] **Step 11: Commit**

```bash
git add src/mmo_edit/environment_preview.h src/mmo_edit/editor_windows/environment_profile_combo.h src/mmo_edit/editor_windows/environment_profile_combo.cpp src/mmo_edit/editor_windows/environment_profile_editor_window.h src/mmo_edit/editor_windows/environment_profile_editor_window.cpp src/mmo_edit/editor_windows/editor_entry_window_base.h src/mmo_edit/editor_windows/zone_editor_window.h src/mmo_edit/editor_windows/zone_editor_window.cpp src/mmo_edit/editor_windows/map_editor_window.h src/mmo_edit/editor_windows/map_editor_window.cpp src/mmo_edit/editors/world_editor/world_editor_instance.h src/mmo_edit/editors/world_editor/world_editor_instance.cpp src/mmo_edit/mmo_edit.cpp
git commit -m "feat(editor): environment profile editor, zone and map pickers, live world preview

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Visual verification, docs sign-off, gate

The controller (main session) runs this task, not a subagent: it drives the real client and editor GUIs. Use the recipes in the `client-visual-verification` memory: auto-login, the Alt-tap foreground plus real-cursor click, and console key VK 0xC0. Stop the user's dev servers only with the user's permission.

**Files:**
- Modify: `docs/superpowers/specs/2026-09-14-zone-environment-profiles-design.md` (append "Implementation Notes")
- Data (only if a test profile is kept): `data/editor/data/environment_profiles.data`, `data/client/ClientDB/…` (commit inside the submodules, no pointer bump)

- [ ] **Step 1: A/B the Default look in the client**

Build `mmo_client` in Debug. Capture screenshots at the same spot at 12:00, 18:30 and 00:00, via the in-game time or the server clock. Do this once with the pre-Task-4 client (checked-out baseline exe from commit `5cb80477`) and once with the new client, with an empty environment profile table.
Expected: no visible difference. Differences in sky horizon, zenith, ambient or clouds mean a seeded key in `EnvironmentProfile::MakeDefault` is wrong. Compare it against the `.hccv` dump in Task 2.

- [ ] **Step 2: Editor authoring and live preview**

1. Open the editor and the Environment Profile Editor.
2. Add a profile "Test Swamp": fog density 0.02, a green Fog curve, exposure 0.8, transition 5 s.
3. Turn on **Preview in world editors** with a world open. Expected: the viewport snaps to the profile; dragging the fog curve's keys updates the viewport while dragging.
4. Scrub the preview time through the presets. Expected: sun direction, sky and fog follow.
5. Turn preview off. Expected: the viewport returns to the zone under the camera (built-in Default if nothing is assigned).
6. Assign "Test Swamp" to one zone in the Zone Editor.
7. Fly the camera pivot across that zone's painted border. Expected: a 5 s fade.
8. Try **Remove Selected** on "Test Swamp". Expected: disabled while the zone references it.

- [ ] **Step 3: Client border fade and teleport snap**

1. Save the project and run Export to Client.
2. Log in, then walk from a neighbouring zone into the "Test Swamp" zone. Expected: a 5 s fade in and a 3 s fade back out (the Default's transition).
3. Teleport across the map with a GM teleport command. Expected: an immediate change, no fade.
4. Log out and back in inside the zone. Expected: no fade-in from the map default on login.

- [ ] **Step 4: Brightness option**

In Options, move Brightness from 1.0 to 2.0. Expected: the scene brightens and stays tied to the zone's exposure.

- [ ] **Step 5: Clean up test data**

Ask the user whether to keep "Test Swamp". If not, clear the zone reference, remove the profile, save and export again. Commit data changes only inside `data/editor` / `data/client`, without bumping the superproject pointers.

- [ ] **Step 6: Record implementation notes**

Append an `## Implementation Notes` section to the spec. Record:
- Deviations: evaluator names `EvaluateEnvironment` / `LerpEnvironment`; the default-parity test covers only the `client_data` copy (protobuf descriptor collision); no `TryGetArea` unit test (terrain suite disabled).
- The A/B result.
- Any tuning.

- [ ] **Step 7: Run the gate**

Run `/gate` (needs the user's dev servers stopped: LNK1168 otherwise). Expected: green, then review findings addressed before `/ship`.

- [ ] **Step 8: Commit**

```bash
git add docs/superpowers/specs/2026-09-14-zone-environment-profiles-design.md
git commit -m "docs(render): zone environment profile implementation notes

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

## Spec coverage

| Spec requirement | Task |
|---|---|
| Profile table, fields 1–19, zone field 12, map field 9, registration, export | 1 |
| Empty curve = Default; built-in Default equals today's look | 2 (seeded keys), 7 (A/B) |
| Resolution order zone → parent (depth 8) → map → Default; unknown id → Default | 2 |
| Malformed tangents → auto + warning | 2 |
| Blending: transition of incoming profile, weights sum 1, return continues, same target no-op, immediate, max 4, zero transition snaps | 3 |
| `TryGetArea` keeps the target while pages stream | 5 |
| SkyComponent drops curves, `ApplyEnvironment`, no black first frame | 4 |
| Per-frame order in client and editor | 4 |
| Look cvars removed, `gxExposure` multiplier | 4 |
| World enter snap, zone fade, >200 m teleport snap | 5 |
| Profile window: list, duplicate, delete guard with references, curve widgets, reset, fixed sliders, preview toggle + time presets | 6 |
| `EnvironmentPreview` state, live rebuild on change counter | 6 |
| Zone "Environment" combo, map "Default environment" combo | 6 |
| World editor follows camera pivot zone; fog sliders removed | 4, 6 |
| Docs updated | 4 |
| Default parity test | 1 |
| Visual checks + gate | 7 |
