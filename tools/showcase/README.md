# Showcase screenshots

Tooling for staging and capturing marketing screenshots (README, website) of the real
client, with headless party members. Used to produce the gallery in `screenshots/`.

## How it works

- **The hero** is one real `mmo_client.exe` (Windows, D3D11) logged in as `showcase0`.
- **Everyone else** is an `e2e_client` running `director.lua`. The director polls
  `runtime/<Name>.cmd.lua` and runs every new block of Lua it finds there, so a capture
  session can choreograph bots live: move them, start a staged `Fight(...)`, post
  party chat. A newer command interrupts a running `Fight`. Bots also revive
  themselves and re-arm godmode. Pass `-Protect <hero guid>` to keep the hero healed
  during fights.
- **Captures** copy only the client area of the game window, so there is never a
  Windows frame or title bar. The window must be on top and the desktop visible
  (monitors on, session unlocked).

## Setup (once per dev stack)

```powershell
powershell -File tools/bots/bots_provision.ps1 -Count 5 -Prefix showcase -Password showcasepass `
	-LoginRest http://127.0.0.1:8090 -WebUser mmo-web -WebPassword test
```

Characters are created by the bots on first login (Kaelith, Brannoc, Seraphine,
Ysolde, Doran). Characters created this way have **no appearance rows**, so they
render bald. Insert `character_customization` rows (realm DB) for hair style and
colour; the value ids come from the race's `.char` file.

## Session

```powershell
Import-Module ./tools/showcase/showcase.psm1
# back up bin/Debug/config/Config.cfg and RunOnce.cfg, then RunOnce: login showcase0 showcasepass
Start-ShowcaseParty -Protect "0xce"   # bots
Start-ShowcaseClient                  # real client, clicks Enter World
Send-ShowcaseChatLine "/invite Brannoc"  # bots accept: Send-ShowcaseCommand Brannoc "AcceptInvite()"
Send-ShowcaseConsole "worldport 0 345 1.3 600 0"
Send-ShowcaseCommand -Name Seraphine -Lua "Fight(45, {58, 13, 57}, 60, 40, 1400)"
Save-ShowcaseShot -Path shots/fight.png
```

Restore the backed-up config files afterwards.

## Traps

- Every input helper refuses to send input unless the game window is in the
  foreground. A lone Alt tap (used to steal focus) puts the window into menu mode,
  which swallows the next keystrokes, so `Set-ShowcaseFocus` only taps Alt when it has to.
- The console key is VK `0xC0`. GM console commands (`worldport`, `kill`,
  `additem`, `money`, `createmonster`, `revive`, `godmode`) need a GM account.
- Bots don't fall: worldporting a bot to a guessed height leaves it floating. Read
  the ground height with `pos` in the client console, port there, then `MoveTo`.
- A bot that changed maps keeps the old map's nav mesh, so `MoveTo` fails with
  `invalid_path`. Restart the bot.
- A restarted bot only knows objects that were spawned after it entered view.
  Use the client's `kill` on its target for leftovers.
- Bots only know creature names, not player names. Target players by GUID.
- Godmode only applies to the caster. The hero needs `godmode 1` from its own
  console, or a protecting bot.
