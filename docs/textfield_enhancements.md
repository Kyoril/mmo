# TextField Enhancements

## Overview

The TextField class has been enhanced with comprehensive support for:

1. **Horizontal Scrolling and Clipping**: Text that exceeds the width of the text field will scroll horizontally
2. **Hyperlink Token Support**: Hyperlinks are treated as single units that cannot be edited in the middle
3. **Cursor Visibility**: The cursor is always kept visible within the text field bounds

## Features Implemented

### Horizontal Scrolling

- Text that extends beyond the visible width of the TextField automatically scrolls
- The scroll offset is automatically adjusted when:
  - Typing text that moves the cursor off-screen
  - Using arrow keys to move the cursor
  - Clicking to position the cursor
  - Using Home/End keys

### Hyperlink Token Behavior

Hyperlinks in TextField are treated as atomic units with the following behaviors:

- **No Cursor Placement Inside**: Clicking on a hyperlink triggers the hyperlink event but does not place the cursor inside it
- **Atomic Deletion**: Pressing Backspace when the cursor is just after a hyperlink deletes the entire hyperlink
- **Skip Over Navigation**: Arrow keys skip over hyperlinks entirely rather than allowing navigation inside them
- **Protected from Editing**: Text insertion is prevented inside hyperlinks

### Key Bindings

- **Left/Right Arrow**: Move cursor, skipping over hyperlinks
- **Home**: Move cursor to the beginning of the text
- **End**: Move cursor to the end of the text
- **Backspace**: Delete previous character or entire hyperlink if cursor is after one

## Implementation Details

### New Member Variables

```cpp
float m_scrollOffset;           // Horizontal scroll offset for text display
ParsedText m_parsedText;        // Parsed text with hyperlinks and colors
bool m_parsedTextDirty;         // Whether the parsed text needs updating
```

### Key Methods

#### `EnsureCursorVisible()`
Automatically adjusts the scroll offset to ensure the cursor is always visible within the text field bounds.

#### `FindHyperlinkAtPosition(size_t cursorPos)`
Determines if a cursor position is within a hyperlink and returns the hyperlink index.

#### `DeleteHyperlink(int hyperlinkIndex)`
Removes an entire hyperlink from the text when backspace is pressed.

#### `UpdateParsedText()`
Parses the text for hyperlinks and color markup when the text has changed.

### Rendering Updates

The `PopulateGeometryBuffer()` method has been updated to:
- Apply clipping to the text area
- Render text with the scroll offset applied
- Use hyperlink-aware rendering when hyperlinks are present
- Only draw the cursor when the field has input focus and the cursor is visible

## Usage Examples

### Basic Text Input
```
|Hello, this is a test message that is longer than the field|
```
When text exceeds the width, it automatically scrolls to keep the cursor visible.

### Hyperlink Support
```
Check out |Hitem:12345|h[Sword of Awesomeness]|h for great damage!
```
The hyperlink `[Sword of Awesomeness]` is treated as a single token that cannot be edited in the middle.

### Mixed Content
```
Visit |Hurl:https://example.com|h[our website]|h for |cff00ff00|hgreen text|h.
```
Both hyperlinks and color markup are supported simultaneously.

## Technical Notes

- Text is always clipped to the visible area of the TextField
- Scroll offset is automatically managed to ensure cursor visibility
- Hyperlink parsing uses the existing `ParseTextMarkup` function
- Color inheritance works correctly with hyperlinks
- The implementation is backward-compatible with existing TextField usage

## Testing

To test the enhanced TextField:

1. Create a TextField with a limited width
2. Type text longer than the field width to test scrolling
3. Add hyperlink markup to test token behavior
4. Use arrow keys and mouse clicks to verify cursor positioning
5. Test backspace behavior on hyperlinks

The enhancements provide a modern, user-friendly text input experience while maintaining full compatibility with existing code.
