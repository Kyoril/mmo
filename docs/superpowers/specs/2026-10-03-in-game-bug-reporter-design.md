# In-Game Bug Reporter — Design

**Date:** 2026-10-03
**Status:** Approved (brainstorming), pending spec review

## Problem

Players have no way to report bugs from inside the game. Reports that arrive through
other channels lack the context needed to act on them: which item, spell, creature or
quest was involved, where the player stood, what the server believed the state was.
Long term, reports should be collected centrally so an AI agent job can fetch,
analyse, triage and hand them to a fix loop — the same way `/fix-crash` works for
crash reports today.

## Goals

- A client UI module that hooks into existing UI (tooltips, quest log, micro menu) so a
  player can report a bug **about a concrete piece of content** with one key press
  (F6 on an open tooltip), or a generic bug via a micro-menu button.
- The whole client feature is enabled/disabled by **one line** in `GameUI.toc`.
- Reports travel client → realm → world over the game protocol; the **world server**
  enriches them with authoritative server-side state the client cannot see, and
  uploads them to a central API.
- A central bug API on the existing `error.mmo-dev.net` backend, with authentication
  and a claim/status workflow suitable for automated (AI) processing.
- A bug list + detail view in the existing error-report web UI.
- A **generic subsystem-status system**: the server announces which subsystems are
  available at login and pushes changes at runtime; the client exposes this through a
  common Lua API and UI event.

## Non-goals (follow-ups)

- Screenshot attachments (needs a chunked transfer path; the API model reserves room
  for attachments so no schema change is needed later).
- The AI triage/fix agent job itself (this design delivers the API and CLI it needs).
- Reporting from the GlueUI (login / character selection).
- Player-visible report history ("my tickets") or GM replies.
- Azure AD authentication for the web UI.
- Locking down the existing crash-report routes.

## Decisions taken during brainstorming

| Topic | Decision |
|---|---|
| Transport | Client → realm (game protocol) → world (proxy) → HTTPS to central API. Client never talks to the API. |
| Central API | Extend `H:\mmo-error-report` (Node 18 / Express 4 / Mongoose / MongoDB 6). |
| API auth | API keys in header `X-Api-Key`: an *ingest* key (world servers only) and a *reader* key (CLI, AI jobs, web UI proxy). Crash routes untouched. |
| Delivery | In-memory queue + one uploader thread with retry while the world server runs. Reports are lost on restart or prolonged API outage (accepted). |
| Subjects | Item, spell, creature, quest, aura, world object, generic. |
| Client attachments | Client metadata + last ~200 log lines. No screenshot. |
| Compression | Client-side JSON payload is zlib-compressed. |
| Comment length | 1000 characters. |
| Global button | On the micro menu. |
| Combat history | World server keeps a per-player ring buffer of recent combat events, always on. |
| Subsystem toggles | GM command + realm REST route **and** automatic toggle by the bug uploader on sustained API failure. |
| Web UI | Gets bug list + detail view; secured by an nginx proxy that injects the reader key + HTTP basic auth. |
| Repos | `git init` for `H:\mmo-error-report`; baseline commit of the current dirty state in `H:\mmo-error-report-ui` before any change. |

## Architecture

```
Client (BugReporter UI module, Interface/GameUI/BugReporter/)
  │  CMSG BugReport { subjectType, subjectId, subjectGuid, comment,
  │                   zlib(clientJson) }
  ▼
Realm server
  │  - checks: in world, size cap, per-character rate limit
  │  - attaches accountId / characterId / realm name from its own session
  │  - forwards to the character's world node (realm↔world link)
  ▼
World server
  │  - decompress with caps, sanitize client JSON
  │  - BugSnapshotBuilder: player snapshot + subject snapshot (io thread)
  │  - enqueue in BugReportUploader → SMSG BugReportResult (via realm)
  ▼  HTTPS POST /api/bugs  (X-Api-Key: ingest)
error.mmo-dev.net  (Express + MongoDB, model Bug)
  ▼  GET / claim / PATCH  (X-Api-Key: reader)
tools/bugs/bugs.py, web UI (via nginx proxy), later: AI agent job
```

Subsystem status flows separately: world node → realm (realm↔world link) → client
(`SMSG SubsystemStatus`), see "Subsystem status".

## Client UI module

### Packaging

All files live in `data/client/Interface/GameUI/BugReporter/`:
`BugReporter.toc`, `BugReporter.xml`, `BugReporter.lua`. `GameUI.toc` gets one line,
`BugReporter/BugReporter.toc`, placed after `GameTooltip.xml` and the frames it hooks
(quest log, micro menu) so their globals exist when the module loads. Commenting the
line out removes the feature completely, including the F6 binding. Note that
`data/client` is the `mmo-data` submodule — UI changes are committed there.

### Runtime key bindings (C++ + Lua API)

Bindings are currently only defined in the fixed `Interface/Bindings.xml`. New Lua API:

```lua
RegisterBinding(name, descriptionKey, category, handler, defaultKey)
```

- Adds a binding action at runtime; it appears in the key-binding options like any
  other action. Re-registering the same name replaces the handler (UI reload safe).
- `defaultKey` is applied only if the user's `Bindings.cfg` has no key for this action
  and the key is not bound to anything else. The default is **not** written to
  `Bindings.cfg`; only user changes are persisted (existing behaviour of `SetBinding`).

BugReporter registers `BUGREPORT` with default `F6` (F1–F5 are taken, F6 is free).

### Subject tracking via tooltip hooks

The module wraps the global tooltip functions in an unsecured Lua state (there is no
`hooksecurefunc`): `GameTooltip_SetItem`, `GameTooltip_SetItemTemplate`,
`GameTooltip_SetSpell`, `GameTooltip_SetAura`, and the unit/world-object branch of
`GameParent_OnHoveredObjectChanged`. Each wrapper calls the original first, then:

- sets `BugReporter.subject = { type, id, guid, name, icon }`;
- appends a grey line "Press <key> to report an issue for this <type>" (localized, key
  name from `GetKeysForBinding("BUGREPORT")`), only while the subsystem is available.

`GameTooltip_Clear` and tooltip hide clear the subject. Creatures need their template
entry: new C++ binding `UnitHandle:GetEntry()` (0 for players). Quests have no
tooltip: F6 with the quest log or quest dialog open uses the selected/displayed quest;
the quest log also gets a small "Report" button.

Subject types and their IDs:

| type | id | guid |
|---|---|---|
| `item` | item entry | item guid if the player owns the instance, else 0 |
| `spell` | spell id | 0 |
| `creature` | creature entry | unit guid |
| `quest` | quest id | 0 |
| `aura` | spell id | caster guid (target unit guid in client JSON) |
| `object` | object entry | object guid |
| `generic` | 0 | 0 |

### Dialog

One shared frame (`BugReportFrame`, built from `SidePanelTemplate`, a multi-line
`GameTextField`, a character counter and a Submit button) with two modes:

- **Subject mode** (F6 with a subject): header "Issue Report: <name>", subject icon,
  prompt "What was the issue with this <type>?".
- **Generic mode** (F6 without a subject, or the micro-menu button): header
  "Bug Report", prompt "Please describe the issue:".

Comment limit 1000 characters, Submit disabled while empty. After Submit the button
stays disabled until `BUG_REPORT_RESULT` arrives or 10 s pass ("No response from the
server"). Results map to localized messages; `accepted` shows a thank-you and closes the
dialog. The dialog closes on disconnect / leaving the world.

### Micro-menu button

A bug-icon button on the micro menu with the help tooltip from the mock-up ("How can I
help? … Automatically collected: your world location, your character information")
opens the generic dialog. It is hidden while the `BUG_REPORT` subsystem is unavailable.

### Client-side C++ (Lua API)

- `SubmitBugReport(type, id, guid, comment)` — builds the client JSON (see below),
  compresses it with zlib (`zlibstatic` is already linked in the project), and sends
  `CMSG BugReport`. Returns false without sending if not in world or the subsystem is
  unavailable.
- `UnitHandle:GetEntry()`.
- `RegisterBinding(...)` as above.
- Event `BUG_REPORT_RESULT(result)` with `result` one of `ACCEPTED`, `RATE_LIMITED`,
  `TOO_LARGE`, `DISABLED`, `INVALID`.

Client JSON (`schemaVersion: 1`): client version/build, OS, CPU, GPU + driver, graphics
settings (resolution, quality cvars), current FPS, latency, locale, and the last 200
lines of the client log taken from an in-memory ring buffer (not by reading
`Client.log`, which is write-buffered while the client runs).

### Localization

Every string is a key in `Localization.txt` for all locales (placeholders acceptable
for non-English/German).

## Subsystem status (generic)

### Wire model

In `game_protocol`:

```cpp
namespace subsystem
{
	enum Type : uint8
	{
		BugReport = 0,
		Count
	};
}

namespace subsystem_status
{
	enum Type : uint8
	{
		Unavailable = 0,
		Available = 1
	};
}
```

A status byte rather than a bool leaves room for states such as "maintenance".

### Ownership

Every subsystem has exactly one owning tier.

- **Realm-owned** subsystems (future: mail, guild, …) are set by the realm itself.
- **World-owned** subsystems (`BugReport`) are reported by each world node to the realm
  over the realm↔world link: the full list after the node connects, and a delta on
  every change. The realm stores status per world node. A disconnected node's
  subsystems count as unavailable.

### Realm → client

`SMSG SubsystemStatus { uint8 count, [uint8 type, uint8 status] × count }`. The realm
sends the full list on world enter, and only the changed entries when:

- a realm-owned subsystem changes;
- the player's current world node reports a change;
- the player moves to a different world node (resend that node's world-owned entries)
  or the node disconnects.

The client applies entries as overwrites; full list vs. delta needs no distinction.

### Client Lua API

- `IsSubsystemAvailable(name)` → bool. Unknown names and "no status received yet"
  return `false` (fail closed).
- `GetSubsystemStatus(name)` → `"AVAILABLE"` / `"UNAVAILABLE"`.
- Event `SUBSYSTEM_STATUS_CHANGED(name, available)`, fired once per changed entry,
  including for the initial list after world enter.

The name ↔ enum mapping (`"BUG_REPORT"` ↔ `subsystem::BugReport`) is one table in C++.
Adding a subsystem is one enum value plus one table entry.

### Toggling at runtime

- GM command on the world server (dev commands): `.subsystem <name> on|off`.
- Realm REST route `POST /subsystem { name, enabled }`: sets realm-owned subsystems
  directly; for world-owned subsystems it relays a toggle to all connected world nodes.
  Add the request to the Bruno collection `bruno/mmo-dev/`.
- Automatic: the bug uploader marks `BugReport` unavailable when the queue is full or
  uploads have failed for a sustained period, and available again after the next
  successful upload (see "Uploader").

Manual and automatic state combine: the subsystem is available only if it is enabled
by config/GM/REST **and** the uploader is healthy.

### BugReporter reaction

While unavailable: micro-menu button hidden, F6 hint not added to tooltips, F6 does
nothing, an already open dialog shows "Bug reporting is currently unavailable" and
disables Submit.

## Realm server

- Handles `CMSG BugReport` itself (not a plain proxy packet), because it attaches
  identity: account id, character id, character name, realm name.
- Rejects with `DISABLED` if the character is not in a world or the node's
  `BugReport` subsystem is unavailable; with `TOO_LARGE` above 64 KB packet size; with
  `RATE_LIMITED` above 1 report per 30 s or 20 per hour per character (in-memory,
  per realm process).
- Forwards a new realm↔world message `BugReport { accountId, characterId,
  characterName, realmName, subject…, comment, compressedClientJson }` to the world
  node, and relays the world's `BugReportResult` back to the client.
- Maintains the per-node subsystem status table and sends `SMSG SubsystemStatus` as
  described above.

## World server

New code in `src/world_server/bug_report/`.

### BugReportHandler

Receives the realm message. Decompresses with caps (≤ 64 KB compressed, ≤ 256 KB
decompressed, abort on overflow), parses the client JSON with `deps/json` (nlohmann),
keeps only known fields, truncates strings. Any failure → `INVALID` + warning log.
Builds the final report and enqueues it; replies `ACCEPTED` once enqueued.

### Combat event ring buffer

Each `GamePlayerS` keeps the last 50 combat events it was involved in (damage/heal
dealt and taken, casts started/succeeded/failed with result, misses, aura
applied/removed), with timestamp, other party guid and spell id. Always on; a few KB
per player.

### BugSnapshotBuilder

Runs on the world io thread (single-threaded, may read game state directly).

Always:

- character: account/character id (from realm), name, race, class, level, class ranks;
- position: map id, instance id, zone/area, x/y/z/facing, dungeon flag;
- health/power, combat state, active auras (spell id, caster guid, remaining ms,
  stacks);
- target (guid, entry, type), group size;
- last 50 combat events;
- server build/commit, realm name, world node id, server time.

Per subject:

- **item**: item proto entry; if owned: guid, slot, enchantments, stack count,
  durability.
- **spell**: spell proto entry; known by player; cooldown and remaining cooldown;
  last cast result for this spell from the combat buffer.
- **creature**: entry; if the guid is in the player's visibility: spawn id, position,
  AI state, health, threat list, auras, evade/combat flags, loot state. Otherwise only
  the entry.
- **quest**: quest proto entry; player's quest status and objective counters; whether
  quest giver / turn-in NPCs are in range.
- **aura**: the aura instance (caster, effects, tick timing) if still active, plus the
  spell entry.
- **object**: entry, state (open/closed), loot, linked triggers.
- **generic**: nothing beyond the always-included part.

### BugReportUploader

- One worker thread, mutex-protected queue, max 200 reports (oldest dropped on
  overflow).
- `POST {apiUrl}/api/bugs` via `https_client::sendRequest` with header `X-Api-Key`.
  Retries on network error and 5xx with backoff 5 s, 30 s, 2 min, 2 min, 2 min (5
  attempts); 4xx are dropped immediately.
- Health: three consecutive failed attempts or a full queue → marks the `BugReport`
  subsystem unavailable; the next 2xx → available again.
- The worker thread never logs. Results (success, drop, failure) are posted to the io
  thread, which logs and updates subsystem health.
- `Stop()` wired into the world server shutdown handler (`tools/shutdown_check.py`
  must stay green). Remaining queued reports are dropped on shutdown.
- Upload target `file://<dir>` writes each report as `<timestamp>-<n>.json` into that
  directory instead of posting (used by E2E).

### Configuration

World server config keys: `bugReport.enabled` (default off), `bugReport.apiUrl`,
`bugReport.apiKey`. Disabled or missing URL/key → subsystem unavailable.

## Protocol

- New client↔realm opcodes: `CMSG BugReport`, `SMSG BugReportResult`,
  `SMSG SubsystemStatus`. Bump `game::ProtocolVersion`.
- New realm↔world messages: `BugReport`, `BugReportResult`, `SubsystemStatus`,
  `SetSubsystemEnabled`. Bump `auth::ProtocolVersion`.
- Run `python tools/protocol_version_check.py --update` and record both bumps in
  `docs/protocol_versions.md`.

## Central API (`H:\mmo-error-report`)

### Repository

`git init` with a `.gitignore` covering `.env`, `uploads/`, `node_modules/`, `logs/`.
First commit = existing state, bug API changes on top.

### Model `Bug`

```
schemaVersion: Number
createdAt: Date
reporter: { accountId, characterId, characterName, realm }
subject: { type, id, guid, name }            // index on (type, id)
comment: String (≤ 1000)
client: Mixed                                 // client JSON without logTail
logTail: String
server: Mixed                                 // world snapshot as delivered
serverBuild: String, worldNode: String        // lifted for filtering
attachments: [{ kind, fileName, size, mime }] // reserved, empty in v1
status: new | triaged | in_progress | pr_open | resolved | wontfix | duplicate
claimedBy, claimedAt, prUrl, duplicateOf
triage: { category, severity, component, summary }
history: [{ at, actor, change }]
```

### Routes

| Route | Key | Behaviour |
|---|---|---|
| `POST /api/bugs` | ingest | body ≤ 512 KB, schema validated (400 with details, 413 too large), returns `{ bugId }` |
| `GET /api/bugs?status=&subjectType=&subjectId=&since=&page=&limit=` | reader | newest first, `limit` ≤ 100, `{ bugs, pagination }`; list items omit `server`, `client`, `logTail` |
| `GET /api/bugs/:id` | reader | full document |
| `POST /api/bugs/:id/claim { worker }` | reader | 409 if claimed by another worker and the claim is younger than 2 h |
| `PATCH /api/bugs/:id { status, triage, prUrl, duplicateOf, note, actor }` | reader | updates fields, appends to `history` |

### Auth

`X-Api-Key` compared in constant time against `BUG_INGEST_KEYS` / `BUG_READER_KEYS`
(comma-separated, for rotation). Missing/wrong key → 401. No keys configured →
`/api/bugs` rejects every request (fail closed). Crash routes are unchanged.

### Tests

jest + supertest + mongodb-memory-server: auth (both keys, fail-closed), validation,
size limit, list filters, claim race and expiry, PATCH history.

## CLI (`tools/bugs/bugs.py`)

`list [--status …] [--subject type:id]`, `show <id>`, `claim <id> --worker <name>`,
`update <id> [--status …] [--note …] [--pr …]`. API base and reader key from
`MMO_BUG_API_URL` / `MMO_BUG_API_KEY`. Foundation for the later AI triage job.

## Web UI (`H:\mmo-error-report-ui`)

- Baseline commit of the current dirty state first (excluding `.env`, `build/`).
- New routes `/bugs` (list: status/subject-type filters, pagination, subject, comment
  excerpt, character, created) and `/bugs/:id` (subject, comment, reporter, client
  info, collapsible JSON views for server snapshot and client JSON, log tail, history;
  status / triage / note editing). Navbar entry.
- The UI calls same-origin `/api/bugs*`. The UI container's nginx proxies these to the
  backend and adds `X-Api-Key` from the container environment; the key never appears
  in the bundle. The whole site sits behind HTTP basic auth (`htpasswd` mounted into
  the container).

## Error handling summary

| Where | Condition | Result |
|---|---|---|
| Client | no answer within 10 s | "No response from the server", Submit re-enabled |
| Client | disconnect / leave world | dialog closes |
| Realm | not in world / subsystem unavailable | `DISABLED` |
| Realm | packet > 64 KB | `TOO_LARGE` |
| Realm | rate limit hit | `RATE_LIMITED` |
| World | bad zlib, > 256 KB decompressed, bad JSON | `INVALID`, warning log |
| World | queue full / sustained API failure | subsystem unavailable, pushed to clients |
| API | no/wrong key | 401 |
| API | schema violation / too large | 400 / 413 |

## Testing

- **C++ unit tests** (headless, in the gate): packet round-trips for all new
  client↔realm and realm↔world messages; zlib caps including a decompression bomb;
  uploader retry/backoff and health transitions with an injected transport (no real
  HTTP); realm subsystem table (per-node aggregation, node disconnect, node change,
  manual + automatic combination); rate limiter; client JSON sanitizing.
- **E2E** `e2e/scenarios/bug_report.lua`: test world server configured with a
  `file://` upload target under `e2e/runtime/`. Asserts the initial
  `SUBSYSTEM_STATUS_CHANGED`, submits an item report, expects `ACCEPTED`, checks the
  written JSON contains subject and server snapshot, runs `.subsystem bugreport off`,
  expects the status event, and expects `DISABLED` on the next submit.
- **Backend**: jest suite above.
- **Web UI**: TypeScript build + manual check in the browser pane against a local
  backend.
- **Real client**: F6 hint on item and spell tooltips, dialog opens with the right
  subject, micro-menu button hides when the subsystem is turned off.
- `tools/shutdown_check.py` stays green.
