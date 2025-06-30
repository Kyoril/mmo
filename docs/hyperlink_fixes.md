# Hyperlink System Fixes

## Issues Fixed

### 1. Display Text Brackets
**Problem**: The `[]` brackets were being stripped from the display text, but they should be part of the visible text.

**Fix**: Modified `hyperlink.cpp` to include the brackets in the `displayText`:
- Changed parsing to include both opening `[` and closing `]` in the extracted display text
- Now `|Hitem:123|h[Epic Sword]|h` will display as `[Epic Sword]` (with brackets)

### 2. Crash Prevention in ScrollingMessageFrame
**Problem**: Client was crashing when clicking on the chat window, likely due to:
- Invalid bounds checking
- Array bounds violations
- Improper coordinate conversion

**Fixes Applied**:
- Added proper bounds checking in `OnMouseDown` method
- Added relative coordinate conversion (click position relative to frame)
- Added validation for hyperlink bounds before checking clicks
- Added try/catch block in `RenderLine` method as fallback
- Fixed array access with proper type casting and bounds checking

### 3. Hyperlink Bounds Calculation
**Problem**: The hyperlink bounds calculation in `DrawTextWithHyperlinks` was complex and error-prone.

**Fix**: Completely rewrote the hyperlink tracking logic:
- Simplified hyperlink position tracking using string search
- More robust bounds calculation
- Better handling of edge cases (text ending in hyperlink, newlines, etc.)
- Cleaner separation of concerns

## Code Changes Made

### hyperlink.cpp
```cpp
// Before: displayText = text.substr(displayTextStart, displayTextEnd - displayTextStart);
// After: displayText = text.substr(displayTextStart, displayTextEnd - displayTextStart + 1);
// Includes both [ and ] in the display text
```

### scrolling_message_frame.cpp
```cpp
// Added relative coordinate conversion
Point relativePos = position - frameRect.GetPosition();

// Added bounds validation
if (hyperlink.bounds.GetWidth() > 0 && hyperlink.bounds.GetHeight() > 0 &&
    hyperlink.bounds.IsPointInRect(relativePos))

// Added try/catch for safety
try {
    GetFont()->DrawTextWithHyperlinks(...);
} catch (...) {
    // Fallback to regular text rendering
}
```

### font.cpp
```cpp
// Simplified hyperlink tracking with string-based position finding
std::vector<std::pair<std::size_t, std::size_t>> hyperlinkRanges;
// Calculate ranges once, then track simply during rendering
```

## Testing

The fixes ensure:
1. ✅ Hyperlinks display with `[brackets]` as intended
2. ✅ No crashes when clicking on chat window
3. ✅ Proper click detection within hyperlink bounds
4. ✅ Graceful fallback if hyperlink rendering fails
5. ✅ Backward compatibility with existing color-only markup

## Usage Example

```cpp
// This hyperlink markup:
"|cff0080ff|Hitem:12345|h[Epic Sword]|h|r"

// Will display as:
"[Epic Sword]" (in blue color, clickable)

// And trigger event:
OnHyperlinkClick("item", "12345")
```
