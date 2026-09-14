# Zone Environment Profiles

Date: 2026-09-14
Status: approved design, awaiting spec review

## Goal

Each zone gets its own lighting and mood: sky, clouds, ambient, sun and moon, fog, light shafts,
exposure and bloom, each following the day/night cycle. The client applies the profile of the zone
the player is in and fades between profiles at zone borders. The world editor previews the same
result live while profiles are authored.

This is the first of two projects. The second, wind-driven volumetric fog, adds fields to the
profile defined here.

## Decisions

| Topic | Decision |
|---|---|
| Profile scope | Full day cycle per profile: its own curves plus fixed values. Today's global curves become the built-in Default. |
| Clock | Profiles change only the look. All zones share the world server's clock; no per-zone time lock or offset. |
| Curve storage | Curve keys are stored inside the profile record in game data. `.hccv` files are no longer read by the sky. |
| Look cvars | Removed: gxFogDensity, gxFogHeightFalloff, gxFogBaseHeight, gxFogAnisotropy, gxShaftStrength, gxBloomIntensity, gxBloomThreshold. `gxExposure` stays as the player Brightness multiplier. |
| Architecture | A graphics-independent `EnvironmentController` resolves, evaluates and blends; `SkyComponent` only applies the result. |
| Border blending | Fade over time, like the zone music crossfade. No distance-based blending. |

## 1. Data model

### New table: environment profiles

The new file `src/shared/proto_data/environment_profiles.proto` has a mirror in `src/shared/client_data/` with identical
field numbers, following the `water_profiles` pattern. The table is registered in both `project.h` files and
in the editor's Export to Client list (`main_window.cpp` `sharedManagers`). Field numbers are part of
the ClientDB contract and must never be reused or renumbered.

```proto
message ColorCurveKey
{
	required float time = 1;
	required float r = 2;
	required float g = 3;
	required float b = 4;
	optional float a = 5 [default = 1];
	repeated float in_tangent = 6 [packed = true];   // exactly 4 values, or empty for auto
	repeated float out_tangent = 7 [packed = true];  // exactly 4 values, or empty for auto
	optional uint32 tangent_mode = 8;                // ColorKey::tangentMode (0 auto, 1 user)
}

message ColorCurveData
{
	repeated ColorCurveKey key = 1;
}

message EnvironmentProfile
{
	required uint32 id = 1;
	required string name = 2;

	// Day curves, time 0..1 over the day.
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
	optional float transition_seconds = 19 [default = 3];  // fade time when blending INTO this profile
}

message EnvironmentProfiles
{
	repeated EnvironmentProfile entry = 1;
}
```

Fields 20 and up are reserved for the wind-driven volumetric fog project.

### Changes to existing tables

Both the `proto_data` and `client_data` copies change:

- `ZoneEntry.environment_profile = 12`. 0 means inherit from the parent zone.
- `MapEntry.environment_profile = 9`. 0 means the built-in Default. Field 9 is free in both copies.

### Rules

- **An empty curve means the Default curve**, not black. A new profile renders exactly like the
  Default until something is changed.
- **Resolution order:** the zone's own profile, then its parent chain (depth cap 8, same walk as zone
  music), then the map's default profile, then the built-in Default. A profile id that is not
  present in the table also resolves to the built-in Default.
- **Built-in Default** lives in code. It is today's `SkyComponent` fallback curves for horizon, zenith,
  ambient, clouds, fog and sun scatter, re-seeded from the current `Models/*.hccv` contents so the
  look does not change. It also holds today's hardcoded sun (1, 0.95, 0.9, intensity 1) and moon
  (0.3, 0.4, 0.65, intensity 0.12) as constant curves, plus the tuned fixed values above. An empty
  or missing profile table therefore renders exactly as today.
- The six `Models/*.hccv` sky curve files are no longer read. The files stay on disk; deleting
  them is not part of this project.
- `data/editor` and `data/client` changes are committed inside the submodules without bumping the
  superproject pointers.

## 2. Environment controller

This is plain C++ with no graphics device, in `src/shared/scene_graph/` next to `atmosphere_settings.h`.

### Units

- **`environment_profile.h/.cpp`**: the runtime `EnvironmentProfile` struct.
  - Holds 8 `ColorCurve`s (horizon, zenith, clouds, ambient, sun, moon, fog, sun scatter) and the fixed values.
  - `static EnvironmentProfile MakeDefault()` builds the built-in Default.
- **`environment_profile_proto.h`**: header-only templates. They are templates because the editor uses `mmo::proto`
  and the client uses `mmo::proto_client`, with identical field names.
  - `template<class TProfile> EnvironmentProfile LoadEnvironmentProfile(const TProfile&)` converts a record.
    Every empty curve is filled from `MakeDefault()`, and fixed values are clamped through the
    `AtmosphereParameters` setters.
  - `template<class TZones, class TMaps> uint32 ResolveEnvironmentProfileId(const TZones&, const TMaps&, uint32 zoneId, uint32 mapId)`
    applies the resolution order above and returns 0 for the built-in Default.
  - Malformed tangent arrays (not 0 or 4 values) are ignored: the key gets auto tangents and a warning is logged.
- **`environment_state.h/.cpp`**: the flat output struct.
  - Sky horizon, zenith and cloud colours (`Vector4`), ambient (`Vector3`).
  - Sun colour and intensity, moon colour and intensity.
  - `AtmosphereParameters`, `AtmosphereTimeOfDay`.
  - `exposure`, `bloomIntensity`, `bloomThreshold`.
  - `EnvironmentState Evaluate(const EnvironmentProfile&, float normalizedTime)`.
  - `EnvironmentState Lerp(const EnvironmentState&, const EnvironmentState&, float t)`: component-wise linear.
- **`environment_controller.h/.cpp`**:
  ```cpp
  void SetTarget(std::shared_ptr<const EnvironmentProfile> profile, bool immediate);
  void Update(float deltaSeconds, float normalizedTime);
  const EnvironmentState& GetState() const;
  ```
  Before any `SetTarget` call, the controller evaluates the built-in Default.

### Blending

- The controller keeps a list of up to 4 weighted profiles.
- **Normal target change:** `SetTarget` with a new profile adds it at weight 0. The weight rises to 1
  over that profile's `transition_seconds`; the others are scaled so the weights always sum to 1.
  Entries that reach weight 0 are dropped.
- **Returning to a fading profile:** its weight keeps rising from its current value instead of restarting.
- **Target already active:** calling `SetTarget` with the profile that is already the target does nothing.
- **Snapping:** `immediate` replaces the list with the single target at weight 1.
- **List full:** if a 5th profile is needed, the lowest-weight entry is dropped and the weights are renormalised.
- **Evaluation:** every entry is evaluated at the current `normalizedTime`, so the day keeps moving during a
  fade. The results are combined by weight.
- **Zero transition time:** `transition_seconds` ≤ 0 behaves like `immediate`.

### Terrain area lookup

`Terrain::GetArea` returns 0 both for "no zone painted" and "page not loaded". A new
`bool Terrain::TryGetArea(const Vector3& position, uint32& outArea) const` returns false while the page
under the position is not yet loaded. Callers keep their current target on false.

## 3. Rendering and client wiring

### SkyComponent

- **Removed:** the six curve members, `LoadColorCurves`, and the hardcoded sun and moon colours and intensities.
- **Kept:** clock advance, sun/moon direction, the sun-vs-moon blend, the sky dome and cloud rotation.
- **New `void ApplyEnvironment(const EnvironmentState&)`:**
  - Light colour and intensity = sun × blendSun + moon × blendMoon.
  - Sky material `HorizonColor`, `ZenithColor`, `CloudColor`, `LightDirection`, `SunHeight`.
  - `Scene::SetAmbientColor`, `Scene::SetAtmosphereParameters`, `Scene::SetAtmosphereTimeOfDay`,
    `Scene::SetPrimaryDirectionalLight`.
  - Global shader parameters `SkyHorizonColor`, `SkyZenithColor`, `SunDirection`, `SunColor`.
- The constructor applies the Default state, so there is no black first frame.

### Per-frame order (client and editor)

1. `sky.Update(deltaSeconds, timestamp)`: advances time and rotates the clouds only.
2. `environment.Update(deltaSeconds, sky.GetNormalizedTimeOfDay())`.
3. `sky.ApplyEnvironment(environment.GetState())`.
4. `renderer.SetExposure(state.exposure * brightness)`, `renderer.SetBloomIntensity(state.bloomIntensity)`,
   `renderer.SetBloomThreshold(state.bloomThreshold)`. In the client, `brightness` is `gxExposure`; the editor uses 1.

### Client (WorldState)

- **Ownership:** owns an `EnvironmentController` and a cache from profile id to `shared_ptr<const EnvironmentProfile>`.
  The cache is built on first use from the profile table in `m_project`. Id 0 maps to the built-in Default.
- **World enter / map change:** `SetupWorldScene` resolves the profile for the player's position (the map default
  if the area is not yet known) and calls `SetTarget(..., immediate = true)`.
- **Zone change:** `CheckForZoneUpdate` uses `TryGetArea`. On false it leaves the target unchanged. When the zone
  changes, it resolves the profile id and calls `SetTarget(..., immediate = false)`.
- **Teleport:** a controlled-unit move of more than 200 m within one frame calls `SetTarget(..., immediate = true)`.
- **Removed cvars:** gxFogDensity, gxFogHeightFalloff, gxFogBaseHeight, gxFogAnisotropy, gxShaftStrength,
  gxBloomIntensity and gxBloomThreshold lose their registration and handlers, including
  `OnAtmosphereParametersChanged`.
- **`gxExposure`:** keeps its name and Options Brightness slider (0.5–2.0, default 1.0) and becomes a multiplier on
  the profile exposure. Its description text changes to say so.
- **Unchanged:** `gxAtmosphereQuality`, `gxAtmosphereMarchDistance`, `gxAtmosphereDebug`, `gxBloomQuality`, fog
  enable, and underwater handling.

### Docs

- `docs/console_commands.md` and `docs/rendering-atmosphere.md`: remove the seven cvars and describe `gxExposure` as
  a multiplier.
- `docs/rendering-atmosphere.md`: replace the "Time of day" section with the profile model.

## 4. Editor

### Environment Profiles window

`src/mmo_edit/editor_windows/environment_profile_editor_window.*` follows the water profile window's layout.

- **List:** add, duplicate and delete.
  - Delete is disabled while any zone or map references the profile.
  - The details panel lists the referencing zones and maps.
- **Details:**
  - Name and transition seconds.
  - One collapsible header per day curve, drawn with `ColorCurveImGuiEditor`. Alpha is labelled per curve
    (intensity, density ×, shaft ×). Each has a "Reset to Default" button that clears the curve.
  - Sliders for the fixed values, with the same ranges as the `AtmosphereParameters` setters. Exposure is 0.1–8,
    bloom intensity 0–1, bloom threshold 0–8, transition seconds 0–30.
- **Preview bar:**
  - A "Preview in world editors" toggle and a time-of-day slider with Dawn / Noon / Dusk / Midnight presets.
  - While preview is on, every open world editor snaps to this profile at the preview time. Turning it off
    returns to the editor's own clock and the zone under the camera.

### Editor-side preview state

`EnvironmentPreview` is a small shared object owned by the editor host. It holds:

- the preview profile id (optional),
- the preview time (optional),
- a change counter.

The profile window updates it on every edit. Each world editor instance reads it every frame and
rebuilds its profile cache when the counter changes, so curve edits show live.

### Zone and map editors

- Zone editor window: an "Environment" combo next to Music/Ambience; "(inherit)" writes 0.
- Map editor window: a "Default environment" combo; "(built-in Default)" writes 0.

### World editor

- Owns an `EnvironmentController` and runs the same per-frame order.
- **Without preview:** the target is the profile resolved from the zone under the camera pivot (`TryGetArea`) and
  the current map, faded normally.
- **With preview:** `SetTarget(preview profile, immediate = true)` and the sky time is set to the preview time.
- **World settings panel:** the fog sliders (Density, Height Falloff, Base Offset, Anisotropy, Shaft Strength) are
  removed from `world_settings_panel.cpp`; "Show Fog" stays.
- **Minimap bake:** unchanged; it still disables fog.

## Testing and verification

### Unit tests (`scene_graph_tests`)

- **Default profile:** `MakeDefault()` curves evaluate to the current fallback keys at those keys' times.
- **Loading:** an empty curve is filled from Default; a set curve is kept; a malformed tangent array falls back to auto.
- **Resolution:** own zone, parent, depth cap 8, map default, unknown profile id → Default.
- **Blending weights:** they sum to 1 at every step; a fade takes the incoming profile's `transition_seconds`.
- **Returning:** a profile coming back to target continues from its current weight.
- **Snapping:** `immediate` snaps, and a zero transition time snaps.
- **Capacity:** the list never exceeds 4 entries.
- **Lerp:** `Lerp(a, b, 0) == a`, `Lerp(a, b, 1) == b`.

### Default consistency (`client_data_tests`)

The proto default values for fields 11–19 in both `proto_data` and `client_data` equal the
`AtmosphereParameters` / tonemap / bloom defaults.

### Visual checks (controller-driven)

- **A/B:** the client with an empty profile table shows no visible difference from the fog branch at noon, dusk
  and night.
- **Border fade:** a test profile assigned to one zone fades in over its transition time when walking across the border.
- **Live editing:** editing a curve with preview on updates the world editor viewport immediately.

### Gate

`/gate` green before `/ship`.

## Out of scope

- Wind-driven volumetric fog and a shared wind value (next spec; adds profile fields 20+).
- Per-zone time lock or offset.
- Interior / WMO environments.
- Distance-based border blending.
- Making the sun path window (0.20–0.80) or sun arc part of a profile.
- Deleting the `Models/*.hccv` files.
