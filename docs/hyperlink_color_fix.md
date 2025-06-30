# Testing Hyperlink Colors in ScrollingMessageFrame

## The Issue
When using hyperlinks with colors in chat messages like:
```
/say Look at that |cff0080ff|Hitem:123|h[Sword]|h|r!
```

The text was rendering as all white instead of showing `[Sword]` in blue.

## Root Cause
The `ScrollingMessageFrame::OnMessagesChanged()` method was not properly copying color changes from the original parsed message to the individual line cache entries. It was only setting the default message color, losing hyperlink colors.

## Fix Applied
Modified the line caching logic to:
1. Find color changes that occur within each wrapped line's text range
2. Copy those color changes with positions adjusted relative to the line start
3. Properly handle hyperlink position adjustments for line-relative coordinates

## Testing

### Test Case 1: Simple Hyperlink with Color
```lua
-- In game chat, type:
/say Look at that |cff0080ff|Hitem:123|h[Sword]|h|r!

-- Expected result:
-- "Look at that [Sword]!" where [Sword] appears in blue
```

### Test Case 2: Multiple Colors
```lua
/say Items: |cffff0000|Hitem:123|h[Red Sword]|h|r and |cff00ff00|Hitem:456|h[Green Shield]|h|r

-- Expected result:
-- [Red Sword] in red, [Green Shield] in green
```

### Test Case 3: Hyperlink Without Explicit Color
```lua
/say Check |Hquest:789|hthis quest|h out

-- Expected result:
-- "this quest" appears in the default white SAY color
```

### Test Case 4: Long Message with Wrapping
```lua
/say This is a very long message that should wrap to multiple lines and contains |cff0080ff|Hitem:123|h[Blue Item]|h|r in the middle

-- Expected result:
-- [Blue Item] should still appear in blue even if the line wraps
```

## Code Changes

### In `scrolling_message_frame.cpp`:
- Enhanced `OnMessagesChanged()` to properly copy color changes to line cache
- Added position adjustment for both colors and hyperlinks relative to line start
- Improved hyperlink range detection using `plainTextStart`/`plainTextEnd`

### Key improvements:
1. **Color Preservation**: Color changes are now properly copied to each line
2. **Position Mapping**: Colors and hyperlinks are correctly positioned relative to line boundaries  
3. **Fallback Handling**: Default colors are applied when color mapping fails

## Verification
The hyperlink system should now correctly render:
- Hyperlink text in specified colors
- Proper color resets after hyperlinks
- Multiple hyperlinks with different colors in the same message
- Color inheritance for hyperlinks without explicit colors
