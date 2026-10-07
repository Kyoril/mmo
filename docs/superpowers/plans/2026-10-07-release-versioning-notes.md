# Release Versioning, Player Patch Notes and Launcher Release Cycle — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Nightly releases carry a real version (`X.Y.Z.<commit count>`), WoW-style player patch
notes written by a free AI model, and a launcher that only changes when it is released on purpose.

**Architecture:** The game version comes from the newest `vX.Y.Z` tag plus `git rev-list --count`;
the launcher version from the newest `launcher-vX.Y.Z` tag. The nightly publish job writes AI
patch notes (GitHub Models, `GITHUB_TOKEN`, no paid key) into `release.json`, falling back to the
cleaned commit list. The deployer renders those notes into `launcher.json` as `Patch <version>`
and replaces the nightly-built `Launcher.exe` with the newest launcher release before compiling
the patch.

**Tech Stack:** CMake, Python 3 stdlib (+ optional `grpcio-tools`/`protobuf` for data names),
GitHub Actions, GitHub Models (`openai/gpt-4o-mini`), the existing deployer.

## Global Constraints

- Python tools: stdlib only for the release path; data-name extraction is best-effort and must
  never fail a release.
- A nightly must never fail because of patch notes (any notes error → fallback notes).
- `release.json` stays `schema: 1`; new fields (`version`, `patch_notes`) are additive and optional
  for the deployer.
- Launcher patch entries: `title` ≤ 180 bytes, `date` ≤ 80, `summary` ≤ 400, `body` ≤ 6 KiB
  (docs/launcher_content/README.md).
- No wire change → no protocol bump.
- Tabs, `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` header on every new file.

## Decisions (from the user, 2026-10-07)

- AI runs in the GitHub Action, with a free model (no paid key) → GitHub Models via `GITHUB_TOKEN`.
- AI input: commit subjects and bodies, diff stats per area, and data changes (new or renamed
  quests, items, spells, units and zones, plus data repo commit messages).
- MAJOR.MINOR.PATCH advances only by manual `vX.Y.Z` tags; the 4th component is the commit count.
- The launcher has its own `launcher-vX.Y.Z` tags and its own release workflow.

---

### Task 1: Version from tags (game and launcher)

**Files:** `cmake/mmo_version.cmake`, `version.h.in`, `src/launcher/launcher.rc`,
`src/launcher/launcher_view.cpp`, `src/launcher/launcher_content.cpp`,
`src/launcher/platform/win32/main_win32.cpp`, create `tools/release/version.py`,
test `tools/tests/test_release_version.py`.

- `version.py`: `parse_tag(tag, prefix) -> (major, minor, patch) | None`,
  `describe(repo, prefix) -> (major, minor, patch)` via
  `git describe --tags --abbrev=0 --match "<prefix>[0-9]*"`, `game_version(repo, commit="HEAD") -> str`
  (`X.Y.Z.<count>`), `launcher_version(repo, commit="HEAD") -> str` (`X.Y.Z`), CLI
  `python tools/release/version.py [--launcher] [--commit C]`.
- CMake: same rules; defines `MMO_VERSION_MAJOR/MINOR/PATCH`, `MMO_LAUNCHER_VERSION_*`.
  `version.h.in` emits `MMO_VERSION_RES/STR` from them and `MMO_LAUNCHER_VERSION_RES/STR`
  (4th = 0). The launcher's resource and UI use the launcher version.
- Test: temp git repo with tags `v0.2.1`, `launcher-v1.0.0`, `build-5`; assert both functions;
  assert that `cmake -P` over the shared cmake logic yields the same numbers (skipped without cmake).

### Task 2: Nightly workflow and manifest carry the version

**Files:** `.github/workflows/nightly-release.yml`, `tools/release/make_release_manifest.py`,
`tools/tests/test_release_tools.py`.

- `client` checkout gets `fetch-depth: 0` (this is the `0.1.0.1` bug).
- `check` job outputs `version`; release tag becomes `<prefix><version>`.
- Manifest gets `--version` → `"version"` and `--patch-notes FILE` → `"patch_notes"`.

### Task 3: AI patch notes

**Files:** create `tools/release/patch_notes.py`, `tools/release/data_changes.py`,
test `tools/tests/test_patch_notes.py`.

- `collect_commits(repo, previous, commit)` → list of `{subject, body}` (no merges).
- `is_internal(subject, files)` drops CI/gate/test/docs/deployer/tooling-only commits.
- `area_stats(repo, previous, commit)` → `{area: changed_files}`.
- `data_changes.py`: `submodule_log(...)` (data repo commit messages, via a fetch of the public
  repo), `diff_names(old_dir, new_dir)` → added/renamed names per catalog, best-effort.
- `build_prompt(context, budget_chars=20000)` condenses with priority data > feat > fix > rest.
- `call_github_models(prompt, token, model, http)` → JSON text.
- `validate_notes(obj)` → `{headline, sections:[{title, bullets[]}]}` with known section titles.
- `fallback_notes(commits)` groups cleaned subjects (feat → General, fix → Bug Fixes).
- `render_markdown(notes)` used for the GitHub release body and the launcher body.
- CLI writes `patch_notes.json` and `notes.md`; never exits non-zero for AI failures.

### Task 4: Deployer renders patch notes and pins the launcher

**Files:** `deploy/deployer/mmo_deployer/{news.py,state.py,app.py,stage.py,config.py}`,
tests in `deploy/deployer/tests/`.

- `State` gains `staged_version`, `staged_notes`.
- `add_patch_note(path, date_text, changes, version=None, notes=None)`: title
  `Patch <version>` (or `Update <date>` without version), summary = notes headline, body =
  rendered sections; old behaviour when `notes` is missing.
- `Stager`: after extracting `client-bin.zip`, fetch the newest `launcher-v*` release
  (`LAUNCHER_RELEASE_PREFIX`, default `launcher-v`) and overwrite `Launcher.exe` with its asset
  (sha256 checked against its `launcher.json` sidecar asset); no launcher release → keep the
  nightly's binary (bootstrap). The manifest returned gets `launcher_version`.

### Task 5: Launcher release workflow + docs

**Files:** create `.github/workflows/launcher-release.yml`, modify `docs/deployment.md`, create
`docs/versioning.md`.

- Trigger on `launcher-v*` tag push (+ `workflow_dispatch`); build `launcher` Release on
  windows-latest with full history; publish release `launcher-vX.Y.Z` with `Launcher.exe` and
  `launcher-release.json` (`{version, commit, sha256, size}`).
- Docs: how to cut a game milestone (`git tag v0.3.0`), how to release the launcher, how notes are made.
