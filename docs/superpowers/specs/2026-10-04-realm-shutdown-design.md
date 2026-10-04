# Scheduled Realm Shutdown — Design

Date: 2026-10-04
Branch: `feature/realm-shutdown`

## Goal

An operator can schedule a shutdown of a realm and every world node connected to it, from the
in-game GM console or the realm REST API (and therefore the admin UI). Players see a countdown in
chat that gets denser towards the end. At zero, characters are saved, world nodes and the realm exit
cleanly with code 0, and Docker does not restart them.

## Decisions

| Topic | Decision |
|---|---|
| Permission | New GM level 3 = operator. Named constants: 1 basic GM, 2 senior GM, 3 operator. |
| World nodes | Every world node connected to the realm shuts down (no "still serving another realm" check). |
| Cancel | Supported via console and REST; players are told. Starting again while pending re-schedules. |
| Message text | Dedicated packet carrying remaining seconds; the client formats a localized line. |
| Docker | No compose change needed; `restart: on-failure` does not restart on exit code 0. A comment is added. |

## 1. Countdown and announcement schedule (realm server)

New `ShutdownManager` (`src/realm_server/shutdown_manager.h/.cpp`).

- State: pending flag and absolute deadline (`GameTime`, from `GetAsyncTimeMs()`; never used in
  float math). Driven by the existing `TimerQueue`, which is already stopped by the realm's shutdown
  handler, so no extra `Stop()` wiring is needed. A generation counter invalidates timer events
  queued before a cancel or re-schedule (TimerQueue events cannot be cancelled).
- `Schedule(uint32 delaySeconds)`: sets/replaces the deadline, announces the exact remaining time
  immediately, then arms the next announcement. `delaySeconds == 0` skips the countdown and starts
  the shutdown sequence (section 3) at once.
- `Cancel()`: returns false if nothing is pending; otherwise clears state and broadcasts the
  cancellation.
- `IsPending()`, `GetRemainingSeconds()` for REST status.
- Signal `shutdownDue` fired at zero; `program.cpp` connects it to the shutdown sequence.

Announcement points are a pure free function, unit-tested:

```cpp
/// Returns the next announcement mark (in remaining seconds) strictly below `remaining`,
/// or 0 when the next event is the shutdown itself.
uint32 NextShutdownAnnouncement(uint32 remaining);
```

Marks: every full hour above 1 h (…, 7200, 3600), then 1800, 900, 600, 300, 240, 180, 120, 60, 45,
30, 15. The immediate announcement at `Schedule` time uses the exact remaining value, so a 20 s
shutdown shows 0:20 and then 0:15.

Broadcast goes to every player in `PlayerManager` that has a character in world (same filter as
`BroadcastMessageOfTheDay`). Players who enter the world while a shutdown is pending receive the
current remaining time once on world entry.

## 2. Wire changes

**Game protocol** — bump `mmo::game::ProtocolVersion`:

- `realm_client_packet::ShutdownCountdown` — payload `uint32 seconds`; `0xFFFFFFFF` = cancelled.
- `client_realm_packet::GmShutdown` — payload `uint8 action` (0 = start, 1 = cancel),
  `uint32 delaySeconds`. Registered on the realm outside `MMO_WITH_DEV_COMMANDS` (operators need it
  on live builds). Rejected with a warning log unless `HasGMLevel(gm_level::Operator)`.
- `session_kick_reason::RealmShutdown` — new enum value, used for the final kick so the client can
  say why it was disconnected.

**Auth protocol** — bump `mmo::auth::ProtocolVersion`:

- `realm_world_packet::Shutdown` — no payload. Tells the world node to shut down for good.

Both bumps: run `python tools/protocol_version_check.py --update` and record them in
`docs/protocol_versions.md`.

GM levels: a `gm_level` pseudo-namespace enum (`Gm = 1`, `SeniorGm = 2`, `Operator = 3`) in shared
code; existing `HasGMLevel(1)` / `HasGMLevel(2)` call sites are switched to the names (no behaviour
change).

## 3. Shutdown sequence at zero (or `delay = 0`)

1. **Close the door, kick players.** The realm stops its player acceptor and kicks every connected
   player with `session_kick_reason::RealmShutdown`. Each kick runs the normal `Player::Destroy`
   path, which sends `PlayerCharacterLeave` to the world node; the node answers with the character
   data, which the realm persists through the still-running database pool.
2. **Shut down world nodes.** Right after the kicks, the realm sends `realm_world_packet::Shutdown`
   to each connected node. It travels on the same link *after* the `PlayerCharacterLeave` packets,
   and the world node removes a player synchronously (despawn → `SaveCharacterData`), so every
   character's data is on its way back before the node acts on `Shutdown`. The node then removes
   any players still present the same way, runs the same graceful handler its SIGTERM path uses
   (which includes `RealmConnector::Shutdown()`, so it does not reconnect) and exits with code 0.
   `close()` defers while a send is in flight, so the last character data still goes out.
3. **Stop the realm.** Once all world nodes have disconnected, or after 15 s at most, the realm runs
   its graceful stop (acceptors, web service, timer queue, remaining connections, signal set, work
   guard, database pool drain) and `main` returns 0.

The wait in step 3 polls the world count on the realm `TimerQueue`; a world node that never answers
cannot keep the realm alive past the grace period.

**Refactor:** the inline lambdas passed to `InstallShutdownHandler` in
`src/realm_server/program.cpp` and `src/world_server/program.cpp` become named callables invoked from
both the signal handler and the new path, and are idempotent (a SIGTERM arriving mid-sequence must
not double-stop). The world server's `RealmConnector` exposes a signal (e.g. `shutdownRequested`)
that `program.cpp` connects to that callable.

Exit codes: both servers already return 0 after a graceful `ioService.run()` exit; this must stay
true on the new path (verified in testing).

## 4. Triggers

### GM console (client)

`shutdown <time>` and `shutdown cancel`, registered in `ConsoleCommandCategory::Gm` outside
`MMO_WITH_DEV_COMMANDS`. `<time>` accepts `seconds`, `m:ss` or `h:mm:ss`. The client sends
`GmShutdown`; permission is enforced only on the realm. Parsing lives in a small free function so it
can be unit-tested.

### Realm REST API (`src/realm_server/web_client.cpp`)

| Method | Path | Parameters | Response |
|---|---|---|---|
| POST | `/shutdown` | `delay` (seconds, optional, default 0) | `{"status":"SUCCESS","delay":N}`; 400 `INVALID_PARAMETER` on a non-numeric delay |
| POST | `/shutdown/cancel` | — | `{"status":"SUCCESS"}`, or 409 `{"status":"NOT_PENDING"}` |
| GET | `/shutdown` | — | `{"pending":bool,"remaining":N}` |

The existing `POST /shutdown` currently calls `ioService.stop()` (abrupt, discards pending closes
and writes). It now goes through `ShutdownManager`; `delay=0` means immediate *graceful* shutdown.
`WebService` gets access to the `ShutdownManager` the same way it gets `MOTDManager`.

Update `docs/realm_server_api.md` and `bruno/mmo-dev/realm-server/` (`Shutdown.bru` gains `delay`;
new `Shutdown Cancel.bru`, `Shutdown Status.bru`).

### Admin UI (`H:\mmo-admin`, separate repository)

A "Shutdown" card on `RealmDetail.tsx`: delay input (minutes), "Schedule" and "Cancel" buttons, and
the current status from `GET /shutdown`, all through the existing `realmApiGet`/`realmApiPost`
proxy. Committed on its own branch in that repository; never pushed without the user asking.

## 5. Client

- `WorldState` handles `ShutdownCountdown`: it formats the time and fires `CHAT_MSG_SYSTEM` with the
  localized line, so it appears like other system messages.
- Formatting (pure function, unit-tested): `h:mm:ss` with the hours key at ≥ 1 h, `m:ss` with the
  minutes key at ≥ 2 min, `1:00` with the singular minute key, `0:ss` with the seconds key below
  1 min.
- New keys in every `data/client/Locales/*/Localization.txt` (placeholders where no translation is
  available): `REALM_SHUTDOWN_IN_HOURS`, `REALM_SHUTDOWN_IN_MINUTES`, `REALM_SHUTDOWN_IN_MINUTE`,
  `REALM_SHUTDOWN_IN_SECONDS` (each "[System] Realm shutdown in %s …"), `REALM_SHUTDOWN_CANCELLED`,
  and a kick message for `RealmShutdown`.
- `data/client` is a submodule: the locale edits are committed there and the pointer updated (see
  the submodule-pointer-landing notes).

## 6. Docker

`compose.yml` keeps `restart: on-failure` for `realm_server_01` and `world_node_01`, with a comment
explaining that a scheduled shutdown exits with code 0 and is therefore not restarted, while
crashes (non-zero) still are. `unless-stopped`/`always` would restart after a scheduled shutdown and
must not be used if that is undesired.

## 7. Testing

- **Unit tests** (realm-side logic in a test-reachable library, or a new `src/tests/realm_tests`
  suite if needed): `NextShutdownAnnouncement` over the whole range, client time formatting, console
  time parsing, `ShutdownManager` schedule / re-schedule / cancel with stale-timer invalidation.
- **`python tools/shutdown_check.py`** stays green.
- **E2E:** a scenario where an operator schedules a short shutdown and cancels it, checking the
  announcement and the cancel message. If the harness can tolerate a stack that goes down on purpose,
  a second scenario lets a 20 s shutdown run to completion and checks the kick reason; otherwise the
  full run to exit (both processes exit 0, characters saved) is verified manually against the dev
  stack. The E2E account gets GM level 3 where the scenario needs it.

## Out of scope

- Shutting down the login server.
- Scheduling at a wall-clock time (only relative delays).
- Restart-after-shutdown semantics (the orchestrator decides).
