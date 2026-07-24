// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mmo
{
	/// A single visual line produced by word-wrapping a UTF-8 string.
	struct TextWrapLine
	{
		/// First byte of the rendered line content in the source string.
		std::size_t startByte = 0;
		/// One past the last rendered byte (excludes a consumed separator or newline).
		std::size_t endByte = 0;
		/// First plain-text character (codepoint) index of the line.
		std::size_t startChar = 0;
		/// One past the last rendered plain-text character index.
		std::size_t endChar = 0;
		/// Pixel width of the rendered line content.
		float width = 0.0f;
	};

	/// A caret position expressed as a visual line and a column within that line.
	struct TextCaretLocation
	{
		/// Zero-based visual line index.
		std::size_t line = 0;
		/// Zero-based column (character offset from the line start).
		std::size_t column = 0;
	};

	/// Splits a UTF-8 string into visual lines no wider than maxWidth.
	/// Break rules match Font::GetLineCount: hard break at '\n', soft break at the
	/// last space/tab of the line, character-level break for unbreakable words, and
	/// a single codepoint wider than the area stays on its line so the algorithm
	/// always makes forward progress. Tabs measure as four spaces.
	/// @param advanceFn Returns the pixel advance for a codepoint.
	std::vector<TextWrapLine> ComputeLineBreaks(const std::string& text, float maxWidth, const std::function<float(uint32_t)>& advanceFn);

	/// Maps a plain-text caret index to its visual line and column. A caret sitting
	/// exactly on a mid-word wrap boundary belongs to the start of the next line;
	/// a caret on a consumed separator belongs to the end of the previous line.
	/// Out-of-range carets clamp to the end of the last line.
	TextCaretLocation GetCaretLocation(const std::vector<TextWrapLine>& lines, std::size_t caretCharIndex);

	/// Maps a visual line and column back to a plain-text caret index. Both the
	/// line and the column are clamped to valid values.
	std::size_t GetCaretIndexAt(const std::vector<TextWrapLine>& lines, std::size_t line, std::size_t column);
}
