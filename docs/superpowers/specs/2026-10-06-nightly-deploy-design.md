# Nightly Automated Deployment — Design

Date: 2026-10-06
Status: Draft for review

## Goal

When `develop` moves during the day (agent fixes for bug reports, crash reports, tickets),
the next morning the live realm runs that code and the launcher has the matching client
patch — with no human in the loop. A bug reported today is fixed in the launcher tomorrow.

Success criteria:

- Every night where `develop` moved **and** the full release gate is green, the live stack
  and the live client patch switch to exactly that commit.
- A red gate, a failed staging step, or an unreachable realm ships nothing and leaves the
  live release untouched.
- A failed post-deploy health check rolls back automatically to the previous release.
- Players get a 15-minute in-game countdown before a fixed maintenance time (04:45 local).
- Client patch and servers always switch together (no window where the launcher serves a
  client the live servers reject, or vice versa).

Non-goals (v1): macOS client patch, multi-host deployments, zero-downtime deploys,
approval workflows, Docker image garbage collection (a host `docker image prune` cron suffices).

## Current state (2026-10-06)

| Step | State |
|---|---|
| Server Docker images | `docker.yml` pushes `kyoril/mmo_{login,realm,world}_server` + `mmo_data` on every push to develop/master, tagged `<branch>`, `<branch>-<shortsha>`, `latest` (= develop). Builds twice per develop push (once before tests). |
| Server hosting | Portainer stack from `compose.yml`, images `:latest`, MySQL on the host, `restart: on-failure`. |
| Windows client | Built only by `win-release.yml` on `master` (1023 commits behind develop). |
| Patch content | `update_compiler` run manually; `source.txt` only on unmerged branch `origin/feature/automated-patch-pipeline` (`data/patch/source.txt`). |
| Patch upload | Manual delta upload via a local console app to the nginx docroot of `patch.mmo-dev.net` on the root server. |
| Release decision | Manual. Local nightly gate is not registered on this workstation. |
| Timed shutdown | Implemented in realm `ShutdownManager`; reachable via realm REST `POST /shutdown delay=<s>`, `GET /shutdown`, `POST /shutdown/cancel` (HTTP Basic, port 8092). Clean shutdown exits 0, so `restart: on-failure` leaves containers down. |
| Launcher | Fetches `https://patch.mmo-dev.net/list.txt`, compares per-file SHA1, downloads changed files; self-updates `Launcher.exe`. No version number. |

Key facts that shape the design:

- Client data (`data/client` submodule) is ~8 GB. Shipping it whole each night is not acceptable;
  transfer must be delta-based.
- The patch docroot lives on the same root server as the Portainer stacks.
- `e2e_client` links only `bot_core`, `base`, `log`, `luabind` (no graphics), and the E2E
  scripts are PowerShell, which runs on Linux runners via `pwsh`.

## Architecture

```
 GitHub Actions (nightly-release.yml, ~00:30)
 ┌──────────┐   ┌──────────────┐   ┌───────────────┐   ┌────────────────────────────┐
 │ gate     │──▶│ client (win) │──▶│               │   │ GitHub prerelease          │
 │ (ubuntu, │   └──────────────┘   │   publish     │──▶│ nightly-YYYYMMDD-<sha8>    │
 │ +MySQL)  │──▶┌──────────────┐──▶│               │   │  release.json              │
 └──────────┘   │ images       │   └───────────────┘   │  client-bin.zip            │
                │ :<sha> → Hub │                       │  symbols.zip               │
                └──────────────┘                       └─────────────┬──────────────┘
                                                                     │ polled (pull)
 Root server                                                         ▼
 ┌──────────────────────────────────────────────────────────────────────────────────┐
 │ mmo-deployer (own Portainer stack)                                               │
 │  stage:  download client-bin → git fetch mmo-data@pinned → update_compiler       │
 │          → /srv/mmo-patch/releases/<sha>                                         │
 │  04:45:  preflight → POST /shutdown delay=900 (all realms) → wait for exit       │
 │          → mysqldump → symlink current→releases/<sha> → Portainer stack update   │
 │            (MMO_TAG=<sha>, pull) → verify /uptime → notify | auto-rollback       │
 └──────────────────────────────────────────────────────────────────────────────────┘
        nginx root = /srv/mmo-patch/current          Portainer game stack: kyoril/mmo_*:${MMO_TAG}
```

The design is pull-based: GitHub holds no credentials to the root server, and timing is
owned by the server (GitHub cron can be delayed by 15–60 min).

## Component 1 — CI release production

New workflow `.github/workflows/nightly-release.yml`.

**Triggers:** `schedule` (~00:30 UTC) and `workflow_dispatch` (optional `force` input).

**Job `check`:** resolves `develop` HEAD; if it equals the `commit` of the newest
`nightly-*` release and `force` is not set, all further jobs are skipped.

**Job `gate`** (ubuntu-latest, MySQL 8 service container):

- Checks out the repo with the `data/editor` submodule only (not `data/client`, saving disk).
- Runs the full gate: protocol check, build (servers, tools, `MMO_BUILD_BOTS`), unit tests,
  tool tests, E2E. Reuses `tools/gate/verify.ps1 -Tier full` under `pwsh`. Linux
  adaptations (single-config generators, binary names without `.exe`, `bin/` layout, MySQL
  connection via env) are part of this work and must keep the Windows path working.
- Uploads the gate report (`last_report.json`) and E2E logs as workflow artifacts.
- Fallback if Linux E2E proves impractical: run E2E on the Windows runner with a MySQL
  setup action. SQLite as a second DB provider was considered and rejected (second SQL
  dialect for every migration and query).

**Job `client`** (windows-latest, needs `gate`):

- Release build of `mmo_client`, `Launcher`, `mmo_error` (the binaries referenced by
  `deploy/patch/source.txt`), reusing the configure/build steps from `win-release.yml`.
- Produces `client-bin.zip` (binaries + required DLLs, laid out as `source.txt` expects)
  and `symbols.zip` (PDBs, consumable by `tools/archive_symbols.ps1` so `/fix-crash` can
  symbolicate live crashes).

**Job `images`** (ubuntu-latest, needs `gate`):

- Builds and pushes `mmo_login_server`, `mmo_realm_server`, `mmo_world_server`,
  `mmo_data`, `mmo_deployer`, each tagged with the full commit SHA.

**Job `publish`** (needs all): creates GitHub prerelease `nightly-YYYYMMDD-<sha8>` with
assets `client-bin.zip`, `symbols.zip`, `release.json`.

**`release.json`** — the contract between CI and the deployer:

```json
{
  "schema": 1,
  "commit": "<full sha>",
  "image_tag": "<full sha>",
  "data_client_commit": "<data/client submodule sha>",
  "data_editor_commit": "<data/editor submodule sha>",
  "created_at": "2026-10-07T00:52:11Z",
  "assets": { "client-bin.zip": { "sha256": "..." } },
  "gate": { "passed": true, "steps": [ { "name": "e2e", "passed": true, "duration_s": 412 } ] },
  "changes": [ "fix(loot): ...", "fix(crash): ..." ]
}
```

`changes` = commit subjects on the first-parent chain since the previous nightly's commit.

**Related repo changes:**

- `deploy/patch/source.txt` — salvaged from `origin/feature/automated-patch-pipeline`,
  with paths relative to a workspace that mirrors the repo layout (`bin/`, `data/client/`).
- `docker.yml` — remove the unconditional `push` build on develop so images are built only
  after "Linux Servers" passes; add the full-SHA tag.
- `compose.yml` — images become `kyoril/mmo_*:${MMO_TAG:-latest}`; `data-service` syncs
  with delete semantics (no stale files accumulating in `mmo-shared-data`).

## Component 2 — `mmo-deployer`

A Python 3 service under `deploy/deployer/`, packaged as image `mmo_deployer`
(multi-stage: builds a Linux `update_compiler` from the same commit). Runs in its own
Portainer stack on the root server.

### Mounts and configuration

| Mount | Purpose |
|---|---|
| `/srv/mmo-patch` (bind, host path identical) | `releases/<sha>/`, `current` symlink (relative). nginx `root` points at `/srv/mmo-patch/current`. |
| `/state` (volume) | `state.json`, `mmo-data` git checkout, workspace, `backups/`, logs. |

| Env var | Meaning |
|---|---|
| `GITHUB_REPO`, `GITHUB_TOKEN` | Release source (read-only token). |
| `PORTAINER_URL`, `PORTAINER_TOKEN`, `PORTAINER_STACK_ID`, `PORTAINER_ENDPOINT_ID` | Game stack to update. |
| `REALMS` | JSON list of `{name, url, password}` for realm REST endpoints. |
| `LOGIN_URL`, `LOGIN_PASSWORD` | Login REST, used for health checks. |
| `MYSQL_HOST`, `MYSQL_USER`, `MYSQL_PASSWORD`, `BACKUP_DATABASES` | Pre-deploy dump. |
| `MAINTENANCE_AT` (default `04:45`), `TZ`, `COUNTDOWN_S` (default `900`) | Schedule. |
| `POLL_INTERVAL_S` (default `900`), `KEEP_RELEASES` (default `3`), `KEEP_BACKUPS` (default `7`) | Housekeeping. |
| `NOTIFY_WEBHOOK` | Discord/Slack-compatible webhook URL. |
| `DRY_RUN` | If `1`, maintenance actions are logged, not executed. |

### State (`/state/state.json`)

`live` (sha + release tag), `staged` (sha + tag), `bad` (list of shas never to stage again),
`paused` (bool), `history` (last N events). Written atomically (temp file + rename).

### Stage loop (every `POLL_INTERVAL_S`)

1. List `nightly-*` releases; pick the newest whose `commit` is not `live`, not `staged`,
   not in `bad`. Intermediate releases are skipped.
2. Download `release.json` and `client-bin.zip`; verify sha256.
3. `git fetch` the `mmo-data` checkout and `checkout --force data_client_commit`.
4. Assemble workspace (`bin/` from the zip, `data/client` → the checkout) and run
   `update_compiler -s <workspace>/deploy/patch -o releases/<sha>.tmp`.
5. Sanity check: `list.txt` exists and parses, and the launcher, client and every `hpak2`
   root from `source.txt` are present.
6. Rename `releases/<sha>.tmp` → `releases/<sha>`; set `staged`; notify "staged" with `changes`.

Any failure: delete the `.tmp` directory, keep `staged`/`live` unchanged, notify, retry next poll.

### Maintenance (daily at `MAINTENANCE_AT`, only if `staged` ≠ `live` and not `paused`)

1. **Preflight:** every realm and login answers `GET /uptime`; Portainer API reachable;
   `releases/<staged>` intact. Any failure → abort night, notify.
2. **Schedule shutdown:** `POST /shutdown delay=COUNTDOWN_S` on every realm. 409 (already
   shutting down) counts as success. If any realm fails, `POST /shutdown/cancel` on those
   that accepted, abort, notify.
3. **Wait** until realm and world containers have exited (`GET /shutdown` and Portainer
   container state), timeout `COUNTDOWN_S + 30 min`. On timeout, continue: the stack
   update sends SIGTERM, which the realm handles as an immediate graceful shutdown
   (`stop_grace_period: 5m`).
4. **Backup:** `mysqldump` of `BACKUP_DATABASES` into `/state/backups/<timestamp>-<sha8>/`.
   Failure aborts the deploy: the realms are already down, so start the stack again on
   the current `live` tag (same as a rollback without a symlink flip).
5. **Flip:** create `current.new` → `releases/<sha>`, `rename` over `current` (atomic).
6. **Redeploy:** Portainer stack update with env `MMO_TAG=<sha>` and `pullImage: true`.
7. **Verify:** within 5 min, login and every realm answer `GET /uptime`.
8. **Success:** `live = staged`; prune releases beyond `KEEP_RELEASES` (never `live` or
   the previous live) and backups beyond `KEEP_BACKUPS`; notify "live" with `changes`.

### Rollback (verify failed, or `deployer rollback`)

Flip `current` back to the previous live release, Portainer update with the previous
`MMO_TAG`, verify again. On success: add the failed sha to `bad`, notify loudly with log tail.
If rollback verification fails too: stop, set `paused`, notify "manual intervention required".

### CLI (`docker exec mmo-deployer deployer <cmd>`)

`status`, `pause`, `resume`, `stage-now`, `deploy-now [sha]` (runs maintenance immediately
with the configured countdown), `rollback`, `unbad <sha>`.

## Cross-cutting rules

- **Additive migrations:** login/realm DB migrations must keep the previous server build
  working on the new schema (add columns/tables; drop or rename only in a later release once
  no rollback target needs them). Otherwise automatic rollback is unsafe. Added to CLAUDE.md
  under "Database Migrations".
- **Protocol changes** need no special handling: client and servers switch in the same
  maintenance window, and the existing `ProtocolVersion` check rejects stale clients until
  the launcher has updated them.
- The deployer never deploys `latest`; only immutable `<sha>` tags.

## Testing

- **Deployer unit tests** (pytest, `deploy/deployer/tests/`), with GitHub, Portainer, realm
  REST, MySQL dump and clock faked:
  - ordering: stage → maintenance → verify
  - each failure case above, including partial shutdown-scheduling with cancel
  - intermediate releases are skipped; `bad` releases are never staged
  - atomic `.tmp` and symlink handling on a temp filesystem
  - rollback, and rollback-failure → paused

  Run in the CI `gate` job and registered as a `tools/gate` tool test.
- **Local integration:** `deploy/test/compose.yml` with a fake realm/login REST container
  and a fake Portainer API, exercising a full night end to end.
- **Dry run on the real server:** `DRY_RUN=1` for the first week — real staging, logged
  maintenance.

## Rollout

1. **CI release production:** Linux E2E, `nightly-release.yml`, `deploy/patch/source.txt`,
   `docker.yml` fix, SHA tags. Each night yields a gated release with a downloadable client.
2. **Deployer staging only:** patches appear in `releases/<sha>`; flip by hand. Replaces the
   manual `update_compiler` + delta upload.
3. **Deployer maintenance:** `compose.yml` `MMO_TAG`, nginx root change, data-volume sync fix,
   DB backups, notifications; `DRY_RUN` first, then live.
4. **Polish:** write `changes` into launcher news (`launcher/launcher.json`); additive-migration
   rule in CLAUDE.md; `docs/deployment.md` runbook.

## Manual setup (outside the repo, listed in the runbook)

- GitHub: Docker Hub secrets (already exist); confirm the repo's Actions minutes and
  disk usage on hosted runners.
- Root server: create `/srv/mmo-patch`, move the current patch content into
  `releases/<current>` with `current` → it; change nginx `root` once; create the Portainer
  API token; set `MMO_TAG` on the game stack; deploy the deployer stack with its env;
  set up the webhook URL.

## Open risks

- Hosted runner disk and time budget for a full build plus E2E (mitigated by skipping the
  `data/client` checkout in `gate`; caching the build tree).
- `update_compiler` and E2E on Linux are unverified; verify both first in phase 1.
- An `update_compiler` run over 8 GB on the root server may take minutes and contend for
  CPU with the live realm. Staging happens hours before maintenance, and `-j` can be limited.
