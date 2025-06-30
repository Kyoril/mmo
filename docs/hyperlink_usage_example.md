# Hyperlink Support in frame_ui

## Overview

The frame_ui library now supports inline hyperlinks in text using the following syntax:

```
|cffxxxxxx|Htype:payload|h[display_text]|h|r
```

Where:
- `|cffxxxxxx` - Sets the color (standard color syntax)
- `|H` - Begins the hyperlink
- `type:payload` - The link type and colon-delimited parameters
- `|h` - Marks the end of the payload
- `[display_text]` - The text shown to the user
- `|h` - Ends the hyperlink
- `|r` - Stops coloring

## Usage Example

### In Text
```
Hello |cff0080ff|Hitem:12345|h[Sword of Power]|h|r, click to see item details!
```

This would render "Hello [Sword of Power], click to see item details!" where "[Sword of Power]" (including the brackets) appears in blue and is clickable.

### In Lua Event Handler
```lua
frame:RegisterEvent("HYPERLINK_CLICKED", function(self, type, payload)
    if type == "item" then
        ShowItemTooltip(payload)  -- payload would be "12345"
    elseif type == "spell" then
        ShowSpellTooltip(payload)
    elseif type == "quest" then
        ShowQuestInfo(payload)
    end
end)
```

## Supported Features

1. **Generic Type System**: The hyperlink system doesn't depend on game-specific concepts. Any `type:payload` combination can be used.

2. **Visual Styling**: Hyperlinks can be colored using the standard color markup syntax.

3. **Click Detection**: The system automatically tracks hyperlink bounds and detects mouse clicks.

4. **Event Integration**: Hyperlink clicks are exposed as Lua events that can be handled by game-specific code.

5. **Markup Parsing**: The system properly parses both color changes and hyperlinks in the same text.

## Technical Details

- The `ParseTextMarkup` function in `hyperlink.h/cpp` handles parsing of the combined markup
- The `TextComponent` class automatically handles hyperlink rendering and click detection
- Mouse clicks on hyperlinks trigger the `OnHyperlinkClick` event with the type and payload
- Hyperlink bounds are calculated during text rendering for accurate hit-testing

## Chat Window Integration

For chat windows, you can now include hyperlinks in messages:

```
[Guild] Player: Check out this |cff9d9d9d|Hitem:12345:0:0:0:0:0:0:0|h[Epic Sword]|h|r I just found!
```

The chat system will automatically parse and render these hyperlinks, making them clickable for players to inspect items, spells, or other game objects.

### ScrollingMessageFrame Usage

The `ScrollingMessageFrame` class now supports hyperlinks automatically. When adding messages with hyperlinks:

```cpp
// Add a message with a hyperlink
scrollingFrame->AddMessage(
    "Found |cff0080ff|Hitem:12345|h[Legendary Sword]|h|r in the dungeon!",
    1.0f, 1.0f, 1.0f  // white text color
);

// Register event handler for hyperlink clicks
scrollingFrame->RegisterEvent("HYPERLINK_CLICKED", function(self, type, payload)
    if type == "item" then
        -- Show item tooltip or details
        ShowItemTooltip(tonumber(payload))
    elseif type == "spell" then
        -- Show spell information
        ShowSpellDetails(tonumber(payload))
    end
end)
```

### Features in ScrollingMessageFrame

1. **Automatic Parsing**: Messages are automatically parsed for hyperlinks during text wrapping
2. **Click Detection**: Mouse clicks on hyperlinks are detected and events are fired
3. **Text Wrapping**: Hyperlinks work with the existing text wrapping system
4. **Scrolling Support**: Hyperlinks remain clickable even when scrolling through message history

### Limitations

- Hyperlinks that span multiple lines due to text wrapping may not work correctly
- Complex hyperlink positioning across wrapped lines is simplified for performance
- Hyperlinks are detected using simple string matching within each wrapped line
