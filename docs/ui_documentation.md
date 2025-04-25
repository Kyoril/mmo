# MMO User Interface Documentation

## Overview

This document provides a comprehensive overview of the UI system used in the MMO project. The UI is divided into two main sections:

1. **GlueUI** - The out-of-game interface (login screens, character selection, etc.)
2. **GameUI** - The in-game interface (player frames, action bars, chat, etc.)

Both systems use a common underlying framework based on Lua scripting with C++ bindings.

## Core UI Architecture

The UI system is built on a frame-based architecture where UI elements are organized in a hierarchy. The system is managed by the `FrameManager` class in C++ which handles:

- Layout management
- Event handling
- Input capture
- Frame rendering

### Key Components

- **Frames**: Basic UI containers that can contain other frames
- **Buttons**: Interactive elements that respond to clicks
- **Text Fields**: For text input
- **Models**: For displaying 3D character models
- **Textures**: For displaying images and icons

## UI Panel System

The game employs a sophisticated panel management system that controls how UI windows are shown, hidden, and arranged. This system is primarily managed through the GameParent.lua file.

### Panel Areas

- **Left**: Standard UI panels (character window, spellbook, etc.)
- **Center**: Dialog boxes and some special panels
- **Full**: Full-screen panels that hide the game world
- **Undefined**: Custom panels that don't follow standard positioning rules

### Panel Functions

- `ShowUIPanel(frame, force)`: Shows a UI panel, handling positioning based on panel type
- `HideUIPanel(frame)`: Hides a UI panel
- `CloseAllWindows(ignoreCenter)`: Closes all UI panels except optionally center panels
- `MovePanelToLeft()`, `MovePanelToCenter()`: Repositions panels between areas

## GlueUI Components

### AccountLogin

The login screen that handles user authentication.

```
Key functions:
- AccountLogin_Login(): Initiates login process
- AccountLogin_OnRealmList(): Called when realm list is received
- AccountLogin_AuthError(): Handles authentication errors
```

### RealmList

Displays available game realms for connection.

```
Key functions:
- RealmList_Show(): Displays the realm selection screen
- RealmList_Accept(): Connects to selected realm
- RealmListItem_Clicked(): Handler for realm selection
```

### CharSelect

Character selection screen.

```
Key functions:
- CharList_Show(): Displays available characters
- CharSelect_EnterWorld(): Enters the game world with selected character
- CharSelect_CreateCharacter(): Opens character creation screen
```

### CharCreate

Character creation interface.

```
Key functions:
- CharCreate_Show(): Shows character creation UI
- CharCreate_Submit(): Creates a new character
- SetupCustomization(): Sets up character customization options
```

### GlueDialog

Dialog system for the login screens.

```
Key functions:
- GlueDialog_Show(which, text, data): Shows a dialog by type
- GlueDialog_Hide(): Hides the current dialog
- GlueDialog_Button1Clicked(): Handles primary button clicks
```

## GameUI Components

### GameParent

The main frame that contains all in-game UI elements.

```
Key functions:
- GameParent_OnLoad(): Sets up game events
- ShowUIPanel(), HideUIPanel(): Controls panel visibility
- CloseAllWindows(): Hides all UI elements
```

### PlayerFrame

Displays player information (health, mana, level).

```
Key functions:
- PlayerFrame_Update(): Updates player information
- PlayerFrame_OnCombatModeChanged(): Handles combat state changes
- PlayerFrame_UpdateLeader(): Updates group leader status
```

### TargetFrame

Displays information about the player's target.

```
Key functions:
- TargetFrame_Update(): Updates target information
- TargetFrame_UpdateAuras(): Updates target buffs/debuffs
- TargetFrame_OnUnitUpdate(): Handles target unit changes
```

### PartyFrame

Manages the display of party member information.

```
Key functions:
- PartyFrame_OnMembersChanged(): Updates when party composition changes
- PartyMemberFrame_UpdateMember(): Updates individual party member display
- PartyMemberFrame_OnClick(): Handles clicking on party members
```

### ChatFrame

Manages the in-game chat system.

```
Key functions:
- ChatFrame_SendMessage(): Sends chat messages
- ChatFrame_ParseText(): Parses slash commands
- ChatEdit_UpdateHeader(): Updates chat input header based on chat type
```

### InventoryFrame

Displays player inventory and equipped items.

```
Key functions:
- InventoryFrame_Load(): Sets up inventory display
- InventoryFrame_UpdateSlots(): Updates inventory slot display
- InventoryFrame_UpdateMoney(): Updates player money display
```

### VendorFrame

Interface for buying and selling items with NPCs.

```
Key functions:
- VendorFrame_Show(): Displays the vendor interface
- VendorFrame_UpdateVendorItems(): Updates vendor item listings
- VendorButton_OnClick(): Handles buying/selling items
```

### GuildFrame

Interface for guild management.

```
Key functions:
- GuildRoster_Update(): Updates guild member listings
- GuildFrame_InviteClicked(): Invites new members
- GuildFrame_OnEvent(): Handles guild-related events
```

### GameMenu

In-game menu system.

```
Key functions:
- AddMenuButton(): Adds buttons to the game menu
- GameMenu_OnLoad(): Sets up the game menu
- ToggleGameMenu(): Shows/hides the game menu
```

### StaticDialog

In-game dialog system.

```
Key functions:
- StaticDialog_Show(): Shows a dialog by type
- StaticDialogs: Table containing all dialog definitions
- StaticDialog_Button1Clicked(): Handles dialog button clicks
```

### MoneyFrame

Displays currency in gold/silver/copper format.

```
Key functions:
- RefreshMoneyFrame(): Updates money display
- MoneyFrame_SetType(): Sets money frame behavior type
```

### GameTooltip

System for displaying tooltip information.

```
Key functions:
- GameTooltip_SetItem(): Sets tooltip for items
- GameTooltip_SetAura(): Sets tooltip for buffs/debuffs
- GameTooltip_AddLine(): Adds text to tooltips
```

## UI Anchoring System

The UI uses an anchor-based layout system where elements are positioned relative to each other or their parent containers.

```lua
frame:SetAnchor(AnchorPoint.TOP, AnchorPoint.BOTTOM, relativeFrame, 0)
```

Available anchor points:
- TOP, BOTTOM, LEFT, RIGHT
- TOPLEFT, TOPRIGHT, BOTTOMLEFT, BOTTOMRIGHT
- H_CENTER, V_CENTER, CENTER

## Event System

UI frames can register for events and handle them with callback functions.

```lua
frame:RegisterEvent("EVENT_NAME", callbackFunction)
```

Common events:
- PLAYER_ENTER_WORLD: Fired when player enters the world
- PLAYER_TARGET_CHANGED: Fired when player changes target
- UNIT_HEALTH_UPDATED: Fired when unit health changes
- CHAT_MSG_SAY: Fired when a chat message is received

## Dialog System

The game has two dialog systems:
1. **GlueDialog**: For login screens
2. **StaticDialog**: For in-game

Both follow a similar pattern where dialogs are defined in a table with properties:
- text: The dialog message
- button1, button2: Button text
- OnAccept, OnCancel: Button handlers
- timeout: Auto-close timeout

## Slash Command System

The chat system supports slash commands via the SlashCmdList table.

```lua
SlashCmdList["COMMAND_NAME"] = function(msg)
    -- Command implementation
end

SLASH_COMMAND_NAME1 = "/command"  -- Primary alias
SLASH_COMMAND_NAME2 = "/cmd"      -- Alternative alias
```

## UI Creation Patterns

Common patterns for creating UI elements:

### Creating a Button
```lua
local button = ButtonTemplate:Clone()
button:SetText("Button Text")
button:SetClickedHandler(OnButtonClicked)
parentFrame:AddChild(button)
```

### Positioning Elements
```lua
button:SetAnchor(AnchorPoint.TOP, AnchorPoint.TOP, nil, yOffset)
button:SetAnchor(AnchorPoint.LEFT, AnchorPoint.LEFT, nil, xOffset)
```

### Creating a Panel Window
```lua
function MyPanel_OnLoad(self)
    SidePanel_OnLoad(self)
    self:RegisterEvent("RELEVANT_EVENT", MyPanel_OnEvent)
end

function MyPanel_Toggle()
    if (MyPanel:IsVisible()) then
        HideUIPanel(MyPanel)
    else
        ShowUIPanel(MyPanel)
    end
end
```

## Best Practices

1. Use templates for commonly repeated UI elements
2. Register for only the events you need
3. Unregister events when frames are hidden
4. Use appropriate anchoring to maintain layout during screen resize
5. Follow the naming conventions:
   - FunctionName for functions
   - variableName for variables
   - CONSTANT_NAME for constants

## Debug UI Tools

The game includes a debug UI system accessible through the debug menu button:

```lua
function DebugUI_Toggle()
    if (DebugPanel:IsVisible()) then
        DebugPanel:Hide()
    else
        DebugPanel:Show()
    end
end
```

Debug UI components can be added with:
```lua
DebugUI_AddMenuButton('BUTTON_NAME', function()
    -- Debug action
end)
```