# Playtest bot in the real client

A bot that plays the rendered client on its own: it finds creatures, walks to them over the nav
mesh, fights with the character's spells, loots, rests, revives after a death and moves on to
where it found game before (or explores). It exists to find bugs in the real client code path
before players do. It is an internal QA tool: it never ships to players.

The headless swarm (`bot_swarm`) is a separate tool for load tests; this document is only about
the in-client bot.

## Pieces

- **Bot API** (`src/mmo_client/bot/`): Lua functions for positions, ranges, nav mesh movement,
  facing and interaction. Listed in [lua_api.md](lua_api.md#bot-functions-dev-builds-only).
  Movement presses the move key and turns the character, so the bot uses the same prediction,
  collision and network code a player does.
- **Bot addon** (`data/client/DevAddOns/Bot/`): the behaviour, in Lua. `BotConfig.lua` holds the
  tuning, routes and optional class rotations; `BotCombat.lua` picks spells; `Bot.lua` is the
  state machine and the `/bot` command.

## Gates

The bot is off three times over:

1. **Compile time.** Only built with `-DMMO_WITH_CLIENT_BOT_API=ON`, which configure refuses
   without `-DMMO_WITH_DEV_COMMANDS=ON`. The player client is built with dev commands on, so dev
   commands alone are not enough. Never enable the bot API in CI or release configurations.
2. **Data.** `DevAddOns` is not in the patch (`deploy/patch/source.txt`), and the nav data in
   `data/editor/nav` is read from disk; no nav archive ships.
3. **Runtime.** Every bot function does nothing until the console variable `botApiEnabled` is 1.
   The cast-failure events the bot listens to are raised only while it is on, too.

## Running it

1. Configure with `-DMMO_WITH_DEV_COMMANDS=ON -DMMO_WITH_CLIENT_BOT_API=ON` and build `mmo_client`.
2. Run the client against loose `data/client` with the editor data checked out next to it, so
   `data/editor/nav` exists. Another location goes into `botNavPath` (absolute, or relative to
   `dataPath`).
3. Log in, then enter `set botApiEnabled 1` in the console.
4. Type `/bot start` in chat. `/bot status` shows what it is doing, `/bot stop` (or a bare
   `/bot`) stops it.

Setting `botApiEnabled 0` stops all bot movement on the next frame. Without nav data for the
map, the bot reports "no navmesh" and stops; nothing else in the client changes.

## Changing the behaviour

The addon is plain Lua and reloads with the UI: edit it, then enter `reload` in the console.

- `BotConfig` - thresholds such as when to rest, which levels to fight and how far to look.
- `BotRoutes[mapId]` - positions to walk in order when the bot knows no grind spot worth
  revisiting. `GetCurrentMapId()` gives the map id.
- `BotClassRotations[className]` - spell ids to try first for a class, in order. Without one the
  rotation is derived from the spells the character knows, ranked the same way as the headless
  bot ranks them (`src/shared/game/spell_offense.h`).
