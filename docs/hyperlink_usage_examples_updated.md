# Hyperlink Usage Examples

This document demonstrates how to use the hyperlink system in the MMO client.

## Hyperlink Syntax

The hyperlink system supports two formats:

### Format 1: Simple Format (no brackets)
```
|Htype:payload|htext|h
```

### Format 2: Bracketed Format 
```
|Htype:payload|h[text]|h
```

## Color Support

Hyperlinks can optionally be combined with color formatting:

### With Color
```
|cFFFF0000|Htype:payload|htext|h|r
|cFF00FF00|Htype:payload|h[text]|h|r
```

### Without Color (uses current text color)
```
|Htype:payload|htext|h
|Htype:payload|h[text]|h
```

## Examples

### 1. Item Links

```lua
-- Simple item link (uses current text color)
ChatFrame:AddMessage("Found |Hitem:12345|hEpic Sword|h!", 1.0, 1.0, 1.0);

-- Item link with color and brackets
ChatFrame:AddMessage("Found |cFFFF6600|Hitem:12345|h[Epic Sword]|h|r!", 1.0, 1.0, 1.0);
```

### 2. Quest Links

```lua
-- Quest link
ChatFrame:AddMessage("Accept |cFFFFFF00|Hquest:789|h[Kill the Dragon]|h|r quest.", 1.0, 1.0, 1.0);

-- Simple quest link
ChatFrame:AddMessage("Accept |Hquest:789|hKill the Dragon|h quest.", 1.0, 1.0, 1.0);
```

### 3. Spell Links

```lua
-- Spell link with color
ChatFrame:AddMessage("Learned |cFF4080FF|Hspell:456|h[Fireball]|h|r!", 1.0, 1.0, 1.0);
```

### 4. Custom Links

```lua
-- Custom link types
ChatFrame:AddMessage("Visit |Hlocation:stormwind|hStormwind City|h.", 1.0, 1.0, 1.0);
ChatFrame:AddMessage("Join |Hchannel:general|h[General Chat]|h.", 1.0, 1.0, 1.0);
```

## Event Handling

When a hyperlink is clicked, it triggers a `HYPERLINK_CLICKED` event:

```lua
-- In ChatFrame.lua or other UI script
this:RegisterEvent("HYPERLINK_CLICKED", function(self, type, payload)
    if type == "item" then
        -- Handle item link click
        print("Clicked item with ID: " .. payload);
        ShowItemTooltip(tonumber(payload));
    elseif type == "quest" then
        -- Handle quest link click
        print("Clicked quest with ID: " .. payload);
        OpenQuestLog(tonumber(payload));
    elseif type == "spell" then
        -- Handle spell link click
        print("Clicked spell with ID: " .. payload);
        ShowSpellTooltip(tonumber(payload));
    else
        -- Handle other link types
        print("Clicked hyperlink of type: " .. type .. " with payload: " .. payload);
    end
end);
```

## Color Behavior

1. **With Explicit Color**: The hyperlink text uses the specified color
2. **Without Color**: The hyperlink text uses the current text color at the time the hyperlink was parsed
3. **Color Inheritance**: Hyperlinks properly inherit and restore surrounding text colors

## Display Text

- **Bracketed Format**: `[text]` - The brackets are included in the displayed text
- **Simple Format**: `text` - The text is displayed as-is without brackets

## Integration

### In Chat System
```lua
-- These will automatically be parsed and rendered with clickable hyperlinks
SendChatMessage("Check out this |cFF0080FF|Hitem:12345|h[Awesome Weapon]|h|r!", "SAY");
```

### In UI Components
```lua
-- Any TextComponent or ScrollingMessageFrame will automatically parse hyperlinks
someTextFrame:SetText("Click |Hhelp:controls|hhere|h for help.");
```

## Best Practices

1. **Use meaningful types**: Choose descriptive type names like "item", "quest", "spell", "player", etc.
2. **Include necessary data in payload**: Put all required information in the payload (IDs, parameters, etc.)
3. **Provide clear display text**: Make it obvious what the link does
4. **Handle all link types**: Ensure your event handler can deal with all hyperlink types you use
5. **Use colors consistently**: Establish color conventions for different link types (items = orange, quests = yellow, etc.)

## Common Color Codes

- **Gray/Poor Items**: `|cFF9D9D9D`
- **White/Common Items**: `|cFFFFFFFF`  
- **Green/Uncommon Items**: `|cFF1EFF00`
- **Blue/Rare Items**: `|cFF0070DD`
- **Purple/Epic Items**: `|cFFA335EE`
- **Orange/Legendary Items**: `|cFFFF8000`
- **Quests**: `|cFFFFFF00` (Yellow)
- **Spells**: `|cFF4080FF` (Light Blue)
- **System Messages**: `|cFFFFFF00` (Yellow)

## Limitations

- Hyperlinks spanning multiple wrapped lines may not render perfectly
- Complex nested formatting within hyperlinks is not supported
- Very long hyperlink text may affect text layout and wrapping
