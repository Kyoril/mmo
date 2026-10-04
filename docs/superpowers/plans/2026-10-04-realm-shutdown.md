# Scheduled Realm Shutdown Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An operator schedules a realm shutdown (console or REST), players get a localized countdown in chat, and at zero characters are saved, world nodes and the realm exit with code 0, and Docker does not restart them.

**Architecture:** Pure countdown helpers live in the shared `game` library (schedule marks, time formatting, delay parsing). A `ShutdownManager` on the realm drives the countdown through an injected clock/scheduler (the realm `TimerQueue` in production, a fake in tests) and fires `announce`/`shutdownDue` signals. `program.cpp` turns `shutdownDue` into the sequence: kick players with a reason → tell world nodes to shut down (new auth opcode) → wait for them (max 15 s) → run the realm's existing graceful stop. Both servers' stop lambdas become idempotent named callables shared by the signal path and the new path.

**Tech Stack:** C++17, asio, Catch2 (`mmo_add_test` suites), Lua scenarios (E2E), React/TS (admin UI, separate repo `H:\mmo-admin`).

Spec: `docs/superpowers/specs/2026-10-04-realm-shutdown-design.md`

## Global Constraints

- Code style: Allman braces, braces on every `if`, tabs, `m_camelCase` members, `PascalCase` methods, `camelCase` locals, `#pragma once`, Doxygen on public members, header `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` on every new source file.
- No exceptions; use `ASSERT`/`VERIFY`/`ELOG`/`WLOG`/`ILOG`/`DLOG`.
- Absolute `GetAsyncTimeMs()` values never enter float math.
- Realm and world servers are single-threaded; no new threads, no new asio objects that would need their own `Stop()` (use the `TimerQueue`).
- Wire changes: bump `mmo::game::ProtocolVersion` 0x11 → 0x12 and `mmo::auth::ProtocolVersion` 0x07 → 0x08, run `python tools/protocol_version_check.py --update`, add rows to `docs/protocol_versions.md`.
- Localized strings in all four locales (`Locale_deDE`, `Locale_enUS`, `Locale_frFR`, `Locale_ruRU`); `data/client` is a submodule.
- GM level 3 = operator; only operators may schedule/cancel a shutdown.
- Never push to origin. Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Build: `cmake --build build --config Debug -t <target>`; tests: `bin/Debug/<suite>.exe "[tag]"` (Windows). If the worktree has no `build/` yet, configure first (Task 0).

---

### Task 0: Worktree setup

**Files:** none (environment only)

- [ ] **Step 1: Initialise submodules** (the worktree's `data/client` and `data/editor` are empty)

```powershell
git submodule update --init data/client data/editor
```
Expected: both directories populated. If `data/editor` is not needed by a build step, it may be skipped, but `data/client` is required for Task 7.

- [ ] **Step 2: Configure the build** (skip if `build/` exists)

```powershell
cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_TOOLS=ON -DMMO_WITH_DEV_COMMANDS=ON
```
Expected: configure succeeds.

- [ ] **Step 3: Baseline build of the touched targets**

```powershell
cmake --build build --config Debug -t realm_server world_server game_tests realm_server_tests
```
Expected: success (a failure here is pre-existing; report it rather than fixing it inside this plan).

---

### Task 1: Shared countdown helpers and GM levels (`game` library)

**Files:**
- Create: `src/shared/game/gm_level.h`
- Create: `src/shared/game/shutdown_countdown.h`
- Create: `src/shared/game/shutdown_countdown.cpp`
- Test: `src/tests/game_tests/shutdown_countdown_tests.cpp`

**Interfaces:**
- Produces:
  - `mmo::gm_level::Type { Player = 0, Gm = 1, SeniorGm = 2, Operator = 3 }`
  - `constexpr uint32 mmo::ShutdownCountdownCancelled = 0xFFFFFFFF;`
  - `constexpr uint32 mmo::MaxShutdownDelaySeconds = 7 * 24 * 3600;`
  - `uint32 mmo::NextShutdownAnnouncement(uint32 remainingSeconds);`
  - `mmo::shutdown_time_unit::Type { Hours, Minutes, Minute, Seconds }`, typedef `ShutdownTimeUnit`
  - `struct mmo::FormattedShutdownTime { String time; ShutdownTimeUnit unit; };`
  - `FormattedShutdownTime mmo::FormatShutdownTime(uint32 seconds);`
  - `bool mmo::ParseShutdownDelay(const String& text, uint32& out_seconds);`

- [ ] **Step 1: Write the failing tests**

`src/tests/game_tests/shutdown_countdown_tests.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game/shutdown_countdown.h"

#include <vector>

using namespace mmo;

namespace
{
	std::vector<uint32> MarksFrom(uint32 remaining)
	{
		std::vector<uint32> marks;
		while ((remaining = NextShutdownAnnouncement(remaining)) != 0)
		{
			marks.push_back(remaining);
		}
		return marks;
	}
}

TEST_CASE("Shutdown announcements follow the fixed schedule below 30 minutes", "[shutdown]")
{
	CHECK(MarksFrom(1800) == std::vector<uint32>{ 900, 600, 300, 240, 180, 120, 60, 45, 30, 15 });
	CHECK(MarksFrom(20) == std::vector<uint32>{ 15 });
	CHECK(MarksFrom(15).empty());
	CHECK(MarksFrom(1).empty());
	CHECK(NextShutdownAnnouncement(0) == 0);
}

TEST_CASE("Shutdown announcements are hourly above one hour", "[shutdown]")
{
	CHECK(NextShutdownAnnouncement(3 * 3600 + 5) == 3 * 3600);
	CHECK(NextShutdownAnnouncement(3 * 3600) == 2 * 3600);
	CHECK(NextShutdownAnnouncement(3601) == 3600);
	CHECK(NextShutdownAnnouncement(3600) == 1800);
	CHECK(NextShutdownAnnouncement(1900) == 1800);
}

TEST_CASE("Shutdown times are formatted with the matching unit", "[shutdown]")
{
	auto check = [](uint32 seconds, const char* text, ShutdownTimeUnit unit)
	{
		const FormattedShutdownTime formatted = FormatShutdownTime(seconds);
		CHECK(formatted.time == text);
		CHECK(formatted.unit == unit);
	};

	check(7200, "2:00:00", shutdown_time_unit::Hours);
	check(3600, "1:00:00", shutdown_time_unit::Hours);
	check(1800, "30:00", shutdown_time_unit::Minutes);
	check(90, "1:30", shutdown_time_unit::Minutes);
	check(60, "1:00", shutdown_time_unit::Minute);
	check(45, "0:45", shutdown_time_unit::Seconds);
	check(5, "0:05", shutdown_time_unit::Seconds);
}

TEST_CASE("Shutdown delays parse from seconds, m:ss and h:mm:ss", "[shutdown]")
{
	uint32 seconds = 99;
	CHECK(ParseShutdownDelay("0", seconds));
	CHECK(seconds == 0);
	CHECK(ParseShutdownDelay("90", seconds));
	CHECK(seconds == 90);
	CHECK(ParseShutdownDelay("30:00", seconds));
	CHECK(seconds == 1800);
	CHECK(ParseShutdownDelay("1:05:30", seconds));
	CHECK(seconds == 3930);
	CHECK(ParseShutdownDelay("120:00", seconds));
	CHECK(seconds == 7200);

	CHECK_FALSE(ParseShutdownDelay("", seconds));
	CHECK_FALSE(ParseShutdownDelay("abc", seconds));
	CHECK_FALSE(ParseShutdownDelay("1:60", seconds));
	CHECK_FALSE(ParseShutdownDelay("1:60:00", seconds));
	CHECK_FALSE(ParseShutdownDelay("1::00", seconds));
	CHECK_FALSE(ParseShutdownDelay("1:00:00:00", seconds));
	CHECK_FALSE(ParseShutdownDelay("-5", seconds));
	CHECK_FALSE(ParseShutdownDelay("99999999", seconds));
	CHECK_FALSE(ParseShutdownDelay("169:00:00", seconds));
	CHECK(ParseShutdownDelay("168:00:00", seconds));
	CHECK(seconds == MaxShutdownDelaySeconds);
}
```

- [ ] **Step 2: Run to verify it fails**

```powershell
cmake --build build --config Debug -t game_tests
```
Expected: compile error, `game/shutdown_countdown.h` not found. (A new `.cpp` in a test folder needs a CMake re-run to be globbed; the build triggers that because `mmo_add_test` uses `CONFIGURE_DEPENDS`. If it does not, run `cmake build` once.)

- [ ] **Step 3: Implement**

`src/shared/game/gm_level.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Game master levels as stored per account in the login database (gm_level). A higher
	/// level includes every right of the lower ones.
	namespace gm_level
	{
		enum Type : uint8
		{
			/// A regular player account.
			Player = 0,

			/// Basic game master: movement, teleport, inspection and moderation commands.
			Gm = 1,

			/// Senior game master: commands that alter characters (items, levels, spells).
			SeniorGm = 2,

			/// Operator: realm administration, such as scheduling a realm shutdown.
			Operator = 3,
		};
	}
}
```

`src/shared/game/shutdown_countdown.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Value of a ShutdownCountdown packet announcing that a pending shutdown was cancelled.
	constexpr uint32 ShutdownCountdownCancelled = 0xFFFFFFFF;

	/// Longest delay a realm shutdown may be scheduled with (one week).
	constexpr uint32 MaxShutdownDelaySeconds = 7 * 24 * 3600;

	/// Returns the next point (in remaining seconds) at which a pending shutdown is announced,
	/// strictly below remainingSeconds, or 0 when the next event is the shutdown itself.
	/// Marks: every full hour above one hour, then 30:00, 15:00, 10:00, every minute from 5:00
	/// to 1:00, then 0:45, 0:30 and 0:15.
	uint32 NextShutdownAnnouncement(uint32 remainingSeconds);

	/// The unit word a formatted shutdown time is shown with.
	namespace shutdown_time_unit
	{
		enum Type
		{
			/// One hour or more, formatted h:mm:ss.
			Hours,
			/// More than one minute, formatted m:ss.
			Minutes,
			/// Exactly one minute, formatted 1:00.
			Minute,
			/// Less than one minute, formatted 0:ss.
			Seconds,
		};
	}

	typedef shutdown_time_unit::Type ShutdownTimeUnit;

	/// A remaining shutdown time as shown to players.
	struct FormattedShutdownTime
	{
		/// The clock text, e.g. "4:00" or "1:00:00".
		String time;
		/// Which unit word belongs after the time.
		ShutdownTimeUnit unit;
	};

	/// Formats a remaining shutdown time for display.
	FormattedShutdownTime FormatShutdownTime(uint32 seconds);

	/// Parses a shutdown delay given as seconds ("90"), m:ss ("30:00") or h:mm:ss ("1:30:00").
	/// The leading component is unbounded, the others must be below 60.
	/// @returns false if the text is malformed or exceeds MaxShutdownDelaySeconds.
	bool ParseShutdownDelay(const String& text, uint32& out_seconds);
}
```

`src/shared/game/shutdown_countdown.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shutdown_countdown.h"

#include <cstdio>
#include <vector>

namespace mmo
{
	namespace
	{
		constexpr uint32 secondsPerHour = 3600;

		/// Announcement marks at or below one hour, descending.
		constexpr uint32 fixedMarks[] = { 1800, 900, 600, 300, 240, 180, 120, 60, 45, 30, 15 };
	}

	uint32 NextShutdownAnnouncement(const uint32 remainingSeconds)
	{
		if (remainingSeconds > secondsPerHour)
		{
			// The largest full hour strictly below the remaining time.
			return ((remainingSeconds - 1) / secondsPerHour) * secondsPerHour;
		}

		for (const uint32 mark : fixedMarks)
		{
			if (mark < remainingSeconds)
			{
				return mark;
			}
		}

		return 0;
	}

	FormattedShutdownTime FormatShutdownTime(const uint32 seconds)
	{
		char buffer[32];

		if (seconds >= secondsPerHour)
		{
			std::snprintf(buffer, sizeof(buffer), "%u:%02u:%02u", seconds / secondsPerHour, (seconds / 60) % 60, seconds % 60);
			return { buffer, shutdown_time_unit::Hours };
		}

		std::snprintf(buffer, sizeof(buffer), "%u:%02u", seconds / 60, seconds % 60);
		if (seconds > 60)
		{
			return { buffer, shutdown_time_unit::Minutes };
		}

		if (seconds == 60)
		{
			return { buffer, shutdown_time_unit::Minute };
		}

		return { buffer, shutdown_time_unit::Seconds };
	}

	bool ParseShutdownDelay(const String& text, uint32& out_seconds)
	{
		std::vector<uint32> parts;
		uint64 current = 0;
		size_t digits = 0;

		for (const char c : text)
		{
			if (c == ':')
			{
				if (digits == 0)
				{
					return false;
				}

				parts.push_back(static_cast<uint32>(current));
				current = 0;
				digits = 0;
				continue;
			}

			if (c < '0' || c > '9')
			{
				return false;
			}

			// Seven digits already exceed a week in seconds; stop before the value can overflow.
			if (++digits > 7)
			{
				return false;
			}

			current = current * 10 + static_cast<uint64>(c - '0');
		}

		if (digits == 0)
		{
			return false;
		}

		parts.push_back(static_cast<uint32>(current));
		if (parts.size() > 3)
		{
			return false;
		}

		// Every component after the first is a minutes or seconds field.
		for (size_t i = 1; i < parts.size(); ++i)
		{
			if (parts[i] >= 60)
			{
				return false;
			}
		}

		uint64 total = 0;
		for (const uint32 part : parts)
		{
			total = total * 60 + part;
		}

		if (total > MaxShutdownDelaySeconds)
		{
			return false;
		}

		out_seconds = static_cast<uint32>(total);
		return true;
	}
}
```

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Debug -t game_tests; bin/Debug/game_tests.exe "[shutdown]"
```
Expected: `All tests passed`.

- [ ] **Step 5: Commit**

```powershell
git add src/shared/game/gm_level.h src/shared/game/shutdown_countdown.h src/shared/game/shutdown_countdown.cpp src/tests/game_tests/shutdown_countdown_tests.cpp
git commit -m "feat(shutdown): countdown schedule, formatting and delay parsing helpers" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Wire protocol additions and version bumps

**Files:**
- Modify: `src/shared/game_protocol/game_protocol.h` (`ProtocolVersion` line 84; end of `client_realm_packet` ~line 439; end of `realm_client_packet` ~line 814; add `gm_shutdown_action` namespace after the `realm_client_packet` namespace)
- Modify: `src/shared/auth_protocol/auth_protocol.h` (`ProtocolVersion` line 34; `session_kick_reason`; end of `realm_world_packet` ~line 198)
- Modify: `src/shared/protocol_fingerprint.json` (regenerated)
- Modify: `docs/protocol_versions.md`

**Interfaces:**
- Produces:
  - `game::client_realm_packet::GmShutdown` — payload `uint8 action`, `uint32 delaySeconds`
  - `game::realm_client_packet::ShutdownCountdown` — payload `uint32 seconds` (`ShutdownCountdownCancelled` = cancelled)
  - `game::gm_shutdown_action::Type : uint8 { Start = 0, Cancel = 1 }`
  - `auth::session_kick_reason::RealmShutdown = 2`
  - `auth::realm_world_packet::Shutdown` — no payload

- [ ] **Step 1: Game protocol opcodes.** In `client_realm_packet`, directly before `/// Counter constant` / `Count_,`, after `CheatSetSubsystem`:

```cpp
				/// OPERATOR (gm_level::Operator). Schedules or cancels a realm shutdown. Handled by the
				/// realm; available in every build, not only with dev commands. Payload: uint8 action
				/// (gm_shutdown_action), uint32 delaySeconds (ignored for Cancel; 0 = immediately).
				GmShutdown,
```

In `realm_client_packet`, directly before its `Count_`, after `SubsystemStatus`:

```cpp
				/// A pending realm shutdown: sent to every player in the world at each announcement
				/// mark, and once on world entry while a shutdown is pending. Payload: uint32 seconds
				/// remaining, or 0xFFFFFFFF (ShutdownCountdownCancelled) when it was cancelled.
				ShutdownCountdown,
```

After the closing `}` of `namespace realm_client_packet`:

```cpp
		/// Action carried by a GmShutdown packet.
		namespace gm_shutdown_action
		{
			enum Type : uint8
			{
				/// Schedules a shutdown, replacing a pending one.
				Start = 0,
				/// Cancels the pending shutdown.
				Cancel = 1,
			};
		}
```

Bump: `constexpr uint32 ProtocolVersion = 0x00000012;`

- [ ] **Step 2: Auth protocol.** In `session_kick_reason`, after `AccountBanned = 1,`:

```cpp
				/// The realm is shutting down (scheduled shutdown reached zero).
				RealmShutdown = 2,
```

In `realm_world_packet`, after `SetSubsystemEnabled,`:

```cpp
				/// Tells the world node to save and remove its remaining players and shut down for
				/// good: it exits instead of reconnecting. Sent when a scheduled realm shutdown is
				/// due, after the realm has logged its players out. No payload.
				Shutdown,
```

Bump: `constexpr uint32 ProtocolVersion = 0x00000008;`

- [ ] **Step 3: Re-record the fingerprint**

```powershell
python tools/protocol_version_check.py --update; python tools/protocol_version_check.py
```
Expected: second run exits 0.

- [ ] **Step 4: Document.** In `docs/protocol_versions.md` set the table "Current" values to `8` (auth) and `18` (game), and add rows at the top of each history table:

```markdown
| 8 | 2026-10-04 | Scheduled realm shutdown: realm→world `Shutdown` opcode; `session_kick_reason::RealmShutdown` |
```
```markdown
| 18 | 2026-10-04 | Scheduled realm shutdown: operator `GmShutdown` (start/cancel + delay, handled by the realm in all builds) and server `ShutdownCountdown` (remaining seconds, 0xFFFFFFFF = cancelled) |
```

- [ ] **Step 5: Build the protocol suites**

```powershell
cmake --build build --config Debug -t game_protocol_tests auth_protocol_tests; bin/Debug/game_protocol_tests.exe; bin/Debug/auth_protocol_tests.exe
```
Expected: all pass.

- [ ] **Step 6: Commit**

```powershell
git add src/shared/game_protocol/game_protocol.h src/shared/auth_protocol/auth_protocol.h src/shared/protocol_fingerprint.json docs/protocol_versions.md
git commit -m "feat(shutdown): GmShutdown, ShutdownCountdown and realm->world Shutdown opcodes" -m "Bumps game protocol to 18 and auth protocol to 8." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Realm `ShutdownManager`

**Files:**
- Create: `src/realm_server/shutdown_manager.h`
- Create: `src/realm_server/shutdown_manager.cpp`
- Modify: `src/tests/realm_server_tests/CMakeLists.txt` (add the source)
- Test: `src/tests/realm_server_tests/test_shutdown_manager.cpp`

**Interfaces:**
- Consumes: `NextShutdownAnnouncement`, `ShutdownCountdownCancelled` (Task 1)
- Produces:

```cpp
class ShutdownManager final : public NonCopyable
{
public:
	typedef std::function<GameTime()> Clock;
	typedef std::function<void(std::function<void()>, GameTime)> Scheduler;
	ShutdownManager(Clock clock, Scheduler scheduler);
	void Schedule(uint32 delaySeconds);
	bool Cancel();
	bool IsPending() const;
	uint32 GetRemainingSeconds() const;
	signal<void(uint32)> announce;   // remaining seconds or ShutdownCountdownCancelled
	signal<void()> shutdownDue;
};
```

- [ ] **Step 1: Write the failing tests**

`src/tests/realm_server_tests/test_shutdown_manager.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "realm_server/shutdown_manager.h"
#include "game/shutdown_countdown.h"

#include <algorithm>
#include <vector>

using namespace mmo;

namespace
{
	/// Stands in for the realm's TimerQueue: events run in time order when time is advanced.
	struct FakeTimers
	{
		struct Event
		{
			std::function<void()> callback;
			GameTime time;
		};

		GameTime now = 1000000;
		std::vector<Event> events;

		ShutdownManager::Clock Clock()
		{
			return [this]() { return now; };
		}

		ShutdownManager::Scheduler Scheduler()
		{
			return [this](std::function<void()> callback, const GameTime time) { events.push_back({ std::move(callback), time }); };
		}

		/// Runs every event due up to and including `until`, earliest first.
		void AdvanceTo(const GameTime until)
		{
			while (true)
			{
				const auto next = std::min_element(events.begin(), events.end(),
					[](const Event& a, const Event& b) { return a.time < b.time; });
				if (next == events.end() || next->time > until)
				{
					break;
				}

				const Event event = *next;
				events.erase(next);
				now = std::max(now, event.time);
				event.callback();
			}

			now = until;
		}
	};

	struct Recorder
	{
		std::vector<uint32> announcements;
		uint32 dueCount = 0;
		scoped_connection announced;
		scoped_connection due;

		explicit Recorder(ShutdownManager& manager)
			: announced(manager.announce.connect([this](const uint32 seconds) { announcements.push_back(seconds); }))
			, due(manager.shutdownDue.connect([this]() { ++dueCount; }))
		{
		}
	};
}

TEST_CASE("A short shutdown announces immediately, at 0:15 and then becomes due", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);
	const GameTime start = timers.now;

	manager.Schedule(20);
	CHECK(manager.IsPending());
	CHECK(manager.GetRemainingSeconds() == 20);
	CHECK(recorder.announcements == std::vector<uint32>{ 20 });

	timers.AdvanceTo(start + 5000);
	CHECK(recorder.announcements == std::vector<uint32>{ 20, 15 });
	CHECK(recorder.dueCount == 0);

	timers.AdvanceTo(start + 19999);
	CHECK(recorder.dueCount == 0);

	timers.AdvanceTo(start + 20000);
	CHECK(recorder.dueCount == 1);
	CHECK_FALSE(manager.IsPending());
}

TEST_CASE("A long shutdown walks the whole announcement schedule", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);
	const GameTime start = timers.now;

	manager.Schedule(3700);
	timers.AdvanceTo(start + 3700 * 1000);

	CHECK(recorder.announcements == std::vector<uint32>{ 3700, 3600, 1800, 900, 600, 300, 240, 180, 120, 60, 45, 30, 15 });
	CHECK(recorder.dueCount == 1);
}

TEST_CASE("Cancelling announces it and silences every queued event", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);
	const GameTime start = timers.now;

	manager.Schedule(60);
	CHECK(manager.Cancel());
	CHECK_FALSE(manager.IsPending());
	CHECK(manager.GetRemainingSeconds() == 0);
	CHECK(recorder.announcements == std::vector<uint32>{ 60, ShutdownCountdownCancelled });

	timers.AdvanceTo(start + 120 * 1000);
	CHECK(recorder.announcements.size() == 2);
	CHECK(recorder.dueCount == 0);

	CHECK_FALSE(manager.Cancel());
}

TEST_CASE("Scheduling again replaces the pending shutdown", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);
	const GameTime start = timers.now;

	manager.Schedule(600);
	manager.Schedule(30);
	timers.AdvanceTo(start + 600 * 1000);

	CHECK(recorder.announcements == std::vector<uint32>{ 600, 30, 15 });
	CHECK(recorder.dueCount == 1);
}

TEST_CASE("A zero delay becomes due on the next timer run without announcing", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	Recorder recorder(manager);

	manager.Schedule(0);
	CHECK(recorder.dueCount == 0);

	timers.AdvanceTo(timers.now);
	CHECK(recorder.announcements.empty());
	CHECK(recorder.dueCount == 1);
}

TEST_CASE("Remaining seconds round up while a shutdown is pending", "[shutdown]")
{
	FakeTimers timers;
	ShutdownManager manager(timers.Clock(), timers.Scheduler());
	const GameTime start = timers.now;

	manager.Schedule(100);
	timers.now = start + 500;
	CHECK(manager.GetRemainingSeconds() == 100);
	timers.now = start + 1000;
	CHECK(manager.GetRemainingSeconds() == 99);
}
```

Add to `src/tests/realm_server_tests/CMakeLists.txt` inside `target_sources(...)`:

```cmake
	${CMAKE_CURRENT_SOURCE_DIR}/../../realm_server/shutdown_manager.cpp
```

- [ ] **Step 2: Run to verify it fails**

```powershell
cmake --build build --config Debug -t realm_server_tests
```
Expected: compile error, `realm_server/shutdown_manager.h` not found.

- [ ] **Step 3: Implement**

`src/realm_server/shutdown_manager.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"

#include <functional>

namespace mmo
{
	/// Counts down a scheduled realm shutdown and announces it at the marks of
	/// NextShutdownAnnouncement.
	///
	/// Time and timers are injected so the countdown can be tested without an io_service: the realm
	/// passes its TimerQueue. Queued timer events cannot be cancelled there, so every schedule and
	/// cancel bumps a generation counter and events from an older generation do nothing.
	///
	/// **Threading:** single-threaded, like the rest of the realm server.
	class ShutdownManager final : public NonCopyable
	{
	public:
		/// Returns the current time in milliseconds.
		typedef std::function<GameTime()> Clock;

		/// Runs a callback once the given time (milliseconds, same base as Clock) is reached.
		typedef std::function<void(std::function<void()>, GameTime)> Scheduler;

	public:
		/// Creates an idle shutdown manager.
		ShutdownManager(Clock clock, Scheduler scheduler);

	public:
		/// Schedules the shutdown, replacing a pending one. Announces the exact remaining time
		/// right away (except for a delay of 0, which only fires shutdownDue on the next timer run).
		/// @param delaySeconds Seconds until the shutdown; must not exceed MaxShutdownDelaySeconds.
		void Schedule(uint32 delaySeconds);

		/// Cancels the pending shutdown and announces the cancellation.
		/// @returns false if no shutdown was pending.
		bool Cancel();

		/// Whether a shutdown is currently scheduled.
		[[nodiscard]] bool IsPending() const { return m_pending; }

		/// Seconds until the pending shutdown, rounded up; 0 if none is pending.
		[[nodiscard]] uint32 GetRemainingSeconds() const;

	public:
		/// Fired with the remaining seconds at every announcement mark, or with
		/// ShutdownCountdownCancelled when a pending shutdown is cancelled.
		signal<void(uint32)> announce;

		/// Fired once when a pending shutdown reaches zero.
		signal<void()> shutdownDue;

	private:
		/// Queues the timer event for the next mark below remainingSeconds.
		void ArmNext(uint32 remainingSeconds);

		/// Handles a timer event of the given generation for the given mark (0 = due).
		void OnTimer(uint32 generation, uint32 mark);

	private:
		Clock m_clock;
		Scheduler m_scheduler;
		bool m_pending = false;
		GameTime m_deadline = 0;
		uint32 m_generation = 0;
	};
}
```

`src/realm_server/shutdown_manager.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shutdown_manager.h"

#include "base/constants.h"
#include "base/macros.h"
#include "game/shutdown_countdown.h"

namespace mmo
{
	ShutdownManager::ShutdownManager(Clock clock, Scheduler scheduler)
		: m_clock(std::move(clock))
		, m_scheduler(std::move(scheduler))
	{
	}

	void ShutdownManager::Schedule(const uint32 delaySeconds)
	{
		ASSERT(delaySeconds <= MaxShutdownDelaySeconds);

		++m_generation;
		m_pending = true;
		m_deadline = m_clock() + static_cast<GameTime>(delaySeconds) * constants::OneSecond;

		if (delaySeconds > 0)
		{
			announce(delaySeconds);
		}

		ArmNext(delaySeconds);
	}

	bool ShutdownManager::Cancel()
	{
		if (!m_pending)
		{
			return false;
		}

		++m_generation;
		m_pending = false;
		announce(ShutdownCountdownCancelled);
		return true;
	}

	uint32 ShutdownManager::GetRemainingSeconds() const
	{
		if (!m_pending)
		{
			return 0;
		}

		const GameTime now = m_clock();
		if (now >= m_deadline)
		{
			return 0;
		}

		return static_cast<uint32>((m_deadline - now + constants::OneSecond - 1) / constants::OneSecond);
	}

	void ShutdownManager::ArmNext(const uint32 remainingSeconds)
	{
		const uint32 mark = NextShutdownAnnouncement(remainingSeconds);
		const uint32 generation = m_generation;
		m_scheduler([this, generation, mark]() { OnTimer(generation, mark); },
			m_deadline - static_cast<GameTime>(mark) * constants::OneSecond);
	}

	void ShutdownManager::OnTimer(const uint32 generation, const uint32 mark)
	{
		if (generation != m_generation || !m_pending)
		{
			return;
		}

		if (mark == 0)
		{
			++m_generation;
			m_pending = false;
			shutdownDue();
			return;
		}

		announce(mark);
		ArmNext(mark);
	}
}
```

If `constants::OneSecond` is not in `base/constants.h`, find it with `Grep "OneSecond ="` (the world server's `RealmConnector` uses `constants::OneSecond`) and include that header instead.

- [ ] **Step 4: Run the tests**

```powershell
cmake --build build --config Debug -t realm_server_tests; bin/Debug/realm_server_tests.exe "[shutdown]"
```
Expected: `All tests passed`.

- [ ] **Step 5: Commit**

```powershell
git add src/realm_server/shutdown_manager.h src/realm_server/shutdown_manager.cpp src/tests/realm_server_tests/CMakeLists.txt src/tests/realm_server_tests/test_shutdown_manager.cpp
git commit -m "feat(shutdown): realm ShutdownManager with announcement schedule and cancel" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Realm wiring — broadcast, GM command, world shutdown, sequence

**Files:**
- Modify: `src/realm_server/player_manager.h/.cpp` (shutdown manager pointer, `BroadcastShutdownCountdown`, `DisconnectAll` reason)
- Modify: `src/realm_server/player.h/.cpp` (`SendShutdownCountdown`, `OnGmShutdown`, world-entry countdown, named GM levels)
- Modify: `src/realm_server/world.h/.cpp` (`SendShutdown`)
- Modify: `src/realm_server/world_manager.h/.cpp` (`BroadcastShutdown`, `GetWorldCount`)
- Modify: `src/realm_server/program.cpp` (manager, named idempotent stop, sequence)
- Modify: `src/tests/realm_server_tests/test_stubs.cpp` (stub `SendShutdownCountdown`)

**Interfaces:**
- Consumes: Task 2 opcodes/enums, Task 3 `ShutdownManager`, Task 1 `gm_level`, `MaxShutdownDelaySeconds`.
- Produces: `PlayerManager::SetShutdownManager(ShutdownManager&)`, `ShutdownManager* PlayerManager::GetShutdownManager() const`, `void PlayerManager::BroadcastShutdownCountdown(uint32)`, `void PlayerManager::DisconnectAll(std::optional<auth::SessionKickReason> reason = std::nullopt)`, `void Player::SendShutdownCountdown(uint32)`, `void World::SendShutdown() const`, `void WorldManager::BroadcastShutdown()`, `size_t WorldManager::GetWorldCount()`.

- [ ] **Step 1: PlayerManager.** In `player_manager.h` add a forward declaration `class ShutdownManager;` next to `class MOTDManager;`, include `auth_protocol/auth_protocol.h` and `<optional>` if not already present, and add public members:

```cpp
		/// Sets the realm's shutdown manager (pending shutdowns are announced to entering players).
		void SetShutdownManager(ShutdownManager& shutdownManager) { m_shutdownManager = &shutdownManager; }

		/// Gets the realm's shutdown manager, or nullptr if none was set.
		ShutdownManager* GetShutdownManager() const { return m_shutdownManager; }

		/// Sends a shutdown countdown (remaining seconds or ShutdownCountdownCancelled) to every
		/// player that is in the world.
		void BroadcastShutdownCountdown(uint32 seconds);
```

Change the `DisconnectAll` declaration to:

```cpp
		/// Disconnects every managed player. Used at shutdown so peers see a closed connection.
		/// @param reason Sent to each client before the disconnect, if set.
		void DisconnectAll(std::optional<auth::SessionKickReason> reason = std::nullopt);
```

Private member: `ShutdownManager* m_shutdownManager = nullptr;`

In `player_manager.cpp` change `DisconnectAll` to take and forward the reason (`player->Kick(reason);`) and add:

```cpp
	void PlayerManager::BroadcastShutdownCountdown(const uint32 seconds)
	{
		ForEachPlayer([seconds](Player& player)
		{
			if (player.HasCharacterGuid())
			{
				player.SendShutdownCountdown(seconds);
			}
		});
	}
```

In `test_stubs.cpp` add under the Player stubs: `void Player::SendShutdownCountdown(uint32) {}`

- [ ] **Step 2: Player.** In `player.h` add next to `SendMessageOfTheDay`:

```cpp
		/// Sends a pending shutdown's remaining seconds (or ShutdownCountdownCancelled) to the client.
		void SendShutdownCountdown(uint32 seconds);
```

and next to `OnCheatSetSubsystem`: `PacketParseResult OnGmShutdown(game::IncomingPacket &packet);`

In `player.cpp` include `"shutdown_manager.h"`, `"game/gm_level.h"`, `"game/shutdown_countdown.h"`. Add:

```cpp
	void Player::SendShutdownCountdown(const uint32 seconds)
	{
		if (!m_connection)
		{
			return;
		}

		m_connection->sendSinglePacket([seconds](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::ShutdownCountdown);
			packet << io::write<uint32>(seconds);
			packet.Finish();
		});
	}

	PacketParseResult Player::OnGmShutdown(game::IncomingPacket &packet)
	{
		uint8 action = 0;
		uint32 delaySeconds = 0;
		if (!(packet >> io::read<uint8>(action) >> io::read<uint32>(delaySeconds)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!HasGMLevel(gm_level::Operator))
		{
			WLOG("Account " << GetAccountName() << " attempted to schedule a realm shutdown without operator rights");
			return PacketParseResult::Pass;
		}

		ShutdownManager* shutdown = GetManager().GetShutdownManager();
		if (!shutdown)
		{
			return PacketParseResult::Pass;
		}

		if (action == game::gm_shutdown_action::Cancel)
		{
			if (shutdown->Cancel())
			{
				ILOG("Account " << GetAccountName() << " cancelled the pending realm shutdown");
			}
			return PacketParseResult::Pass;
		}

		if (action != game::gm_shutdown_action::Start || delaySeconds > MaxShutdownDelaySeconds)
		{
			WLOG("Account " << GetAccountName() << " sent an invalid shutdown request (action " << static_cast<uint32>(action) << ", delay " << delaySeconds << ")");
			return PacketParseResult::Pass;
		}

		ILOG("Account " << GetAccountName() << " scheduled a realm shutdown in " << delaySeconds << " seconds");
		shutdown->Schedule(delaySeconds);
		return PacketParseResult::Pass;
	}
```

Register the handler **outside** `#if MMO_WITH_DEV_COMMANDS`, directly after `RegisterPacketHandler(game::client_realm_packet::BugReport, ...)` (~line 3263), and clear it after `ClearPacketHandler(game::client_realm_packet::BugReport);` (~line 3300):

```cpp
			RegisterPacketHandler(game::client_realm_packet::GmShutdown, *this, &Player::OnGmShutdown);
```
```cpp
			ClearPacketHandler(game::client_realm_packet::GmShutdown);
```

On world entry, directly after the MOTD block (`if (!motd.empty()) { SendMessageOfTheDay(motd); }`, ~line 3373):

```cpp
		// A shutdown already counting down is announced once to players arriving late
		if (const ShutdownManager* shutdown = GetManager().GetShutdownManager(); shutdown && shutdown->IsPending())
		{
			SendShutdownCountdown(shutdown->GetRemainingSeconds());
		}
```

Replace every `HasGMLevel(1)` with `HasGMLevel(gm_level::Gm)` and every `HasGMLevel(2)` with `HasGMLevel(gm_level::SeniorGm)` in `player.cpp` (`Grep "HasGMLevel\(" src/realm_server` to find them all). Behaviour is unchanged.

- [ ] **Step 3: World / WorldManager.** In `world.h` next to `SendSetSubsystemEnabled`:

```cpp
		/// Tells the world node to remove its remaining players and shut down for good.
		void SendShutdown() const;
```

`world.cpp`:

```cpp
	void World::SendShutdown() const
	{
		m_connection->sendSinglePacket([](auth::OutgoingPacket& packet)
		{
			packet.Start(auth::realm_world_packet::Shutdown);
			packet.Finish();
		});
	}
```

`world_manager.h` public:

```cpp
		/// Tells every connected world node to shut down for good (scheduled realm shutdown).
		void BroadcastShutdown();

		/// Number of currently connected world nodes.
		size_t GetWorldCount();
```

`world_manager.cpp` (mirror the locking used by `BroadcastTimeOfDay` in the same file):

```cpp
	void WorldManager::BroadcastShutdown()
	{
		std::scoped_lock lock{ m_worldsMutex };
		for (const auto& world : m_worlds)
		{
			world->SendShutdown();
		}
	}

	size_t WorldManager::GetWorldCount()
	{
		std::scoped_lock lock{ m_worldsMutex };
		return m_worlds.size();
	}
```

If `BroadcastTimeOfDay` only sends to authenticated worlds (it checks a flag), apply the same check in `BroadcastShutdown`; unauthenticated connections have no handlers and are closed by `DisconnectAll` anyway.

- [ ] **Step 4: program.cpp — manager and announcements.** Include `"shutdown_manager.h"` and `"game/shutdown_countdown.h"`. Directly after `playerManager.SetRealmName(config.realmName);`:

```cpp
		// Scheduled shutdowns count down on the timer queue, which the shutdown handler stops.
		ShutdownManager shutdownManager{
			[&timerQueue]() { return timerQueue.GetNow(); },
			[&timerQueue](std::function<void()> callback, const GameTime time) { timerQueue.AddEvent(std::move(callback), time); } };
		playerManager.SetShutdownManager(shutdownManager);
		const scoped_connection shutdownAnnounced{ shutdownManager.announce.connect([&playerManager](const uint32 seconds)
		{
			playerManager.BroadcastShutdownCountdown(seconds);
		}) };
```

(`timerQueue` must be declared before this point; it is, since earlier lambdas capture it — if not, move this block below its declaration.)

After `webService->SetWorldManager(worldManager);` add `webService->SetShutdownManager(shutdownManager);` (method added in Task 5; if Task 5 is not done yet, add this line there instead).

- [ ] **Step 5: program.cpp — named idempotent stop and the sequence.** Replace the `InstallShutdownHandler(ioService, [&worldServer, ...]() { ... });` call with a named callable holding the **unchanged** body, plus an idempotency guard, and install it:

```cpp
		// Declared before the handler that captures it, so the handler can cancel its own wait.
		std::unique_ptr<asio::signal_set> shutdownSignals;

		// Shared by the signal handler and the scheduled shutdown below, and safe to call twice:
		// a SIGTERM arriving while a scheduled shutdown is winding down must not stop things again.
		bool realmStopped = false;
		const std::function<void()> stopRealm = [&worldServer, &playerServer, &webService, &playerManager, &worldManager,
			 &shutdownSignals, &timerQueue, &ioWork, &databasePool, &realmStopped]()
		{
			if (realmStopped)
			{
				return;
			}
			realmStopped = true;

			ILOG("Stopping the realm server cleanly");

			// ... the existing handler body, unchanged, from "Stop taking new work first" through
			// "databasePool->Stop();" ...
		};
		shutdownSignals = InstallShutdownHandler(ioService, [&stopRealm]() { stopRealm(); });

		// A scheduled shutdown: log the players out first (their logout saves travel world -> realm
		// on the same links), then tell the world nodes to go, and stop the realm once they have
		// disconnected or the grace period ran out. The database pool keeps running until then, so
		// the character data the world nodes send back is still written.
		const GameTime worldGracePeriod = constants::OneSecond * 15;
		std::function<void(GameTime)> stopOnceWorldsAreGone;
		stopOnceWorldsAreGone = [&stopOnceWorldsAreGone, &worldManager, &timerQueue, &ioService, &stopRealm](const GameTime deadline)
		{
			const size_t remaining = worldManager.GetWorldCount();
			if (remaining == 0 || timerQueue.GetNow() >= deadline)
			{
				if (remaining != 0)
				{
					WLOG(remaining << " world node(s) did not disconnect in time - stopping anyway");
				}

				// Posted rather than run here: this is a timer queue callback, and stopping
				// stops that queue.
				ioService.post([&stopRealm]() { stopRealm(); });
				return;
			}

			timerQueue.AddEvent([&stopOnceWorldsAreGone, deadline]() { stopOnceWorldsAreGone(deadline); }, timerQueue.GetNow() + 250);
		};

		const scoped_connection shutdownDue{ shutdownManager.shutdownDue.connect(
			[&playerServer, &playerManager, &worldManager, &timerQueue, &stopOnceWorldsAreGone, worldGracePeriod]()
		{
			ILOG("Scheduled realm shutdown is due - logging out players and stopping world nodes");
			playerServer->Stop();
			playerManager.DisconnectAll(auth::session_kick_reason::RealmShutdown);
			worldManager.BroadcastShutdown();
			stopOnceWorldsAreGone(timerQueue.GetNow() + worldGracePeriod);
		}) };
```

Keep the existing comments of the moved body. Remove the old first line `ILOG("Shutdown signal received - stopping cleanly");` (replaced by the new log line). Include `"base/constants.h"` if `constants::OneSecond` is not yet visible.

- [ ] **Step 6: Build and run the realm suite**

```powershell
cmake --build build --config Debug -t realm_server realm_server_tests; bin/Debug/realm_server_tests.exe
```
Expected: build succeeds, all realm tests pass.

- [ ] **Step 7: Commit**

```powershell
git add src/realm_server src/tests/realm_server_tests/test_stubs.cpp
git commit -m "feat(shutdown): realm broadcasts countdown, handles GmShutdown and runs the shutdown sequence" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Realm REST API, Bruno requests, docs

**Files:**
- Modify: `src/realm_server/web_service.h` (shutdown manager accessor)
- Modify: `src/realm_server/web_client.h/.cpp` (routes)
- Modify: `bruno/mmo-dev/realm-server/Shutdown.bru`; Create: `bruno/mmo-dev/realm-server/Shutdown Cancel.bru`, `bruno/mmo-dev/realm-server/Shutdown Status.bru`
- Modify: `docs/realm_server_api.md`

**Interfaces:**
- Consumes: `ShutdownManager` (Task 3), `MaxShutdownDelaySeconds` (Task 1).
- Produces: `WebService::SetShutdownManager(ShutdownManager&)`, `ShutdownManager* WebService::GetShutdownManager() const`; routes `GET /shutdown`, `POST /shutdown` (`delay`), `POST /shutdown/cancel`.

- [ ] **Step 1: WebService accessor.** In `web_service.h` add `class ShutdownManager;` to the forward declarations and, next to the world manager accessors:

```cpp
		/// Sets the shutdown manager that /shutdown schedules and cancels through.
		void SetShutdownManager(ShutdownManager &shutdownManager) { m_shutdownManager = &shutdownManager; }
		/// Gets the shutdown manager, or nullptr if none was set.
		ShutdownManager *GetShutdownManager() const { return m_shutdownManager; }
```

private: `ShutdownManager *m_shutdownManager = nullptr;`

- [ ] **Step 2: Routes.** In `web_client.h` replace the `handleShutdown` declaration with:

```cpp
		/// GET /shutdown: reports whether a shutdown is pending and the seconds remaining.
		void handleGetShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		/// POST /shutdown: schedules a graceful realm shutdown ('delay' in seconds, default 0 = now).
		void handleShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		/// POST /shutdown/cancel: cancels the pending shutdown.
		void handleCancelShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const;
```

In `web_client.cpp` include `"shutdown_manager.h"`, `"game/shutdown_countdown.h"`, `<charconv>`; register next to the existing `/shutdown` route:

```cpp
		RegisterRoute(Type::Get, "/shutdown", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			handleGetShutdown(req, response);
		});

		RegisterRoute(Type::Post, "/shutdown/cancel", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			handleCancelShutdown(req, response);
		});
```

Replace `handleShutdown` and add the two handlers. Use the same 400/409 response helper the existing handlers use (look at `handleSetMotd` for how it sets a status code with `SendJsonResponse`; if it uses e.g. `response.setStatus(net::http::OutgoingAnswer::BadRequest)`, mirror that exactly):

```cpp
	void WebClient::handleGetShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const ShutdownManager* shutdown = m_service.GetShutdownManager();

		json jsonResponse;
		jsonResponse["pending"] = shutdown && shutdown->IsPending();
		jsonResponse["remaining"] = shutdown ? shutdown->GetRemainingSeconds() : 0;
		SendJsonResponse(response, jsonResponse);
	}

	void WebClient::handleShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		ShutdownManager* shutdown = m_service.GetShutdownManager();
		if (!shutdown)
		{
			// same 500-style error the other handlers use for a missing dependency
			json jsonResponse;
			jsonResponse["status"] = "UNAVAILABLE";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		uint32 delaySeconds = 0;
		const auto& arguments = request.getPostFormArguments();
		if (const auto it = arguments.find("delay"); it != arguments.end() && !it->second.empty())
		{
			const String& text = it->second;
			const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), delaySeconds);
			if (ec != std::errc() || ptr != text.data() + text.size() || delaySeconds > MaxShutdownDelaySeconds)
			{
				// 400 Bad Request, as handleSetMotd does for MISSING_PARAMETER
				json jsonResponse;
				jsonResponse["status"] = "INVALID_PARAMETER";
				jsonResponse["message"] = "delay must be a whole number of seconds between 0 and " + std::to_string(MaxShutdownDelaySeconds);
				SendJsonResponse(response, jsonResponse);
				return;
			}
		}

		ILOG("Realm shutdown scheduled via web API in " << delaySeconds << " seconds");
		shutdown->Schedule(delaySeconds);

		json jsonResponse;
		jsonResponse["status"] = "SUCCESS";
		jsonResponse["delay"] = delaySeconds;
		SendJsonResponse(response, jsonResponse);
	}

	void WebClient::handleCancelShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		ShutdownManager* shutdown = m_service.GetShutdownManager();
		if (!shutdown || !shutdown->Cancel())
		{
			// 409 Conflict
			json jsonResponse;
			jsonResponse["status"] = "NOT_PENDING";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		ILOG("Realm shutdown cancelled via web API");

		json jsonResponse;
		jsonResponse["status"] = "SUCCESS";
		SendJsonResponse(response, jsonResponse);
	}
```

Before writing these, read `handleSetMotd` (~line 212) and copy its exact mechanism for non-200 statuses into the three marked places (400 for `INVALID_PARAMETER`, 409 for `NOT_PENDING`, 503 for `UNAVAILABLE`). If `getPostFormArguments()` returns a different container type than a map with `find`, follow `handleSetMotd`'s lookup instead. Remove the old `ioService.stop()` body entirely.

- [ ] **Step 3: Bruno.** Read `bruno/mmo-dev/realm-server/Shutdown.bru` and an existing POST with a form body (e.g. the MOTD one) for the file format. Make `Shutdown.bru` send form body `delay: 300`; create `Shutdown Cancel.bru` (POST `{{realmUrl}}/shutdown/cancel`, same auth block) and `Shutdown Status.bru` (GET `{{realmUrl}}/shutdown`), copying the variable names and `seq` numbering style of the existing files.

- [ ] **Step 4: Docs.** In `docs/realm_server_api.md`, replace the `/shutdown` section with the three endpoints, their parameters and responses (table from the spec, section 4), and a note: "A scheduled shutdown announces itself to players in chat, logs them out, stops every connected world node and then the realm; all exit with code 0."

- [ ] **Step 5: Build and smoke-test against the E2E stack**

```powershell
cmake --build build --config Debug -t realm_server world_server login_server e2e_client
$env:MMO_E2E_MYSQL_PASSWORD = "<from the e2e-test-harness memory>"
powershell -File tools/e2e/e2e_up.ps1
$auth = @{ Authorization = "Basic " + [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("mmo-e2e:e2e-secret")) }
Invoke-RestMethod -Uri http://127.0.0.1:18092/shutdown -Headers $auth
Invoke-RestMethod -Uri http://127.0.0.1:18092/shutdown -Method Post -Headers $auth -Body @{ delay = 600 }
Invoke-RestMethod -Uri http://127.0.0.1:18092/shutdown -Headers $auth
Invoke-RestMethod -Uri http://127.0.0.1:18092/shutdown/cancel -Method Post -Headers $auth
Invoke-RestMethod -Uri http://127.0.0.1:18092/shutdown/cancel -Method Post -Headers $auth -SkipHttpErrorCheck -StatusCodeVariable code; $code
powershell -File tools/e2e/e2e_down.ps1
```
Expected: `pending=False`; `SUCCESS delay=600`; `pending=True remaining≈600`; `SUCCESS`; second cancel `NOT_PENDING` with status 409.

- [ ] **Step 6: Commit**

```powershell
git add src/realm_server/web_service.h src/realm_server/web_client.h src/realm_server/web_client.cpp src/realm_server/program.cpp bruno/mmo-dev/realm-server docs/realm_server_api.md
git commit -m "feat(shutdown): realm REST endpoints to schedule, cancel and query a shutdown" -m "POST /shutdown is now graceful instead of stopping the io service." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: World node — handle `Shutdown`, exit cleanly

**Files:**
- Modify: `src/world_server/player_manager.h/.cpp` (`GetCharacterGuids`)
- Modify: `src/world_server/realm_connector.h/.cpp` (`shutdownRequested`, `OnShutdown`)
- Modify: `src/world_server/program.cpp` (named idempotent stop, wire the signal)

**Interfaces:**
- Consumes: `auth::realm_world_packet::Shutdown` (Task 2).
- Produces: `signal<void()> RealmConnector::shutdownRequested`, `std::vector<ObjectGuid> PlayerManager::GetCharacterGuids() const`.

- [ ] **Step 1: PlayerManager.** `player_manager.h` (world) public, include `<vector>`:

```cpp
		/// Character guids of every player currently on this node.
		std::vector<ObjectGuid> GetCharacterGuids() const;
```

`player_manager.cpp`:

```cpp
	std::vector<ObjectGuid> PlayerManager::GetCharacterGuids() const
	{
		std::vector<ObjectGuid> guids;
		guids.reserve(m_players.size());
		for (const auto& [guid, player] : m_players)
		{
			guids.push_back(guid);
		}
		return guids;
	}
```

- [ ] **Step 2: RealmConnector.** `realm_connector.h`: include `"base/signal.h"`, add public `signal<void()> shutdownRequested;` with doc comment "Fired when the realm orders this node to shut down for good (scheduled realm shutdown).", and private `PacketParseResult OnShutdown(auth::IncomingPacket& packet);` next to `OnSetSubsystemEnabled`.

`realm_connector.cpp`: register after `SetSubsystemEnabled` in `OnLogonProof` (~line 535):

```cpp
				RegisterPacketHandler(auth::realm_world_packet::Shutdown, *this, &RealmConnector::OnShutdown);
```

Add:

```cpp
	PacketParseResult RealmConnector::OnShutdown(auth::IncomingPacket& packet)
	{
		ILOG("The realm ordered this world node to shut down");

		// The realm logs its players out before sending this, and those PlayerCharacterLeave
		// packets arrived first on this link. Anyone still here leaves the same way, so their
		// character data goes back to the realm before the link closes.
		for (const ObjectGuid characterGuid : m_playerManager.GetCharacterGuids())
		{
			if (const auto player = m_playerManager.GetPlayerByCharacterGuid(characterGuid))
			{
				m_playerManager.RemovePlayer(player);
				NotifyWorldInstanceLeft(characterGuid, auth::world_left_reason::Disconnect);
			}
		}

		shutdownRequested();
		return PacketParseResult::Pass;
	}
```

- [ ] **Step 3: program.cpp (world).** Replace the inline `InstallShutdownHandler` lambda with a named, idempotent callable holding the unchanged body, and wire the connector signal. Posting matters: the signal fires inside the connector's own packet handler, and stopping closes that connection.

```cpp
		// Declared before the handler that captures it, so the handler can cancel its own wait.
		std::unique_ptr<asio::signal_set> shutdownSignals;

		// Shared by the signal handler and a realm-ordered shutdown; safe to call twice.
		bool worldStopped = false;
		const std::function<void()> stopWorld = [&realmConnector, &worldInstanceManager, &shutdownSignals, &timerQueue, &timer, &bugReports, &worldStopped]()
		{
			if (worldStopped)
			{
				return;
			}
			worldStopped = true;

			ILOG("Stopping the world server cleanly");

			// ... the existing handler body, unchanged, from "Both queues hold an armed asio timer"
			// through the shutdownSignals->cancel block ...
		};
		shutdownSignals = InstallShutdownHandler(ioService, [&stopWorld]() { stopWorld(); });

		// The realm's order arrives inside the realm connector's packet handler, and stopping closes
		// that very connection, so it runs as its own handler instead.
		const scoped_connection realmShutdown{ realmConnector->shutdownRequested.connect([&ioService, &stopWorld]()
		{
			ioService.post([&stopWorld]() { stopWorld(); });
		}) };
```

Remove the old `ILOG("Shutdown signal received - stopping cleanly");`. `connection.h`'s `close()` defers while a send is in flight, so the character data written in `OnShutdown` still goes out.

- [ ] **Step 4: Build and check shutdown wiring**

```powershell
cmake --build build --config Debug -t world_server realm_server; python tools/shutdown_check.py
```
Expected: build succeeds; `shutdown_check.py` passes (if it reports a new timer/acceptor owner without `Stop()`, fix the wiring it names).

- [ ] **Step 5: Commit**

```powershell
git add src/world_server
git commit -m "feat(shutdown): world nodes save remaining players and exit when the realm shuts down" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Client — console command, countdown display, localization

**Files:**
- Modify: `src/mmo_client/net/realm_connector.h/.cpp` (`GmShutdown`)
- Modify: `src/mmo_client/game_states/world_state.h/.cpp` (`OnShutdownCountdown`, register/unregister `shutdown` command)
- Modify: `src/mmo_client/game_states/world_state_commands.cpp` (`Command_Shutdown`)
- Modify (submodule `data/client`): `Locales/Locale_{enUS,deDE,frFR,ruRU}/Localization.txt`, `Interface/GlueUI/GlueDialog.lua`

**Interfaces:**
- Consumes: Task 1 helpers, Task 2 opcodes.
- Produces: `void RealmConnector::GmShutdown(uint8 action, uint32 delaySeconds);` (client), `void WorldState::Command_Shutdown(const std::string&, const std::string&) const;`

- [ ] **Step 1: Connector.** `realm_connector.h` (client), **outside** any `#ifdef MMO_WITH_DEV_COMMANDS`:

```cpp
		/// OPERATOR only. Schedules (gm_shutdown_action::Start) or cancels a realm shutdown.
		/// @param action A game::gm_shutdown_action value.
		/// @param delaySeconds Seconds until the shutdown; ignored for Cancel.
		void GmShutdown(uint8 action, uint32 delaySeconds);
```

`realm_connector.cpp`, outside any dev ifdef:

```cpp
	void RealmConnector::GmShutdown(const uint8 action, const uint32 delaySeconds)
	{
		sendSinglePacket([action, delaySeconds](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GmShutdown);
			packet << io::write<uint8>(action) << io::write<uint32>(delaySeconds);
			packet.Finish();
		});
	}
```

- [ ] **Step 2: WorldState.** `world_state.h`: declare outside the dev ifdef, near `OnMessageOfTheDay`:

```cpp
		PacketParseResult OnShutdownCountdown(game::IncomingPacket &packet);
```

and near the other `Command_*` declarations but outside `#ifdef MMO_WITH_DEV_COMMANDS`:

```cpp
		/// Console 'shutdown <seconds | m:ss | h:mm:ss>' / 'shutdown cancel' (operators only; the realm checks).
		void Command_Shutdown(const std::string &cmd, const std::string &args) const;
```

`world_state.cpp`: include `"game/shutdown_countdown.h"` and `"game_protocol/game_protocol.h"` (if missing). Register the packet handler next to `MessageOfTheDay` (~line 1923):

```cpp
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ShutdownCountdown, *this, &WorldState::OnShutdownCountdown);
```

Register the console command **after** the `#endif` that closes the GM command registrations (~line 2012), and unregister it after the `#endif` in `RemovePacketHandler` (~line 2045):

```cpp
		Console::RegisterCommand("shutdown", [this](const std::string &cmd, const std::string &args)
								 { Command_Shutdown(cmd, args); }, ConsoleCommandCategory::Gm, "Schedules a realm shutdown for all players: 'shutdown <seconds | m:ss | h:mm:ss>', or 'shutdown cancel'. Operators only.");
```
```cpp
		Console::UnregisterCommand("shutdown");
```

Handler (next to `OnMessageOfTheDay`):

```cpp
	PacketParseResult WorldState::OnShutdownCountdown(game::IncomingPacket &packet)
	{
		uint32 seconds = 0;
		if (!(packet >> io::read<uint32>(seconds)))
		{
			ELOG("Failed to read ShutdownCountdown packet!");
			return PacketParseResult::Disconnect;
		}

		const auto& localization = FrameManager::Get().GetLocalization();
		if (seconds == ShutdownCountdownCancelled)
		{
			FrameManager::Get().TriggerLuaEvent("CHAT_MSG_SYSTEM", Localize(localization, "REALM_SHUTDOWN_CANCELLED"));
			return PacketParseResult::Pass;
		}

		// Indexed by shutdown_time_unit
		static const char* const unitKeys[] = {
			"REALM_SHUTDOWN_IN_HOURS",
			"REALM_SHUTDOWN_IN_MINUTES",
			"REALM_SHUTDOWN_IN_MINUTE",
			"REALM_SHUTDOWN_IN_SECONDS",
		};

		const FormattedShutdownTime formatted = FormatShutdownTime(seconds);
		String message = Localize(localization, unitKeys[formatted.unit]);
		if (const size_t placeholder = message.find("%s"); placeholder != String::npos)
		{
			message.replace(placeholder, 2, formatted.time);
		}
		else
		{
			message += " " + formatted.time;
		}

		FrameManager::Get().TriggerLuaEvent("CHAT_MSG_SYSTEM", message);
		return PacketParseResult::Pass;
	}
```

- [ ] **Step 3: Console command.** `world_state_commands.cpp`: add after the file's final `#endif` (still inside `namespace mmo`), including `"game/shutdown_countdown.h"` at the top:

```cpp
	void WorldState::Command_Shutdown(const std::string &cmd, const std::string &args) const
	{
		std::istringstream stream(args);
		std::string arg;
		stream >> arg;

		if (arg.empty())
		{
			ELOG("Usage: shutdown <seconds | m:ss | h:mm:ss> | shutdown cancel");
			return;
		}

		if (arg == "cancel")
		{
			m_realmConnector.GmShutdown(game::gm_shutdown_action::Cancel, 0);
			ILOG("Requested cancellation of the pending realm shutdown");
			return;
		}

		uint32 delaySeconds = 0;
		if (!ParseShutdownDelay(arg, delaySeconds))
		{
			ELOG("Invalid shutdown delay '" << arg << "': expected seconds, m:ss or h:mm:ss, at most " << MaxShutdownDelaySeconds / 3600 << " hours");
			return;
		}

		m_realmConnector.GmShutdown(game::gm_shutdown_action::Start, delaySeconds);
		ILOG("Requested a realm shutdown in " << FormatShutdownTime(delaySeconds).time);
	}
```

If `<sstream>` is only included inside the dev ifdef, move/add the include at file top.

- [ ] **Step 4: Localization (submodule `data/client`).** Add after the `KICK_REASON_LOGGED_IN_ELSEWHERE` line in each file, matching its `(key = "...", string = "...")` format and indentation:

enUS:
```
	(key = "REALM_SHUTDOWN_IN_HOURS", string = "[System] Realm shutdown in %s hours.")
	(key = "REALM_SHUTDOWN_IN_MINUTES", string = "[System] Realm shutdown in %s minutes.")
	(key = "REALM_SHUTDOWN_IN_MINUTE", string = "[System] Realm shutdown in %s minute.")
	(key = "REALM_SHUTDOWN_IN_SECONDS", string = "[System] Realm shutdown in %s seconds.")
	(key = "REALM_SHUTDOWN_CANCELLED", string = "[System] Realm shutdown cancelled.")
	(key = "KICK_REASON_REALM_SHUTDOWN", string = "You have been disconnected because the realm is shutting down.")
```
deDE:
```
	(key = "REALM_SHUTDOWN_IN_HOURS", string = "[System] Herunterfahren des Realms in %s Stunden.")
	(key = "REALM_SHUTDOWN_IN_MINUTES", string = "[System] Herunterfahren des Realms in %s Minuten.")
	(key = "REALM_SHUTDOWN_IN_MINUTE", string = "[System] Herunterfahren des Realms in %s Minute.")
	(key = "REALM_SHUTDOWN_IN_SECONDS", string = "[System] Herunterfahren des Realms in %s Sekunden.")
	(key = "REALM_SHUTDOWN_CANCELLED", string = "[System] Herunterfahren des Realms abgebrochen.")
	(key = "KICK_REASON_REALM_SHUTDOWN", string = "Die Verbindung wurde getrennt, weil der Realm heruntergefahren wird.")
```
frFR:
```
	(key = "REALM_SHUTDOWN_IN_HOURS", string = "[Système] Arrêt du royaume dans %s heures.")
	(key = "REALM_SHUTDOWN_IN_MINUTES", string = "[Système] Arrêt du royaume dans %s minutes.")
	(key = "REALM_SHUTDOWN_IN_MINUTE", string = "[Système] Arrêt du royaume dans %s minute.")
	(key = "REALM_SHUTDOWN_IN_SECONDS", string = "[Système] Arrêt du royaume dans %s secondes.")
	(key = "REALM_SHUTDOWN_CANCELLED", string = "[Système] Arrêt du royaume annulé.")
	(key = "KICK_REASON_REALM_SHUTDOWN", string = "Vous avez été déconnecté car le royaume s'arrête.")
```
ruRU:
```
	(key = "REALM_SHUTDOWN_IN_HOURS", string = "[Система] Отключение игрового мира через %s ч.")
	(key = "REALM_SHUTDOWN_IN_MINUTES", string = "[Система] Отключение игрового мира через %s мин.")
	(key = "REALM_SHUTDOWN_IN_MINUTE", string = "[Система] Отключение игрового мира через %s мин.")
	(key = "REALM_SHUTDOWN_IN_SECONDS", string = "[Система] Отключение игрового мира через %s сек.")
	(key = "REALM_SHUTDOWN_CANCELLED", string = "[Система] Отключение игрового мира отменено.")
	(key = "KICK_REASON_REALM_SHUTDOWN", string = "Соединение разорвано: игровой мир отключается.")
```

Keep each file's existing encoding (check for a BOM with `Format-Hex -Count 3` before editing; preserve it).

`Interface/GlueUI/GlueDialog.lua`, after `KICK_REASON_STRING[1] = ...`:

```lua
KICK_REASON_STRING[2] = "KICK_REASON_REALM_SHUTDOWN"
```

- [ ] **Step 5: Build the client**

```powershell
cmake --build build --config Debug -t mmo_client
```
Expected: success.

- [ ] **Step 6: Commit (submodule first, then the gitlink)**

```powershell
git -C data/client checkout -b feature/realm-shutdown
git -C data/client add Locales Interface/GlueUI/GlueDialog.lua
git -C data/client commit -m "Realm shutdown countdown strings and kick reason" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git add src/mmo_client data/client
git commit -m "feat(shutdown): client shutdown console command and localized countdown" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

(Landing the `data/client` commit on the submodule's `master` before any push is part of `/ship` / the submodule-pointer-landing procedure; do not push.)

---

### Task 8: E2E — schedule and cancel from a scenario

**Files:**
- Modify: `src/shared/bot_core/bot_realm_connector.h/.cpp` (send `GmShutdown`, record `ShutdownCountdown`)
- Modify: `src/e2e_client/scenario_engine.cpp` (Lua `GM.ScheduleShutdown`, `GM.CancelShutdown`)
- Create: `e2e/scenarios/gm_realm_shutdown_cancel.lua`
- Modify: `e2e/README.md` (document the two GM functions next to `SetTimeOfDay`)

**Interfaces:**
- Consumes: Task 2 opcodes, Task 4 realm handling.
- Produces: `BotRealmConnector::GmShutdown(uint8, uint32)`, `uint32 GetShutdownCountdownCounter() const`, `uint32 GetLastShutdownCountdown() const`; Lua `GM.ScheduleShutdown(seconds) -> seconds announced`, `GM.CancelShutdown() -> true`.

- [ ] **Step 1: Write the scenario (fails: functions don't exist)**

`e2e/scenarios/gm_realm_shutdown_cancel.lua`:

```lua
-- Regression: scheduled realm shutdown, schedule and cancel. The chain under test is:
--
--   client GmShutdown (gm_level 3, handled by the realm, not proxied)
--     -> realm ShutdownManager schedules and announces the exact remaining time
--     -> ShutdownCountdown to every player in the world
--   and the same for a cancel, which announces 0xFFFFFFFF.
--
-- The shutdown is never allowed to run out: it would stop the shared stack under every later
-- scenario. Both delays are far longer than this scenario, and both are cancelled.

local announced = GM.ScheduleShutdown(600)
Assert(announced == 600, "the realm should announce the full delay right away, got " .. tostring(announced))
Assert(GM.CancelShutdown(), "the realm should announce the cancellation")

-- Scheduling again works after a cancel, and a reschedule replaces the pending shutdown.
announced = GM.ScheduleShutdown(1800)
Assert(announced == 1800, "a second schedule should be announced, got " .. tostring(announced))
announced = GM.ScheduleShutdown(900)
Assert(announced == 900, "a reschedule should announce its own delay, got " .. tostring(announced))
Assert(GM.CancelShutdown(), "the rescheduled shutdown should be cancellable")

Log("Realm shutdown verified: schedule, cancel, reschedule, cancel")
```

- [ ] **Step 2: BotRealmConnector.** `bot_realm_connector.h` public (near `CheatSetTimeOfDay`/`GetGameTimeInfoCounter`):

```cpp
		/// Schedules (gm_shutdown_action::Start) or cancels a realm shutdown. Needs gm_level 3.
		void GmShutdown(uint8 action, uint32 delaySeconds);
		/// Incremented for every ShutdownCountdown received.
		uint32 GetShutdownCountdownCounter() const { return m_shutdownCountdownCounter; }
		/// Seconds of the last ShutdownCountdown (ShutdownCountdownCancelled after a cancel).
		uint32 GetLastShutdownCountdown() const { return m_lastShutdownCountdown; }
```

private: `uint32 m_shutdownCountdownCounter { 0 }; uint32 m_lastShutdownCountdown { 0 };` and `PacketParseResult OnShutdownCountdown(game::IncomingPacket& packet);`

`bot_realm_connector.cpp`: register next to `GameTimeInfo` (~line 471):

```cpp
		RegisterPacketHandler(game::realm_client_packet::ShutdownCountdown, *this, &BotRealmConnector::OnShutdownCountdown);
```

(and clear it wherever `GameTimeInfo`'s handler is cleared, if it is). Implementations:

```cpp
	void BotRealmConnector::GmShutdown(const uint8 action, const uint32 delaySeconds)
	{
		sendSinglePacket([action, delaySeconds](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GmShutdown);
			packet << io::write<uint8>(action) << io::write<uint32>(delaySeconds);
			packet.Finish();
		});
	}

	PacketParseResult BotRealmConnector::OnShutdownCountdown(game::IncomingPacket& packet)
	{
		uint32 seconds = 0;
		if (!(packet >> io::read<uint32>(seconds)))
		{
			return PacketParseResult::Disconnect;
		}

		m_lastShutdownCountdown = seconds;
		++m_shutdownCountdownCounter;
		return PacketParseResult::Pass;
	}
```

- [ ] **Step 3: Lua bindings.** In `scenario_engine.cpp`, include `"game/shutdown_countdown.h"`, add next to `luaGmResetTimeOfDay`:

```cpp
		/// Waits for the first ShutdownCountdown after previousCounter and returns its seconds.
		uint32 waitForShutdownCountdown(const char* action, const uint32 previousCounter)
		{
			BotRealmConnector& realm = g_runtime->session->GetRealm();

			const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (std::chrono::steady_clock::now() < until)
			{
				if (realm.GetShutdownCountdownCounter() != previousCounter)
				{
					const uint32 seconds = realm.GetLastShutdownCountdown();
					if (g_runtime->transcript)
					{
						g_runtime->transcript->Action(action, { { "seconds", seconds } });
					}
					return seconds;
				}

				pumpChecked();
			}

			abortScenario(bot_exit_code::ScenarioFailed, std::string(action) + ": no shutdown countdown received within 10s");
		}

		uint32 luaGmScheduleShutdown(const uint32 delaySeconds)
		{
			BotRealmConnector& realm = g_runtime->session->GetRealm();
			const uint32 previousCounter = realm.GetShutdownCountdownCounter();
			realm.GmShutdown(game::gm_shutdown_action::Start, delaySeconds);
			return waitForShutdownCountdown("GM.ScheduleShutdown", previousCounter);
		}

		bool luaGmCancelShutdown()
		{
			BotRealmConnector& realm = g_runtime->session->GetRealm();
			const uint32 previousCounter = realm.GetShutdownCountdownCounter();
			realm.GmShutdown(game::gm_shutdown_action::Cancel, 0);
			return waitForShutdownCountdown("GM.CancelShutdown", previousCounter) == ShutdownCountdownCancelled;
		}
```

Register next to `GM_ResetTimeOfDay` (~line 1573):

```cpp
				luabind::def_lambda("GM_ScheduleShutdown", &luaGmScheduleShutdown),
				luabind::def_lambda("GM_CancelShutdown", &luaGmCancelShutdown),
```

and in the Lua `GM` table next to `ResetTimeOfDay` (~line 1641):

```lua
				ScheduleShutdown = function(seconds) return GM_ScheduleShutdown(seconds) end,
				CancelShutdown = function() return GM_CancelShutdown() end,
```

(Mind the luabind trailing-comma gotcha noted in the e2e-test-harness memory: match the comma style of the surrounding entries exactly.) Note: `delaySeconds = 0` in Lua would shut the stack down; the scenario never passes it.

- [ ] **Step 4: Document** both functions in `e2e/README.md` next to `GM.SetTimeOfDay`, with the warning that a shutdown must always be cancelled within the scenario.

- [ ] **Step 5: Run the E2E suite**

```powershell
cmake --build build --config Debug -t e2e_client login_server realm_server world_server
$env:MMO_E2E_MYSQL_PASSWORD = "<from the e2e-test-harness memory>"
powershell -File tools/e2e/e2e_run.ps1
```
Expected: exit 0; `e2e/runtime/logs/summary.json` lists `gm_realm_shutdown_cancel` as passed and its transcript shows `GM.ScheduleShutdown seconds=600`, `GM.CancelShutdown seconds=4294967295`.

- [ ] **Step 6: Commit**

```powershell
git add src/shared/bot_core src/e2e_client/scenario_engine.cpp e2e/scenarios/gm_realm_shutdown_cancel.lua e2e/README.md
git commit -m "test(shutdown): E2E schedule/cancel scenario and GM bindings" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Full run-to-exit verification and Docker note

**Files:**
- Modify: `compose.yml` (comments on `realm_server_01` and `world_node_01`)

- [ ] **Step 1: compose.yml.** Above `restart: on-failure` of `realm_server_01` and of `world_node_01` add:

```yaml
    # A scheduled realm shutdown (GM 'shutdown' command or POST /shutdown on the realm) makes the
    # realm and its world nodes exit with code 0, which on-failure does not restart. Crashes exit
    # non-zero and are still restarted. Do not switch to 'always' or 'unless-stopped' unless a
    # scheduled shutdown should restart the servers.
```

- [ ] **Step 2: Run a real shutdown to completion on the E2E stack**

```powershell
powershell -File tools/e2e/e2e_up.ps1
$pids = Get-Content e2e/runtime/pids.json | ConvertFrom-Json   # check e2e_up.ps1's Save-Pids for the actual file name
$auth = @{ Authorization = "Basic " + [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("mmo-e2e:e2e-secret")) }
Invoke-RestMethod -Uri http://127.0.0.1:18092/shutdown -Method Post -Headers $auth -Body @{ delay = 20 }
$realm = Get-Process -Id $pids.realm; $world = Get-Process -Id $pids.world
$realm.WaitForExit(60000); $world.WaitForExit(60000)
"realm exit: $($realm.ExitCode)  world exit: $($world.ExitCode)"
Select-String -Path e2e/runtime/logs/realm_server.out.log, e2e/runtime/logs/world_server.out.log -Pattern "shutdown|Stopping|stopped cleanly"
powershell -File tools/e2e/e2e_down.ps1
```
Expected: both processes exit within ~40 s with exit code `0`; the realm log shows "Scheduled realm shutdown is due", "Stopping the realm server cleanly", "Realm server stopped cleanly"; the world log shows "The realm ordered this world node to shut down" and "World server stopped cleanly"; the world log does **not** show "Reconnect in 5 seconds". If `Get-Process` loses the exit code (process started by another shell), read the exit code from the e2e harness instead or start the servers manually with `Start-Process -PassThru`.

- [ ] **Step 3: Character save check.** Repeat Step 2 with an E2E bot logged in (e.g. run one scenario with `-KeepStack` if `e2e_run.ps1` supports it, move the character with `GM.Port`, then shut down). After restart, the character's position in `mmo_realm_01`-equivalent E2E database (`characters` table) matches the position before shutdown. If no convenient hook exists, verify by log: the realm log contains the character data save for that character after "Scheduled realm shutdown is due" and before "Stopping the realm server cleanly".

- [ ] **Step 4: Manual client check (optional, needs the user's dev stack and GM 3 account).** In the game client console: `shutdown 1:10` → chat shows "[System] Realm shutdown in 1:10 minutes.", then "1:00 minute", "0:45 seconds", "0:30 seconds", "0:15 seconds", then the disconnect dialog with the realm shutdown reason. `shutdown cancel` in between shows "[System] Realm shutdown cancelled."

- [ ] **Step 5: Commit**

```powershell
git add compose.yml
git commit -m "docs(shutdown): explain restart behaviour of scheduled shutdowns in compose.yml" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Admin UI shutdown card (separate repo `H:\mmo-admin`)

**Files (in `H:\mmo-admin`):**
- Create: `frontend/src/components/RealmShutdownCard.tsx`
- Modify: `frontend/src/pages/RealmDetail.tsx` (render the card next to the MOTD card)

**Interfaces:**
- Consumes: realm REST from Task 5 through `realmApiGet`/`realmApiPost` (`frontend/src/api/realms.ts`).

- [ ] **Step 1: Branch**

```powershell
git -C H:/mmo-admin status --short; git -C H:/mmo-admin checkout -b feature/realm-shutdown
```
Expected: clean tree before branching (if not, stop and ask the user).

- [ ] **Step 2: Component**

`frontend/src/components/RealmShutdownCard.tsx`:

```tsx
import { useEffect, useState } from 'react';
import { realmApiGet, realmApiPost } from '../api/realms';

interface ShutdownStatus { pending: boolean; remaining: number; }

function formatRemaining(seconds: number): string {
	const h = Math.floor(seconds / 3600);
	const m = Math.floor((seconds % 3600) / 60);
	const s = seconds % 60;
	const mm = String(m).padStart(h > 0 ? 2 : 1, '0');
	const ss = String(s).padStart(2, '0');
	return h > 0 ? `${h}:${mm}:${ss}` : `${mm}:${ss}`;
}

export default function RealmShutdownCard({ realmId, canManage }: { realmId: number; canManage: boolean }) {
	const [status, setStatus] = useState<ShutdownStatus | null>(null);
	const [minutes, setMinutes] = useState('15');
	const [busy, setBusy] = useState(false);
	const [error, setError] = useState('');

	async function refresh() {
		try {
			setStatus(await realmApiGet<ShutdownStatus>(realmId, 'shutdown'));
		} catch {
			setStatus(null);
		}
	}

	useEffect(() => {
		void refresh();
		const timer = window.setInterval(() => void refresh(), 5000);
		return () => window.clearInterval(timer);
	}, [realmId]);

	async function run(action: () => Promise<unknown>) {
		setBusy(true);
		setError('');
		try {
			await action();
			await refresh();
		} catch (err: any) {
			setError(err.response?.data?.message ?? err.response?.data?.status ?? 'Request failed');
		} finally {
			setBusy(false);
		}
	}

	function schedule(e: React.FormEvent) {
		e.preventDefault();
		const delay = Math.round(parseFloat(minutes) * 60);
		if (!Number.isFinite(delay) || delay < 0) { setError('Enter a delay in minutes'); return; }
		if (!window.confirm(`Shut down the realm and all its world nodes in ${formatRemaining(delay)}?`)) return;
		void run(() => realmApiPost(realmId, 'shutdown', { delay }));
	}

	return (
		<div className="card card-outline card-danger">
			<div className="card-header"><h3 className="card-title"><i className="fas fa-power-off mr-2" />Shutdown</h3></div>
			<div className="card-body">
				{error && <div className="alert alert-danger py-1 small">{error}</div>}
				<p className="mb-2">
					{status === null ? <span className="text-muted small">Status unavailable</span>
						: status.pending ? <span className="text-danger">Shutdown in <strong>{formatRemaining(status.remaining)}</strong></span>
						: <span className="text-muted">No shutdown scheduled</span>}
				</p>
				{canManage && (
					<form onSubmit={schedule} className="form-inline">
						<div className="input-group input-group-sm mr-2">
							<input type="number" min="0" step="1" className="form-control" value={minutes}
								onChange={(e) => setMinutes(e.target.value)} disabled={busy} />
							<div className="input-group-append"><span className="input-group-text">min</span></div>
						</div>
						<button type="submit" className="btn btn-sm btn-danger mr-2" disabled={busy}>
							{status?.pending ? 'Reschedule' : 'Schedule'}
						</button>
						{status?.pending && (
							<button type="button" className="btn btn-sm btn-outline-secondary" disabled={busy}
								onClick={() => void run(() => realmApiPost(realmId, 'shutdown/cancel', {}))}>
								Cancel shutdown
							</button>
						)}
					</form>
				)}
			</div>
		</div>
	);
}
```

- [ ] **Step 3: Render it.** In `RealmDetail.tsx` import `RealmShutdownCard` and render `<RealmShutdownCard realmId={realmId} canManage={!!user?.is_super_admin} />` in the same column/row as the MOTD card, only when `config` is set (the same condition under which `loadLiveData` runs). Check that the backend proxy route `router.all('/:id/proxy*', ...)` in `backend/src/routes/realms.ts` forwards the nested `shutdown/cancel` path (it uses a wildcard; confirm by reading how it builds the target URL).

- [ ] **Step 4: Type-check and build**

```powershell
npm --prefix H:/mmo-admin/frontend run build
```
Expected: build succeeds without type errors.

- [ ] **Step 5: Commit (in mmo-admin)**

```powershell
git -C H:/mmo-admin add frontend/src/components/RealmShutdownCard.tsx frontend/src/pages/RealmDetail.tsx
git -C H:/mmo-admin commit -m "feat: realm shutdown card (schedule, cancel, countdown)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Gate

- [ ] **Step 1:** Run `/gate` (fast tier: protocol check, build, unit tests, tool tests) on `feature/realm-shutdown`. Expected: green. The nightly of 2026-10-04 failed in `tool_tests` on develop; if the same tool test fails here, compare against develop before attributing it to this branch.
- [ ] **Step 2:** Because this touches server shutdown paths and the wire protocol, also run `/gate full` (adds E2E and review) before `/ship`.
