# Nightly deployment

Design: [docs/superpowers/specs/2026-10-06-nightly-deploy-design.md](superpowers/specs/2026-10-06-nightly-deploy-design.md)

## 1. Overview

1. 00:30 UTC (02:30 Berlin in summer; the cron is UTC, while `MAINTENANCE_AT` 04:45 is in `TZ`, Europe/Berlin): `.github/workflows/nightly-release.yml` gates `develop` on Linux (full gate incl. E2E), builds the images and client patch, and publishes a `nightly-*` release (with `release.json`).
2. The `mmo-deployer` container on the root server polls GitHub and stages the newest green release (patch files, data, `update_compiler`). The stager refuses a release whose `release.json` gate is not green.
3. 04:45 (Europe/Berlin): maintenance. Preflight (login, realms, Portainer token), 15-minute realm shutdown countdown, DB backup, patch `current` flip, Portainer redeploy with `MMO_TAG=<sha>`.
4. Verify: within 300 s there must be two consecutive rounds in which login and every realm answer `/uptime` and Portainer reports every `WAIT_SERVICES` container (default `realm_server_01,world_node_01`, so the world node too) as `running`. Otherwise the deployer rolls back to the previous release.
5. Notify: every step posts to `NOTIFY_WEBHOOK` (the URL itself is never included in a message).

## 2. One-time server setup

Order matters: the game stack is redeployed with the new `compose.yml` FIRST (it creates the named network `mmo_game_servers`), then the deployer stack (which joins it as an external network).

1. **Patch root**
   ```bash
   sudo mkdir -p /srv/mmo-patch/releases /srv/mmo-patch/launcher
   sudo mv <old docroot> /srv/mmo-patch/releases/legacy
   sudo ln -s releases/legacy /srv/mmo-patch/current
   sudo mv /srv/mmo-patch/releases/legacy/launcher/* /srv/mmo-patch/launcher/
   ```
2. **nginx**: in the patch vhost use `root /srv/mmo-patch/current;` and `location /launcher/ { alias /srv/mmo-patch/launcher/; }`. Then:
   ```bash
   sudo nginx -t && sudo systemctl reload nginx
   curl -fsS https://patch.mmo-dev.net/list.txt | head -3
   curl -fsSI https://patch.mmo-dev.net/launcher/launcher.json
   ```
   Both requests must still answer.
3. **Game stack**
   - Paste the new `compose.yml` into the Portainer web editor.
   - Add stack env `MMO_TAG=<sha of the currently running images>`, or `latest` for the very first time.
   - Note the stack ID and endpoint ID from the Portainer URL (`#!/<endpoint>/docker/stacks/<name>?id=<stack>`).
   - The stack must be a web-editor stack, not a Git-backed one.
4. **Portainer API token**: My account, Access tokens, add `mmo-deployer`. The deployer pings an authenticated endpoint in preflight, so a wrong token aborts the night before any shutdown.
5. **GitHub token** (`GITHUB_TOKEN`): fine-grained, repository `Kyoril/mmo`, permission *Contents: read*. Not needed if the repo is public.
6. **MySQL backup user**
   ```sql
   CREATE USER 'deployer'@'%' IDENTIFIED BY '<pw>';
   GRANT SELECT, SHOW VIEW, TRIGGER, LOCK TABLES, EVENT ON mmo_login.* TO 'deployer'@'%';
   GRANT SELECT, SHOW VIEW, TRIGGER, LOCK TABLES, EVENT ON mmo_realm_01.* TO 'deployer'@'%';
   ```
   `mmo_world_01` is not backed up because the world server does not use its database yet. If that changes, add it to `BACKUP_DATABASES` and grant it.
7. **First clone of mmo-data**: the first staging run clones about 830 MB and checks out 8 GB into the `deployer-state` volume. Expect the first stage to take a while. `DATA_REPO_URL` must be an HTTPS URL (the image has no ssh client).
8. **Deployer stack**
   - Paste `deploy/deployer/compose.yml` into a new Portainer stack and set its env: `DEPLOYER_TAG`, `GITHUB_TOKEN`, `PORTAINER_URL`, `PORTAINER_TOKEN`, `PORTAINER_STACK_ID`, `PORTAINER_ENDPOINT_ID`, `REALMS`, `LOGIN_PASSWORD` (the login server's REST API password, i.e. `LOGIN_WEB_API_PASSWORD` of the game stack), `MYSQL_USER`, `MYSQL_PASSWORD`, `NOTIFY_WEBHOOK`, `DRY_RUN`.
   - `DEPLOYER_TAG=<sha>` of a green nightly.
   - `REALMS=[{"name":"realm01","url":"http://mmo-dev-realm-01:8092","password":"<WEB_API_PASSWORD>"}]`
   - Keep `DRY_RUN=1` (also the default in the compose file). Other defaults (maintenance time, countdown, retention `KEEP_RELEASES=3` / `KEEP_BACKUPS=7`) live in `deploy/deployer/mmo_deployer/config.py`.

The patch is built from `deploy/patch/source.txt`, which packs every `data/client` folder the client loads, including `Particles` (`Particles.hpak`). `Cache` (a runtime cache the client fills itself) and `Editor` (editor-only assets) are intentionally not packed.

## 3. Going live

1. Watch at least one dry-run night: a "Staged ..." message, then a "[dry-run] would deploy ..." message at 04:45.
2. Then do the first real deploy by hand and while watching: set `DRY_RUN=0`, redeploy the deployer stack, then
   ```bash
   docker exec mmo-deployer deployer deploy-now
   ```
3. From the second successful deploy on, automatic rollback has a previous release to return to. Before that, a failed first deploy has nothing to roll back to and ends in MANUAL INTERVENTION.

## 4. Daily operation

```bash
docker exec mmo-deployer deployer status | pause | resume | stage-now | deploy-now [sha] | rollback | unbad <sha>
```

(Pick one command per call.) Commands are queued as request files and applied within one tick. A request that crashes is dropped, never retried, and reported by notification.

- `pause` / `resume`: skip or resume staging and maintenance.
- `stage-now`: poll GitHub on the next tick.
- `deploy-now [sha]`: run the maintenance now with the countdown. It runs even when paused and counts as that day's maintenance (no second run at 04:45).
- `rollback`: return to the previous live release immediately.
- `unbad <sha>`: allow a release marked bad to be staged again.

Notifications:

| Message | Meaning |
|---|---|
| `Deployer started (...)` | Service (re)started; shows live/staged/dry-run. |
| `Staged <tag> (N changes)` | New release staged, goes live at the next 04:45. |
| `Staging <tag> failed; retrying` | Staging error (once per release); live release unchanged. |
| `[dry-run] would deploy` | Dry run; nothing changed. |
| `Maintenance for <sha> aborted, nothing changed` | Preflight failed or a realm shutdown could not be scheduled. |
| `Maintenance started` | Countdown running, servers go down in 15 min. |
| `Database backup failed ... restarting` | Backup failed after the shutdown; stack is restarted on the live release. |
| `Manual rollback from X to Y: servers restart now.` / `Rollback done: Y is live.` | A manual `rollback` started / finished. |
| `rollback: there is no previous release to return to` | `rollback` requested but `previous_live` is empty. |
| `deploy-now: ...` | Reply to `deploy-now`: could not list releases, no release for that sha, or nothing new is staged. |
| `post-deploy cleanup step failed: ...` | Pruning releases/backups or the patch note failed after a successful deploy; harmless. |
| `Release <sha> failed its health check; rolling back` | Verify failed, rolling back. |
| `Release <sha> was rolled back` | Rollback healthy; release marked bad. |
| `Release <sha> is live` | Success, with the change list. |
| `MANUAL INTERVENTION REQUIRED` | Deployer paused; see the playbook. |
| `Request <cmd> failed` | A CLI request crashed and was dropped. |

Backups: `/state/backups/<timestamp>-<sha8>/` in the `deployer-state` volume, one `<db>.sql.gz` per database. Restore with `gunzip -c <db>.sql.gz | mysql <db>`.

## 5. Failure playbook

- **Gate red**: nothing shipped, the live release keeps running. Open the `gate-report` artifact of the failed `Nightly Release` run and fix on develop; the next night ships.
- **Staging failed**: the data commit was not pushed to `mmo-data`, or the `deployer-state` volume or patch disk is full. The deployer retries every poll and notifies once; fix the cause, or run `stage-now`.
- **Aborted** (notification `... aborted, nothing changed`): a realm, the login server or Portainer was unreachable at 04:45 (or a shutdown could not be scheduled). Nothing changed. Fix the service, then `deploy-now`, or wait for the next night. If the DB backup fails after the shutdown, the deployer restarts the stack on the live release and notifies "Database backup failed (...); restarting the stack ..."; the staged release is retried the next night. Only if that restart also fails (or there is no live release yet) it escalates to MANUAL INTERVENTION.
- **Rolled back**: the release failed its health check and the previous release is live again. The release is marked bad and not staged again. Fix it on develop; the next night ships the fix. To retry the same commit use `unbad <sha>`.
- **Paused after MANUAL INTERVENTION** (release failed and rollback failed, or backup failure with no working restart): the deployer is paused and the failed release is marked bad and cleared from staging; `live` still names the last good release. Restore it by hand (`deploy-now` and `rollback` cannot do this: `deploy-now <live sha>` answers "nothing new is staged", and `rollback` targets the older `previous_live`):
  1. `docker exec mmo-deployer deployer status`, read `live`.
  2. On the host: `sudo ln -sfn releases/<live> /srv/mmo-patch/current.new && sudo mv -T /srv/mmo-patch/current.new /srv/mmo-patch/current`
  3. In Portainer set the game stack env `MMO_TAG=<live>` and redeploy with "pull latest image".
  4. Check that login and realm come up.
  5. Optionally `docker exec mmo-deployer deployer unbad <failed sha>` if the release should be retried.
  6. `docker exec mmo-deployer deployer resume`.

## 6. Updating the deployer itself

Change `DEPLOYER_TAG` to a newer green SHA and redeploy its stack. It never updates itself.

## 7. CI notes

- The Linux gate runs on the runner's preinstalled MySQL (root/root). The CI run proving the pipeline has not happened yet; if Linux E2E fails, move the gate job to `windows-latest` and start MySQL 8.0 (root/root) with `shogo82148/actions-setup-mysql`.
- `docker.yml` builds develop images only after "Linux Servers" passes.
- `win-release.yml` (master) is unchanged.
