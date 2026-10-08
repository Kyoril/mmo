# Versions, patch notes and launcher releases

## Game version

`MAJOR.MINOR.PATCH.REVISION`, for example `0.3.0.3441`:

- `MAJOR.MINOR.PATCH` comes from the newest `vX.Y.Z` tag reachable from the commit. It only
  moves when you tag a milestone by hand.
- `REVISION` is `git rev-list --count` of the commit. It goes up with every commit, so it
  orders all builds.

`cmake/mmo_version.cmake` computes it for the build (`version.h`: `Major`, `Minor`, `Build`,
`Revision`, `MMO_VERSION_STR`). `tools/release/version.py` computes the same number for the
workflows, and `tools/tests/test_release_version.py` checks that the two agree.

Builds need the full history. A shallow clone has no tags and a commit count of 1, which is
why every nightly client used to be `0.1.0.1`. Docker images are built without `.git` and
report `0.0.0.0`. Only logs show the server version.

Cutting a milestone (for example the next content patch):

```bash
git tag -a v0.4.0 -m "Patch 0.4.0" develop
git push origin v0.4.0
```

The next nightly is `0.4.0.<count>`. The nightly tag is `nightly-<version>`, and the GitHub
release and launcher entry are titled `Patch <version>`.

## Player patch notes

The nightly `publish` job runs `tools/release/patch_notes.py`. It builds a context of at most
60,000 characters, with the most important parts first:

1. New and renamed quests, items, spells, units and zones, decoded from the `data/editor`
   protobuf data at both submodule commits (this needs `grpcio-tools`).
2. Commit messages from the `data/editor` and `data/client` repositories.
3. Changed files per area.
4. Player-facing commit subjects and bodies.
5. Internal commit subjects only, for a short "Behind the Scenes" section. These are `ci`,
   `test`, `docs`, `chore`, `build`, `refactor` and `style` commits, scopes such as `gate`,
   `deployer`, `e2e` and `bug-loop`, and commits that only touch `tools/`, `deploy/`,
   `.github/`, `docs/` or tests.

Claude writes the notes: `claude -p` (Claude Code, installed by the job with npm), no tools,
our instructions as the system prompt, run in an empty directory so no `CLAUDE.md` leaks in.
The credential is the repository secret `CLAUDE_CODE_OAUTH_TOKEN`, created once with
`claude setup-token` from the maintainer's Claude subscription. There is no paid API key. The
default model is `opus`; set `PATCH_NOTES_MODEL` to change it. A nightly makes one call.

GitHub Models, the earlier free generator, was shut down on 2026-07-30. Its endpoint answers
every request with a plain `OK`, which is why the first versioned nightlies only had fallback
notes.

The voice is the hand-written "Night Watch" entry of 2026-10-07, kept in the script as
`STYLE_SAMPLE`: in-world, wry, addressed to "Adventurers", one dry aside on every second or
third bullet at most, nothing technical, nothing invented. The answer is JSON with a `title`
(the update's theme, such as "The Night Watch"), a `headline`, an `intro` of one or two short
paragraphs, and `sections`, ending with "Bug Fixes" and "Behind the Scenes". Anything
unusable, a missing token, or any process error falls back to notes built from the cleaned
commit subjects. A release never fails because of its notes.

The notes are stored in `release.json` as `patch_notes`, together with `version`, and also
become the GitHub release body. At promotion the deployer writes them to `launcher.json`:

| Field | Value |
|---|---|
| `title` | `Patch <version>: <title>` (just `Patch <version>` for fallback notes) |
| `date` | Promotion date |
| `summary` | The headline |
| `body` | The intro, then the sections |

Releases from before this change carry no notes and keep the old commit list. The deployer on
the root server must run this version to show titles and intros.

Try it locally with your own Claude Code login. `--context-out` shows what the model saw,
`--no-model` writes the fallback notes, and the data names need `pip install grpcio-tools`:

```bash
python tools/release/patch_notes.py --previous <old sha> --commit HEAD --version 0.3.0.1 --context-out context.txt --out notes.json --markdown notes.md
```

## Launcher releases

Every launcher binary that differs from the one players have makes every launcher update
itself. A launcher rebuilt every night differs in bytes even when its code did not change,
so the launcher has its own release cycle:

- Its version is `MAJOR.MINOR.PATCH` of the newest `launcher-vX.Y.Z` tag, with no commit
  count (`MMO_LAUNCHER_VERSION_STR`). The launcher's version label, its file version and its
  About box show this version.
- Pushing a `launcher-vX.Y.Z` tag runs `.github/workflows/launcher-release.yml`. It builds
  `Launcher.exe` and publishes a release with the binary and `launcher-release.json`
  (version, commit, sha256, size).
- When it stages a nightly, the deployer looks up the highest `launcher-v*` tag that has a
  release. It uses the tag list, so the launcher release never drops off the release list.
  It replaces the nightly's `Launcher.exe` with that release's binary after checking the
  checksum. Until the first launcher release exists, the nightly's own build ships.

Releasing a launcher change:

```bash
git tag -a launcher-v1.1.0 -m "Launcher 1.1.0" <commit on develop>
git push origin launcher-v1.1.0
```

The launcher reaches players with the next nightly patch. If develop has not moved, run the
Nightly Release workflow with `force` to ship it sooner.
