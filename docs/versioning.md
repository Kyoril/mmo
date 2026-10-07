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

The nightly `publish` job runs `tools/release/patch_notes.py`. It builds a condensed
context, at most about 18,000 characters, with the most important parts first:

1. New and renamed quests, items, spells, units and zones, decoded from the `data/editor`
   protobuf data at both submodule commits (this needs `grpcio-tools`).
2. Commit messages from the `data/editor` and `data/client` repositories.
3. Changed files per area.
4. Player-facing commit subjects and bodies.

Internal commits are dropped. These are `ci`, `test`, `docs`, `chore`, `build`, `refactor` and
`style` commits, scopes such as `gate`, `deployer` and `e2e`, and commits that only touch
`tools/`, `deploy/`, `.github/`, `docs/` or tests.

The script sends this context to `openai/gpt-4o-mini` on GitHub Models. The workflow's
`GITHUB_TOKEN` with `models: read` is the only credential; there is no paid key. To change
the model, set the `PATCH_NOTES_MODEL` environment variable. The free tier allows about 150
requests per day, and a nightly makes one.

The answer must be JSON with a headline and sections from a fixed list ("General",
"Classes: Mage", "Quests", "Bug Fixes", and so on). Anything else, and any HTTP error, falls
back to notes built from the cleaned commit subjects. A release never fails because of its
notes.

The notes are stored in `release.json` as `patch_notes`, together with `version`, and also
become the GitHub release body. At promotion the deployer writes them to `launcher.json`:

| Field | Value |
|---|---|
| `title` | `Patch <version>` |
| `date` | Promotion date |
| `summary` | The headline |
| `body` | The sections |

Releases from before this change carry no notes and keep the old commit list.

Try it locally without a token. This writes the fallback notes, and the data names need
`pip install grpcio-tools`:

```bash
python tools/release/patch_notes.py --previous <old sha> --commit HEAD --version 0.3.0.1 --out notes.json --markdown notes.md
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
