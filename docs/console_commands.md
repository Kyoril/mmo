# MMO Client Console Commands

This document lists all console commands available in the MMO client, organized by category. Some commands are only available when developer commands are enabled (`MMO_WITH_DEV_COMMANDS` preprocessor flag).

## Default Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `ver` | None | Displays the client version. |
| `run` | `<filename>` | Executes a console script file (used for config files). |
| `quit` | None | Shuts down the game client immediately. |
| `list` | None | Shows all available console commands. |
| `clear` | None | Clears the console text. |
| `set` | `<cvar_name> <value>` | Sets a console variable to a given value. |
| `reset` | `<cvar_name>` | Resets a console variable to its default value. |
| `unset` | `<cvar_name>` | Removes a console variable. |
| `cvarlist` | None | Lists all registered console variables. |
| `saveconfig` | None | Saves all console variables into a config file. |

## Debugging Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `ToggleAxis` | None | Toggles visibility of the axis display. |
| `ToggleGrid` | None | Toggles visibility of the world grid display. |
| `ToggleWire` | None | Toggles wireframe render mode. |
| `ToggleCullingFreeze` | None | Toggles culling (freezes rendering in place). |
| `reload` | None | Reloads the user interface. |

## Game Commands

| Command | Parameters | Description |
|---------|------------|-------------|
| `bind` | `<key_name> <command>` | Binds an input action to a key. |
| `login` | `<username> <password>` | Connects to the login server and attempts to log in. |

## Game Master Commands

These commands are available when `MMO_WITH_DEV_COMMANDS` is defined and require the player to have an appropriate GM level set on their account:

| Command | Parameters | Description | Required GM Level |
|---------|------------|-------------|------------------|
| `followme` | None | Makes the selected creature follow you. | 1 |
| `faceme` | None | Makes the selected creature face towards you. | 1 |
| `level` | `[<levels>]` | Increases the target's level. Default is 1 level. | 2 |
| `money` | `<amount>` | Increases the target's money. | 2 |
| `createmonster` | `<entry>` | Creates a monster with the specified entry ID. | 2 |
| `destroymonster` | `[<guid>]` | Destroys the selected monster or a monster with the given GUID. | 2 |
| `additem` | `<item_id> [<count>]` | Adds an item to your inventory. Default count is 1. | 2 |
| `worldport` | `<mapid> <x> <y> <z>` | Teleports you to the specified coordinates on the given map. | 1 |
| `speed` | `<value>` | Changes your movement speed. | 1 |
| `summon` | `<playername>` | Summons the specified player to your location. | 2 |
| `port` | `<playername>` | Teleports you to the specified player's location. | 1 |
| `guildcreate` | `<guild_name>` | Creates a new guild with yourself as the leader. | 0 |

> **Note:** GM levels can be set using the Login Server's HTTP API with the `/gm-level` endpoint. Different commands require different GM levels as shown above. A player must have at least the specified GM level to execute the corresponding command.
>
> - GM Level 1: Basic commands like following, facing, speed adjustments
> - GM Level 2: Advanced commands like teleportation, level adjustment, money creation, and item creation

## Console Variables

In addition to commands, the system also uses console variables (CVars) that control various aspects of the game:

### Graphics CVars
- `gxApi` - Which graphics API should be used
- `gxResolution` - The resolution of the primary output window (default: 1280x720)
- `gxWindow` - Whether the application will run in windowed mode (default: 1)
- `gxVSync` - Whether the application will run with vsync enabled (default: 1)
- `perf` - Toggles whether performance counters are visible (default: 0)

### Game Settings CVars
- `dataPath` - The path of the client data directory
- `lastRealm` - ID of the last realm connected to (default: -1)
- `locale` - The locale of the game client (default: enUS)
- `realmlist` - Server address for the realm list (default: "mmo-dev.net")

### Debug CVars
- `DebugTargetPath` - Debug setting for path display
- `RenderShadows` - Determines whether shadows should be rendered (default: 1)
- `ShadowDepthBias` - Shadow depth bias setting (default: 50)
- `ShadowSlopeBias` - Shadow slope bias setting (default: 0.25)
- `ShadowClampBias` - Shadow clamp bias setting (default: 0.005)

## Notes

1. Console commands can be executed by pressing the tilde (~) or backslash (\) key to open the console.
2. Commands are executed by typing the command name followed by any required parameters.
3. Console variables can be viewed with `cvarlist` and modified with the `set` command.
4. Developer commands are only available when the `MMO_WITH_DEV_COMMANDS` preprocessor flag is defined.

Commands are registered in the system with `Console::RegisterCommand()`, which takes the command name, handler function, category, and help text.