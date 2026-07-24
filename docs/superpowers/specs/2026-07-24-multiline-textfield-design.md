# Multi-line TextField for frame_ui — Design

**Date:** 2026-07-24
**Status:** Approved

## Problem

The frame_ui `TextField` is strictly single-line: Enter is swallowed, newlines are
stripped on paste, overflow is handled by a horizontal scroll offset, and the caret
is drawn spanning the entire frame height. The MailFrame letter body
(`MailSendBodyField`, a 300-unit-tall field) therefore behaves badly: the cursor
renders as a huge vertical bar and typed text scrolls off to the right instead of
wrapping.

## Goals

- A `TextField` can opt into multi-line editing via XML.
- Word wrap while typing; Enter inserts a hard newline.
- Caret rendered at the correct line with font-line height.
- Up/Down caret navigation, line-relative Home/End, click-to-position on any line.
- Vertical scrolling that follows the caret when content exceeds the field height.
- No behavior or visual change for existing single-line fields.

## Non-goals (follow-ups)

- Text selection (mouse drag, shift+arrows).
- Mouse-wheel scrolling / scrollbar widget.
- Undo/redo.
- Masked (`Masked=true`) multi-line fields — masked fields stay single-line.

## Approach

Soft-wrap layout, non-destructive: the underlying text only contains hard `\n`
where the user pressed Enter. Word wrap is computed at layout time and never
inserts characters into the text. Plain-text character indices therefore stay
stable, which keeps hyperlink bookkeeping, the caret index model, and all
`GetText()` consumers (e.g. MailFrame.lua sending the letter) working unchanged.

The rejected alternative — inserting real `\n` at wrap points — would corrupt the
text on resize and break hyperlink ranges.

## Components

### 1. Shared line-breaking helper (`src/shared/frame_ui/text_wrap.h/.cpp`)

Dependency-free (std + `utf8_utils.h` only) so the headless `unit_tests` target can
compile it directly, following the `remote_movement_queue.cpp` precedent.

```cpp
struct TextWrapLine
{
    std::size_t startByte;   // first byte of the line in the source string
    std::size_t endByte;     // one past the last rendered byte (excludes the \n / wrap space)
    std::size_t startChar;   // first plain-text character index of the line
    std::size_t endChar;     // one past the last plain-text character index
    float width;             // pixel width of the rendered line
};

/// advanceFn: returns the pixel advance for a codepoint (tab is handled by the
/// algorithm as 4x space using advanceFn(' ')).
std::vector<TextWrapLine> ComputeLineBreaks(
    const std::string& text,
    float maxWidth,
    const std::function<float(uint32_t codepoint)>& advanceFn);
```

Break rules (identical to `Font::GetLineCount`):
- Hard break at `\n` (the `\n` belongs to the line it ends, but is not rendered).
- Soft break at the last space/tab on the line when the next glyph would exceed
  `maxWidth`.
- Unbreakable word wider than the area: character-level break.
- Single codepoint wider than the area: keep it on the line (forward-progress
  guarantee — the algorithm always terminates).
- Empty text yields one empty line; text ending in `\n` yields a trailing empty line.

### 2. TextField changes (`src/shared/frame_ui/textfield.h/.cpp`)

- New `MultiLine` boolean property (default `False`), wired like `Masked` /
  `AcceptsTab`, copied in `Copy()`. Accessors `IsMultiLine()` / `SetMultiLine()`.
- Cached line layout (`std::vector<TextWrapLine>`), recomputed when text, frame
  width, font, or text scale changes. Built from the parsed **plain text** so
  hyperlink display text wraps like normal text.
- Caret index ↔ (line, column) mapping helpers based on the cached layout. The
  caret index at a soft-wrap boundary belongs to the **start of the next line**.
- Key handling when multi-line:
  - Enter inserts `\n` at the caret (both `OnKeyDown` swallow and `OnKeyChar`
    filter are lifted).
  - Up/Down move one line, clamping the column to the target line length.
  - Home/End move to start/end of the current visual line.
  - Left/Right/Backspace already operate on character indices and work unchanged
    (`\n` counts as one character).
- Paste when multi-line: keep newlines; normalize `\r\n` and `\r` to `\n`.
- Scrolling when multi-line: horizontal scroll offset stays 0; new vertical
  scroll offset (pixels). `EnsureCursorVisible` gets a vertical branch keeping
  the caret line inside the visible band.
- Click-to-position: pick the line from the local y (plus vertical scroll), then
  the column via the existing advance walk over that line only.

### 3. Renderer changes (`src/shared/frame_ui/textfield_renderer.cpp`)

When the field is multi-line:
- Draw line-by-line at `textArea.top + lineIndex * lineHeight - verticalScroll`,
  skipping lines fully outside the visible band.
- Caret: 2px × font-line-height rect positioned at the caret's (line, column).
- Vertical alignment property is ignored (content is top-aligned).

Single-line fields take the exact same code path as today.

### 4. MailFrame (`data/client/Interface/GameUI/MailFrame.xml`)

`MailSendBodyField` gets `<Property name="MultiLine" value="True" />`. No other
layout change.

## Testing

- Unit tests (TDD) for `ComputeLineBreaks` with a fake advance function:
  no wrap needed, wrap at word boundary, multiple spaces, tab handling,
  unbreakable long word, single oversized codepoint, hard newlines, empty text,
  trailing newline, UTF-8 multi-byte characters.
- Unit tests for the caret index ↔ line/column mapping helper (exposed as a free
  function in `text_wrap.h` operating on the line vector).
- Manual verification in the game client MailFrame: typing with wrap, Enter,
  arrows, click positioning, caret size, vertical scroll following the caret,
  and no regression in chat box / name / subject / money fields.
