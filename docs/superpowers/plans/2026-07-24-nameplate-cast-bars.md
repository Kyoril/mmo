# Nameplate Cast Bars Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a Plater-style cast bar (progress + spell name, interrupted flash) below the health bar of unit nameplates, driven by the spell packets the client already receives for every caster in view.

**Architecture:** A header-only `UnitCastInfo` value struct holds per-unit cast state and derives progress from timestamps at read time (no timers). `GameUnitC` embeds it behind thin `NotifyCast*` methods called from the five existing `WorldState` spell packet handlers. `NameplateFrame` gains a cast bar child (childless-XML-template Copy pattern) that reads the state each `Animate` tick. Client-only — no server/protocol changes.

**Tech Stack:** C++17, custom frame_ui library, Catch2 (`src/unit_tests`, include `"catch.hpp"`), CMake/Visual Studio on Windows. UI templates/Lua/locales live in the **data/client git submodule**.

**Spec:** `docs/superpowers/specs/2026-07-24-nameplate-cast-bars-design.md`

## Global Constraints

- Code style: Allman braces, tabs, `m_camelCase` members, `PascalCase` methods, `#pragma once`, copyright header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on every new file.
- No exceptions; use `ASSERT`/`DLOG`/`ELOG` from `base/macros.h`.
- Localized strings: no hardcoded UI text; keys must exist in ALL 4 locales (`Locale_enUS`, `Locale_deDE`, `Locale_frFR`, `Locale_ruRU`).
- `data/client` is a submodule: files under `data/client/...` are committed **inside the submodule**, C++ and docs in the main repo.
- `unit_tests` cannot link the full `game_client` library (render deps) — new testable code must be header-only or explicitly listed in `src/unit_tests/CMakeLists.txt`. `UnitCastInfo` is header-only, so no CMake change is needed.
- The plate cvar naming/Options pattern: cvar registered in `WorldState::RegisterGameplayCommands`, unregistered in the matching unregister block, toggle row in `OptionsFrame.lua`, label key prefixed `OPTIONS_NAMEPLATES_`.

---

### Task 1: `UnitCastInfo` value struct + unit tests

**Files:**
- Create: `src/shared/game_client/unit_cast_info.h`
- Test: `src/unit_tests/test_unit_cast_info.cpp`

**Interfaces:**
- Consumes: nothing (only `base/typedefs.h`).
- Produces (used by Tasks 2 and 3):
  - `struct mmo::UnitCastInfo` with public fields `const proto_client::SpellEntry* spell`, `GameTime startTime`, `GameTime endTime`, `bool channeling`, `GameTime interruptedAt`.
  - `static constexpr GameTime InterruptFlashDurationMs = 800;`
  - `void BeginCast(const proto_client::SpellEntry& spell, GameTime now, GameTime castTimeMs)`
  - `void BeginChannel(const proto_client::SpellEntry& spell, GameTime now, GameTime durationMs)`
  - `void UpdateChannel(GameTime now, GameTime timeLeftMs)`
  - `void FinishSucceeded()`
  - `void FinishFailed(GameTime now)`
  - `[[nodiscard]] bool IsActive() const`
  - `[[nodiscard]] float GetProgress(GameTime now) const` — casts fill 0→1, channels drain 1→0, clamped.
  - `[[nodiscard]] bool IsInterruptFlashActive(GameTime now) const`

- [ ] **Step 1: Write the failing test**

Create `src/unit_tests/test_unit_cast_info.cpp`. The struct never dereferences the spell pointer, so the tests use a dummy address:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "game_client/unit_cast_info.h"

using namespace mmo;

namespace
{
	// UnitCastInfo stores the spell pointer for identity only and never dereferences
	// it, so the tests can use the address of an unrelated dummy object.
	const proto_client::SpellEntry* DummySpell()
	{
		static int dummy = 0;
		return reinterpret_cast<const proto_client::SpellEntry*>(&dummy);
	}
}

TEST_CASE("UnitCastInfo is idle by default", "[unit_cast_info]")
{
	const UnitCastInfo info;
	CHECK_FALSE(info.IsActive());
	CHECK_FALSE(info.IsInterruptFlashActive(123456));
}

TEST_CASE("BeginCast activates and progress fills over time", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 1000, 2000);

	REQUIRE(info.IsActive());
	CHECK(info.spell == DummySpell());
	CHECK_FALSE(info.channeling);
	CHECK(info.GetProgress(1000) == Approx(0.0f));
	CHECK(info.GetProgress(2000) == Approx(0.5f));
	CHECK(info.GetProgress(3000) == Approx(1.0f));
	// Clamped past the end.
	CHECK(info.GetProgress(9000) == Approx(1.0f));
}

TEST_CASE("BeginChannel drains from full to empty", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginChannel(*DummySpell(), 1000, 4000);

	REQUIRE(info.IsActive());
	CHECK(info.channeling);
	CHECK(info.GetProgress(1000) == Approx(1.0f));
	CHECK(info.GetProgress(3000) == Approx(0.5f));
	CHECK(info.GetProgress(5000) == Approx(0.0f));
	CHECK(info.GetProgress(9000) == Approx(0.0f));
}

TEST_CASE("UpdateChannel rebases the end time (pushback) and zero ends the channel", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginChannel(*DummySpell(), 1000, 4000);

	// Pushback: at t=2000 the server says only 1000ms remain.
	info.UpdateChannel(2000, 1000);
	CHECK(info.endTime == 3000);
	REQUIRE(info.IsActive());

	// timeLeft == 0 is the server's normal channel-end signal: clears without flash.
	info.UpdateChannel(2500, 0);
	CHECK_FALSE(info.IsActive());
	CHECK_FALSE(info.IsInterruptFlashActive(2500));
}

TEST_CASE("UpdateChannel on an idle unit is a no-op", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.UpdateChannel(2000, 1000);
	CHECK_FALSE(info.IsActive());
}

TEST_CASE("FinishSucceeded clears without an interrupt flash", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 1000, 2000);
	info.FinishSucceeded();

	CHECK_FALSE(info.IsActive());
	CHECK(info.spell == nullptr);
	CHECK_FALSE(info.IsInterruptFlashActive(1500));
}

TEST_CASE("FinishFailed on an active cast triggers a time-limited interrupt flash", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 1000, 2000);
	info.FinishFailed(1500);

	CHECK_FALSE(info.IsActive());
	CHECK(info.IsInterruptFlashActive(1500));
	CHECK(info.IsInterruptFlashActive(1500 + UnitCastInfo::InterruptFlashDurationMs - 1));
	CHECK_FALSE(info.IsInterruptFlashActive(1500 + UnitCastInfo::InterruptFlashDurationMs));
}

TEST_CASE("FinishFailed without an active cast does not flash", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.FinishFailed(1500);
	CHECK_FALSE(info.IsInterruptFlashActive(1500));
}

TEST_CASE("Starting a new cast clears a pending interrupt flash", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 1000, 2000);
	info.FinishFailed(1500);
	info.BeginCast(*DummySpell(), 1600, 2000);

	REQUIRE(info.IsActive());
	CHECK_FALSE(info.IsInterruptFlashActive(1600));
}

TEST_CASE("Zero cast time yields full progress instead of dividing by zero", "[unit_cast_info]")
{
	UnitCastInfo info;
	info.BeginCast(*DummySpell(), 1000, 0);
	CHECK(info.GetProgress(1000) == Approx(1.0f));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```powershell
cmake --build build --config Debug -t unit_tests
```
Expected: **compile error** — `game_client/unit_cast_info.h` not found. (The CMake glob picks the new test file up automatically; if it doesn't, re-run `cmake -S . -B build` once.)

- [ ] **Step 3: Write the implementation**

Create `src/shared/game_client/unit_cast_info.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>

namespace mmo
{
	namespace proto_client
	{
		class SpellEntry;
	}

	/// Client-side cast state of a single unit, fed by the SpellStart / SpellGo /
	/// SpellFailure / ChannelStart / ChannelUpdate packets (which arrive for every
	/// caster in view). Progress is derived from timestamps at read time, so no
	/// per-frame updates or timers are required. Consumed by the nameplate cast bar.
	struct UnitCastInfo final
	{
		/// How long the "Interrupted" flash stays visible after a failed cast.
		static constexpr GameTime InterruptFlashDurationMs = 800;

		/// The spell being cast, or nullptr while idle. Stored for identity and
		/// name display only; this struct never dereferences it.
		const proto_client::SpellEntry* spell = nullptr;

		/// Client timestamp (GetAsyncTimeMs) when the cast/channel started.
		GameTime startTime = 0;

		/// Client timestamp when the cast/channel will finish.
		GameTime endTime = 0;

		/// True while channeling (bar drains instead of filling).
		bool channeling = false;

		/// Client timestamp of the last mid-cast failure (0 = none). Drives the
		/// time-limited "Interrupted" flash; expires by comparison, never cleaned up.
		GameTime interruptedAt = 0;

		/// Starts tracking a regular cast.
		void BeginCast(const proto_client::SpellEntry& castSpell, const GameTime now, const GameTime castTimeMs)
		{
			spell = &castSpell;
			startTime = now;
			endTime = now + castTimeMs;
			channeling = false;
			interruptedAt = 0;
		}

		/// Starts tracking a channeled cast.
		void BeginChannel(const proto_client::SpellEntry& castSpell, const GameTime now, const GameTime durationMs)
		{
			BeginCast(castSpell, now, durationMs);
			channeling = true;
		}

		/// Applies a server channel update (pushback or, with timeLeftMs == 0, the
		/// regular end-of-channel signal, which clears without an interrupt flash).
		void UpdateChannel(const GameTime now, const GameTime timeLeftMs)
		{
			if (!IsActive())
			{
				return;
			}

			if (timeLeftMs == 0)
			{
				FinishSucceeded();
				return;
			}

			endTime = now + timeLeftMs;
		}

		/// Clears the cast without any failure feedback (SpellGo / channel end).
		void FinishSucceeded()
		{
			spell = nullptr;
			startTime = 0;
			endTime = 0;
			channeling = false;
		}

		/// Clears the cast and stamps the interrupt flash - but only if a cast was
		/// actually tracked (failures of instant casts never showed a bar).
		void FinishFailed(const GameTime now)
		{
			if (!IsActive())
			{
				return;
			}

			FinishSucceeded();
			interruptedAt = now;
		}

		/// Whether a cast or channel is currently tracked.
		[[nodiscard]] bool IsActive() const
		{
			return spell != nullptr;
		}

		/// Bar fill fraction at the given time: casts fill 0 -> 1, channels drain
		/// 1 -> 0. Clamped to [0, 1].
		[[nodiscard]] float GetProgress(const GameTime now) const
		{
			float progress = 1.0f;
			if (endTime > startTime)
			{
				progress = static_cast<float>(now - startTime) / static_cast<float>(endTime - startTime);
				progress = std::clamp(progress, 0.0f, 1.0f);
			}

			return channeling ? 1.0f - progress : progress;
		}

		/// Whether the "Interrupted" flash should currently be shown.
		[[nodiscard]] bool IsInterruptFlashActive(const GameTime now) const
		{
			return interruptedAt != 0 && now >= interruptedAt && now - interruptedAt < InterruptFlashDurationMs;
		}
	};
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```powershell
cmake --build build --config Debug -t unit_tests
./bin/Debug/unit_tests.exe "[unit_cast_info]"
```
(Executable may land at `bin/unit_tests.exe` depending on generator — check `bin/`.)
Expected: All assertions pass, e.g. `All tests passed (NN assertions in 10 test cases)`.

- [ ] **Step 5: Commit (main repo)**

```bash
git add src/shared/game_client/unit_cast_info.h src/unit_tests/test_unit_cast_info.cpp
git commit -m "Add UnitCastInfo per-unit cast state for nameplate cast bars"
```

---

### Task 2: GameUnitC integration + WorldState packet wiring

**Files:**
- Modify: `src/shared/game_client/game_unit_c.h` (include + notify methods + member)
- Modify: `src/mmo_client/game_states/world_state.cpp` (`OnSpellStart` ~line 3247, `OnSpellGo` ~line 3294, `OnSpellFailure` ~line 3557, `OnChannelStart` ~line 3628, `OnChannelUpdate` ~line 3649)

**Interfaces:**
- Consumes: `UnitCastInfo` from Task 1; `GetAsyncTimeMs()` (already used in game_unit_c.cpp, declared via `base/clock.h`).
- Produces (used by Task 3):
  - `void GameUnitC::NotifyCastStarted(const proto_client::SpellEntry& spell, GameTime castTimeMs)`
  - `void GameUnitC::NotifyChannelStarted(const proto_client::SpellEntry& spell, GameTime durationMs)`
  - `void GameUnitC::NotifyChannelUpdate(GameTime timeLeftMs)`
  - `void GameUnitC::NotifyCastSucceeded()`
  - `void GameUnitC::NotifyCastFailed()`
  - `[[nodiscard]] const UnitCastInfo& GameUnitC::GetCastInfo() const`

- [ ] **Step 1: Add the cast state to GameUnitC**

In `src/shared/game_client/game_unit_c.h`:

Add the include next to the other project includes near the top of the file:

```cpp
#include "unit_cast_info.h"
```

Add the public API in the class body, near the other notify facades (around `NotifyAttackSwingEvent`, ~line 611). `GetAsyncTimeMs` is already available in this header's include set (`base/clock.h` — if the compiler disagrees, add `#include "base/clock.h"`):

```cpp
	public:
		/// Starts tracking a regular cast on this unit (SpellStart with castTime > 0).
		void NotifyCastStarted(const proto_client::SpellEntry& spell, const GameTime castTimeMs)
		{
			m_castInfo.BeginCast(spell, GetAsyncTimeMs(), castTimeMs);
		}

		/// Starts tracking a channeled cast on this unit (ChannelStart with duration > 0).
		void NotifyChannelStarted(const proto_client::SpellEntry& spell, const GameTime durationMs)
		{
			m_castInfo.BeginChannel(spell, GetAsyncTimeMs(), durationMs);
		}

		/// Applies a channel pushback / end signal (ChannelUpdate).
		void NotifyChannelUpdate(const GameTime timeLeftMs)
		{
			m_castInfo.UpdateChannel(GetAsyncTimeMs(), timeLeftMs);
		}

		/// Ends the tracked cast successfully (SpellGo). No-op while idle.
		void NotifyCastSucceeded()
		{
			m_castInfo.FinishSucceeded();
		}

		/// Ends the tracked cast as failed/interrupted (SpellFailure), triggering the
		/// nameplate "Interrupted" flash. No-op while idle.
		void NotifyCastFailed()
		{
			m_castInfo.FinishFailed(GetAsyncTimeMs());
		}

		/// Gets this unit's current cast state (read by the nameplate cast bar).
		[[nodiscard]] const UnitCastInfo& GetCastInfo() const
		{
			return m_castInfo;
		}
```

Add the member next to the other protected/private members:

```cpp
		/// Client-side cast state driving the nameplate cast bar.
		UnitCastInfo m_castInfo;
```

- [ ] **Step 2: Wire the five WorldState packet handlers**

In `src/mmo_client/game_states/world_state.cpp`:

**`OnSpellStart`** — inside the existing `if (const std::shared_ptr<GameUnitC> casterUnit = ...)` block (~line 3247), inside its `if (castTime > 0)`, after the two `SpellVisualizationService` calls:

```cpp
				casterUnit->NotifyCastStarted(*spell, castTime);
```

**`OnSpellGo`** — after the spell lookup + `ASSERT(spell);` (~line 3295), before the visualization lookup:

```cpp
		// End the caster's tracked cast bar state (no-op for instant casts, which
		// never started tracking).
		if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
		{
			casterUnit->NotifyCastSucceeded();
		}
```

**`OnSpellFailure`** — inside the existing `if (const std::shared_ptr<GameUnitC> casterUnit = ...)` block (~line 3560), after the `CancelCast` visualization call:

```cpp
				// Ends a tracked cast with the "Interrupted" nameplate flash. No-op if
				// this unit had no cast bar running (e.g. instant cast validation failure).
				casterUnit->NotifyCastFailed();
```

**`OnChannelStart`** — inside the existing caster-unit block (~line 3628), inside its `if (duration > 0)`, after the visualization calls:

```cpp
				casterUnit->NotifyChannelStarted(*spell, static_cast<GameTime>(duration));
```

**`OnChannelUpdate`** — before the existing local-player forwarding block (~line 3659):

```cpp
		// Update the caster's nameplate cast bar state for ANY unit (the packet is
		// broadcast to nearby players; timeLeft == 0 is the regular end-of-channel).
		if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
		{
			casterUnit->NotifyChannelUpdate(timeLeft);
		}
```

- [ ] **Step 3: Build to verify**

```powershell
cmake --build build --config Debug -t mmo_client
```
Expected: builds without errors or new warnings.

- [ ] **Step 4: Commit (main repo)**

```bash
git add src/shared/game_client/game_unit_c.h src/mmo_client/game_states/world_state.cpp
git commit -m "Track per-unit cast state from spell packets on GameUnitC"
```

---

### Task 3: Nameplate cast bar UI (XML template + NameplateFrame child)

**Files:**
- Modify: `data/client/Interface/GameUI/Nameplate.xml` (**data/client submodule**)
- Modify: `src/mmo_client/ui/nameplate_frame.h`
- Modify: `src/mmo_client/ui/nameplate_frame.cpp`

**Interfaces:**
- Consumes: `unit.GetCastInfo()` → `const UnitCastInfo&` (Task 2); `UnitCastInfo::{IsActive, GetProgress, IsInterruptFlashActive, spell, channeling}` (Task 1); existing `FitNameToWidth`, `ToHexColor` helpers in nameplate_frame.cpp; `Localize(FrameManager::Get().GetLocalization(), key)` from `frame_ui/localizer.h`; cvar `NameplateShowCastBars` (registered in Task 4 — until then the read falls back to visible).
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Add font + childless cast bar template to Nameplate.xml**

In `data/client/Interface/GameUI/Nameplate.xml`, add below the existing `NameplateFont` (line 18):

```xml
	<Font name="NameplateCastFont" file="Fonts/FRIZQT__.TTF" size="18" outline="0" shadowX="1.0" shadowY="1.0" />
```

Add the new template before the closing `</UiLayout>` (after `NameplateHighlightTemplate`), and extend the header comment list (lines 8-12) with one line:

```xml
		- NameplateCastBarTemplate   : the spell cast/channel bar shown below the health bar.
```

```xml
	<!--
		Cast bar shown below the health bar while the unit casts or channels. Must stay
		childless: the C++ frame clones it via Copy(), and template children would leak
		into the global frame registry (see NameplateFrame). The fill color is driven at
		runtime via "ProgressColor" (cast = yellow, interrupted flash = grey-red), the
		spell name via the frame text rendered by the Caption section.
	-->
	<Frame name="NameplateCastBarTemplate" type="ProgressBar">
		<Property name="Progress" value="0.0" />
		<Property name="ProgressColor" value="FFE8B923" />
		<Property name="Font" value="NameplateCastFont" />

		<Visual>
			<ImagerySection name="Background">
				<ImageComponent texture="Interface/fg4_gradientWhiteV1_result.htex" tiling="HORZ" tint="D8101010" />
			</ImagerySection>
			<ImagerySection name="Progress">
				<ImageComponent texture="Interface/fg4_gradientWhiteV1_result.htex" tiling="HORZ" tint="FFFFFFFF">
					<Area><Inset all="3" /></Area>
				</ImageComponent>
			</ImagerySection>
			<ImagerySection name="Border">
				<BorderComponent texture="Interface/GameUI/fg4_borders_insetBlackSmall.htex" borderSize="11" />
			</ImagerySection>
			<ImagerySection name="Caption">
				<TextComponent color="FFFFFFFF" horzAlign="CENTER" vertAlign="CENTER" wrap="false" />
			</ImagerySection>

			<StateImagery name="Enabled">
				<Layer>
					<Section section="Background" />
				</Layer>
			</StateImagery>
			<StateImagery name="Disabled">
				<Layer>
					<Section section="Background" />
				</Layer>
			</StateImagery>
			<StateImagery name="Progress">
				<Layer>
					<Section section="Progress" />
				</Layer>
			</StateImagery>
			<StateImagery name="Overlay">
				<Layer>
					<Section section="Border" />
					<Section section="Caption" />
				</Layer>
			</StateImagery>
			<StateImagery name="OverlayDisabled">
				<Layer>
					<Section section="Border" />
					<Section section="Caption" />
				</Layer>
			</StateImagery>
		</Visual>

		<Area>
			<Size><AbsDimension x="240" y="20" /></Size>
		</Area>
	</Frame>
```

- [ ] **Step 2: Extend NameplateFrame header**

In `src/mmo_client/ui/nameplate_frame.h`:

Add to the class doc comment (optional) and the private section, next to the existing children/caches:

```cpp
		/// The cast/channel bar below the health bar - visible only while the unit is
		/// casting (or briefly flashing "Interrupted" after a failed cast).
		std::shared_ptr<ProgressBar> m_castBar;

		/// The spell whose name the cast bar currently displays, so the (truncated)
		/// caption is only recomputed when the cast changes.
		const void* m_lastCastSpell = nullptr;

		/// Whether the cast bar currently shows the interrupted flash styling.
		bool m_castBarInterrupted = false;

		/// Cached cast bar fill color so the color property is only written on changes.
		argb_t m_castBarColor = 0;
```

Also add a private method declaration next to `UpdateContent`:

```cpp
		/// Refreshes the cast bar from the unit's current cast state (progress, spell
		/// name, interrupted flash, visibility, cvar filter).
		void UpdateCastBar(GameUnitC& unit);
```

- [ ] **Step 3: Implement the cast bar in nameplate_frame.cpp**

In `src/mmo_client/ui/nameplate_frame.cpp`:

Add includes next to the existing ones:

```cpp
#include "base/clock.h"
#include "console/console_var.h"
#include "frame_ui/localizer.h"
```

(Adjust the console include if the compiler complains — `ConsoleVarMgr` comes from the same header nameplate_manager.cpp uses; check its include block: `#include "console/console_var.h"`.)

Add to the anonymous namespace, next to the other constants:

```cpp
			// Vertical gap between the health bar bottom and the cast bar.
			constexpr float NameplateCastBarGap = 2.0f;
			// Cast bar fill while casting/channeling (warm yellow) and during the
			// short "Interrupted" flash (desaturated red).
			constexpr argb_t NameplateCastBarColor = 0xFFE8B923;
			constexpr argb_t NameplateCastBarInterruptedColor = 0xFF8B3A3A;

			const std::string NameplateCastBarTemplateName("NameplateCastBarTemplate");

			/// Reads a boolean console variable, falling back to a default if it doesn't
			/// exist (same idiom as NameplateManager).
			bool GetBoolCVar(const char* name, const bool defaultValue)
			{
				const ConsoleVar* var = ConsoleVarMgr::FindConsoleVar(name);
				return var ? var->GetBoolValue() : defaultValue;
			}
```

In `CreateChildren()`, load the template alongside the others (extend the existing lookup + guard):

```cpp
			const FramePtr castBarTemplate = FrameManager::Get().Find(NameplateCastBarTemplateName);
```

and extend the null-check to `... || !castBarTemplate`. Then, after `m_nameText` is created and before the `AddChild` calls, create the bar (same pattern as the health bar):

```cpp
			m_castBar = std::make_shared<ProgressBar>("ProgressBar", GetName() + "_Cast");
			castBarTemplate->Copy(*m_castBar);
			m_castBar->SetClickable(false);
			m_castBar->SetEnabled(false);
			m_castBar->SetVisible(false);
```

Add it after the other children (`AddChild(m_castBar);` after `AddChild(m_nameText);`), and anchor it below the health bar after the existing anchor setup:

```cpp
			// The cast bar hangs below the plate rect, attached to the health bar. Children
			// may render outside the parent rect (the highlight already does).
			m_castBar->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr, 0.0f);
			m_castBar->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr, 0.0f);
			m_castBar->SetAnchor(anchor_point::Top, anchor_point::Bottom, m_healthBar, NameplateCastBarGap);
```

Implement `UpdateCastBar` (place after `UpdateContent`) and call it from the end of `UpdateContent`:

```cpp
	void NameplateFrame::UpdateCastBar(GameUnitC& unit)
	{
		if (!m_castBar)
		{
			return;
		}

		const UnitCastInfo& cast = unit.GetCastInfo();
		const GameTime now = GetAsyncTimeMs();

		const bool enabled = GetBoolCVar("NameplateShowCastBars", true);
		const bool interrupted = cast.IsInterruptFlashActive(now);
		const bool active = cast.IsActive();
		if (!enabled || (!active && !interrupted))
		{
			m_castBar->SetVisible(false);
			m_lastCastSpell = nullptr;
			return;
		}

		if (interrupted)
		{
			// Full grey-red bar with a localized "Interrupted" caption for the flash.
			if (!m_castBarInterrupted)
			{
				m_castBarInterrupted = true;
				m_lastCastSpell = nullptr;
				m_castBar->SetText(Localize(FrameManager::Get().GetLocalization(), "NAMEPLATE_INTERRUPTED"));
			}
			m_castBar->SetProgress(1.0f);
		}
		else
		{
			m_castBarInterrupted = false;

			// Only re-fit the caption when the cast (or the text scale) changes.
			if (m_lastCastSpell != cast.spell)
			{
				m_lastCastSpell = cast.spell;
				const float maxTextWidth = ((GetWidth() > 0.0f ? GetWidth() : 240.0f) - 8.0f) * FrameManager::Get().GetUIScale().x;
				m_castBar->SetText(FitNameToWidth(cast.spell->name(), m_castBar->GetFont(), maxTextWidth));
			}

			m_castBar->SetProgress(cast.GetProgress(now));
		}

		const argb_t color = interrupted ? NameplateCastBarInterruptedColor : NameplateCastBarColor;
		if (color != m_castBarColor)
		{
			m_castBarColor = color;
			if (Property* progressColor = m_castBar->GetProperty("ProgressColor"))
			{
				progressColor->Set(ToHexColor(color));
			}
		}

		m_castBar->SetVisible(true);
	}
```

At the end of `UpdateContent(GameUnitC& unit)` (after the opacity line), add:

```cpp
		UpdateCastBar(unit);
```

`cast.spell->name()` requires the full `proto_client::SpellEntry` type — add `#include "client_data/project.h"` to the nameplate_frame.cpp include block (the same header world_state.h uses for all `proto_client` types).

- [ ] **Step 4: Build to verify**

```powershell
cmake --build build --config Debug -t mmo_client
```
Expected: builds clean.

- [ ] **Step 5: Commit (submodule + main repo)**

```bash
cd data/client
git add Interface/GameUI/Nameplate.xml
git commit -m "Add nameplate cast bar template + font"
cd ../..
git add src/mmo_client/ui/nameplate_frame.h src/mmo_client/ui/nameplate_frame.cpp data/client
git commit -m "Render cast/channel bars with interrupted flash on nameplates"
```

---

### Task 4: Cvar, Options toggle, localization

**Files:**
- Modify: `src/mmo_client/game_states/world_state.cpp` (register ~line 1877, unregister ~line 1998)
- Modify: `data/client/Interface/GameUI/OptionsFrame.lua` (~line 359, **submodule**)
- Modify: `data/client/Locales/Locale_enUS/Localization.txt` (**submodule**)
- Modify: `data/client/Locales/Locale_deDE/Localization.txt` (**submodule**)
- Modify: `data/client/Locales/Locale_frFR/Localization.txt` (**submodule**)
- Modify: `data/client/Locales/Locale_ruRU/Localization.txt` (**submodule**)

**Interfaces:**
- Consumes: cvar name `"NameplateShowCastBars"` and localization key `"NAMEPLATE_INTERRUPTED"` exactly as read in Task 3.
- Produces: registered cvar (default `"1"`), Options → Gameplay toggle, localized strings.

- [ ] **Step 1: Register/unregister the cvar**

In `src/mmo_client/game_states/world_state.cpp`, after the `NameplateShowFriendlyPets` registration (~line 1877):

```cpp
		ConsoleVarMgr::RegisterConsoleVar("NameplateShowCastBars", "Show cast bars on unit nameplates.", "1");
```

After the `NameplateShowFriendlyPets` unregistration (~line 1998):

```cpp
		ConsoleVarMgr::UnregisterConsoleVar("NameplateShowCastBars");
```

(The existing nameplate cvars assign to `s_nameplate*Var` statics used by the manager's filters; the cast bar cvar is read by name in `NameplateFrame`, so no static is needed.)

- [ ] **Step 2: Add the Options toggle row**

In `data/client/Interface/GameUI/OptionsFrame.lua`, after the `NameplateShowFriendlyPets` toggle entry (~line 359) and before the `NameplateDistance` dropdown:

```lua
			{
				type = "toggle",
				labelKey = "OPTIONS_NAMEPLATES_CAST_BARS",
				cvar = "NameplateShowCastBars",
				defaultValue = "1",
			},
```

(Note: label key uses the existing `OPTIONS_NAMEPLATES_` prefix of the sibling toggles; the spec's `OPTIONS_NAMEPLATE_CAST_BARS` spelling is superseded for consistency.)

- [ ] **Step 3: Add localization strings in all 4 locales**

Entry format is `\t(key = "KEY", string = "value")` — add both keys next to the existing `OPTIONS_NAMEPLATES_*` entries in each file:

`data/client/Locales/Locale_enUS/Localization.txt`:
```
	(key = "OPTIONS_NAMEPLATES_CAST_BARS", string = "Nameplate cast bars")
	(key = "NAMEPLATE_INTERRUPTED", string = "Interrupted")
```

`data/client/Locales/Locale_deDE/Localization.txt`:
```
	(key = "OPTIONS_NAMEPLATES_CAST_BARS", string = "Zauberbalken an Namensplaketten")
	(key = "NAMEPLATE_INTERRUPTED", string = "Unterbrochen")
```

`data/client/Locales/Locale_frFR/Localization.txt`:
```
	(key = "OPTIONS_NAMEPLATES_CAST_BARS", string = "Barres d'incantation des plaques")
	(key = "NAMEPLATE_INTERRUPTED", string = "Interrompu")
```

`data/client/Locales/Locale_ruRU/Localization.txt`:
```
	(key = "OPTIONS_NAMEPLATES_CAST_BARS", string = "Полосы применения на табличках")
	(key = "NAMEPLATE_INTERRUPTED", string = "Прервано")
```

- [ ] **Step 4: Build to verify**

```powershell
cmake --build build --config Debug -t mmo_client
```
Expected: builds clean (Lua/locale files are data, but the cvar registration is C++).

- [ ] **Step 5: Commit (submodule + main repo)**

```bash
cd data/client
git add Interface/GameUI/OptionsFrame.lua Locales/Locale_enUS/Localization.txt Locales/Locale_deDE/Localization.txt Locales/Locale_frFR/Localization.txt Locales/Locale_ruRU/Localization.txt
git commit -m "Add nameplate cast bar option toggle + localization"
cd ../..
git add src/mmo_client/game_states/world_state.cpp data/client
git commit -m "Register NameplateShowCastBars cvar"
```

---

### Task 5: Full verification

**Files:** none new.

**Interfaces:** none.

- [ ] **Step 1: Full test suite**

```powershell
cmake --build build --config Debug -t unit_tests game_server_unit_tests
./bin/Debug/unit_tests.exe
./bin/Debug/game_server_unit_tests.exe
```
Expected: all pass (adjust `bin/Debug/` to the actual output dir if needed).

- [ ] **Step 2: E2E suite (regression check — no behavior change expected)**

```powershell
$env:MMO_E2E_MYSQL_PASSWORD = "<mysql password>"
cmake --build build -t e2e_client login_server realm_server world_server --config Debug
powershell -File tools/e2e/e2e_run.ps1
```
Expected: exit 0, all scenarios green.

- [ ] **Step 3: Hand the manual in-game checklist to the user**

Nameplate rendering is not E2E-assertable; report these checks for manual verification:
1. Enemy caster: bar appears below its health plate with the spell name, fills left→right, clears on completion.
2. Channeled cast: bar starts full and drains right→left, ends promptly when the channel ends.
3. Interrupt a cast: grey-red "Interrupted" bar for ~0.8 s, then hides.
4. Console `NameplateShowCastBars 0` hides all plate cast bars instantly; Options → Gameplay checkbox round-trips.
5. Long spell name truncates with an ellipsis.
6. The player's own big cast bar behaves exactly as before.
