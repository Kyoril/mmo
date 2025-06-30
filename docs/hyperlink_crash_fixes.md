# Hyperlink Crash Fixes and Text Alignment Restoration

## Issues Fixed

### 1. Client Crashes When Clicking Chat Window

**Problem**: The client was crashing when clicking on the chat window after entering text with hyperlinks.

**Root Causes**:
- Coordinate system mismatch: Hyperlink bounds were calculated in absolute rendering coordinates, but mouse clicks were being checked against frame-relative coordinates
- Unsafe bounds checking and array access
- Missing null pointer checks in font rendering
- Index overflow in UTF-8 character processing

**Fixes Applied**:

#### In `ScrollingMessageFrame::OnMouseDown()`:
- Added proper coordinate conversion from absolute mouse position to frame-relative coordinates
- Implemented line-based hit testing to first check if click is within the correct line's Y range
- Added robust bounds checking for array access
- Fixed hyperlink bounds adjustment to use frame-relative coordinates

#### In `Font::DrawTextWithHyperlinks()`:
- Added safety checks for string length bounds to prevent index overflow
- Added null pointer checks for glyph images before rendering
- Improved UTF-8 character processing with bounds validation
- Enhanced error handling to prevent crashes during text rendering

#### In `Hyperlink` structure:
- Added `plainTextStart` and `plainTextEnd` fields to store exact positions in plain text
- This eliminates the need to search for display text, which was unreliable with duplicate text

### 2. Square Brackets Display Issue

**Problem**: The square brackets `[]` in hyperlink display text were being treated as formatting markers instead of actual display text.

**Solution**: 
- Modified the parser in `hyperlink.cpp` to include the square brackets `[` and `]` as part of the `displayText`
- The display text now correctly includes the brackets: `[Epic Sword of Doom]` instead of just `Epic Sword of Doom`
- This matches the expected behavior where brackets are visible to the user

### 3. Text Alignment Destruction in TextComponent

**Problem**: After implementing hyperlink support, all text alignment (horizontal/vertical) was broken in TextComponent.

**Root Cause**: The original rendering logic that properly handled LEFT/CENTER/RIGHT horizontal alignment and TOP/CENTER/BOTTOM vertical alignment was completely replaced with hyperlink-aware rendering that ignored alignment.

**Solution**: 
- Implemented a hybrid rendering approach in TextComponent
- Added two separate rendering methods:
  - `RenderTraditional()`: For text without hyperlinks, uses full alignment support
  - `RenderWithHyperlinks()`: For text with hyperlinks, provides basic alignment support
- The main `Render()` method now chooses the appropriate rendering path based on whether hyperlinks are present
- Traditional rendering preserves all original alignment functionality for non-hyperlink text
- Hyperlink rendering provides basic alignment while maintaining hyperlink functionality

## Technical Details

### Coordinate System Fix
```cpp
// Before: Using absolute coordinates for both bounds and mouse position
if (hyperlink.bounds.IsPointInRect(position))

// After: Converting to consistent coordinate system
Point relativePos = position - frameRect.GetPosition();
// ... calculate line position ...
Rect adjustedBounds = hyperlink.bounds;
adjustedBounds.top = lineY;
adjustedBounds.bottom = lineY + lineHeight;
if (adjustedBounds.IsPointInRect(relativePos))
```

### Bounds Safety
```cpp
// Added comprehensive bounds checking
if (i < 0 || i >= static_cast<int>(m_lineCache.size()))
    continue;
if (hyperlink.bounds.GetWidth() <= 0 || hyperlink.bounds.GetHeight() <= 0)
    continue;
```

### Plain Text Position Tracking
```cpp
// Store exact positions instead of searching
hyperlink.plainTextStart = hyperlinkTextStart;
hyperlink.plainTextEnd = plainTextPos;
```

### Text Alignment Restoration
```cpp
// Hybrid rendering approach
if (!m_parsedText.hyperlinks.empty())
{
    // Use hyperlink-aware rendering with basic alignment
    RenderWithHyperlinks(frameRect, c, textScale);
}
else
{
    // Use traditional rendering with full alignment support
    RenderTraditional(frameRect, c, textScale);
}
```

## Event System

- **Event Name**: `HYPERLINK_CLICKED` (matches the ALL_CAPS convention used by other game events)
- **Parameters**: `type` (string), `payload` (string)
- **Lua Handler**: Already implemented in `ChatFrame.lua`

## Testing Verification

To test the fixes:

1. **Hyperlink Display**: Enter text like `|cFF00FF00|Hitem:12345|h[Epic Sword]|h|r` in chat
   - Verify that `[Epic Sword]` displays with brackets visible
   
2. **Click Detection**: Click on the hyperlink text in the chat window
   - Should not crash
   - Should trigger the `HYPERLINK_CLICKED` event with correct type and payload
   
3. **Text Alignment**: Check UI elements with text alignment (buttons, labels, etc.)
   - LEFT/CENTER/RIGHT horizontal alignment should work correctly
   - TOP/CENTER/BOTTOM vertical alignment should work correctly
   - Multi-line text should render properly with alignment
   
4. **Multiple Hyperlinks**: Test multiple hyperlinks in the same message
   - Each should be clickable independently
   
5. **Edge Cases**: Test with very long messages, wrapped text, and special characters
   - Should handle gracefully without crashes

## Limitations

1. **Hyperlink Alignment**: Text with hyperlinks has limited alignment support compared to traditional text
2. **Multi-line Hyperlinks**: Hyperlinks spanning multiple wrapped lines may not work perfectly
3. **Performance**: Hyperlink-aware rendering is slightly slower than traditional rendering

## Future Improvements

1. **Enhanced Hyperlink Alignment**: Improve alignment support for hyperlink text
2. **Visual Styling**: Add underline or different color for hyperlinks
3. **Multi-line Hyperlinks**: Better support for hyperlinks that span wrapped lines  
4. **Accessibility**: Add keyboard navigation support for hyperlinks
5. **Performance**: Optimize bounds calculation for many hyperlinks
