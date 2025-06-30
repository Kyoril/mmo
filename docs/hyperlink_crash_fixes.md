# Hyperlink Crash Fixes and Improvements

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

## Testing Verification

To test the fixes:

1. **Hyperlink Display**: Enter text like `|cFF00FF00|Hitem:12345|h[Epic Sword]|h|r` in chat
   - Verify that `[Epic Sword]` displays with brackets visible
   
2. **Click Detection**: Click on the hyperlink text in the chat window
   - Should not crash
   - Should trigger the `OnHyperlinkClick` event with correct type and payload
   
3. **Multiple Hyperlinks**: Test multiple hyperlinks in the same message
   - Each should be clickable independently
   
4. **Edge Cases**: Test with very long messages, wrapped text, and special characters
   - Should handle gracefully without crashes

## Future Improvements

1. **Visual Styling**: Add underline or different color for hyperlinks
2. **Multi-line Hyperlinks**: Better support for hyperlinks that span wrapped lines  
3. **Accessibility**: Add keyboard navigation support for hyperlinks
4. **Performance**: Optimize bounds calculation for many hyperlinks
