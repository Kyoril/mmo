// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "text_wrap.h"

#include "utf8_utils.h"

#include <algorithm>

namespace mmo
{
	std::vector<TextWrapLine> ComputeLineBreaks(const std::string& text, float maxWidth, const std::function<float(uint32_t)>& advanceFn)
	{
		std::vector<TextWrapLine> lines;

		std::size_t lineStartByte = 0;
		std::size_t lineStartChar = 0;
		float lineWidth = 0.0f;

		// Last soft-break opportunity (space/tab) on the current line.
		bool hasBreak = false;
		std::size_t breakByte = 0;       // byte index of the separator itself
		std::size_t breakChar = 0;       // character index of the separator itself
		std::size_t afterBreakByte = 0;  // first byte after the separator
		std::size_t afterBreakChar = 0;  // first character after the separator
		float widthAtBreak = 0.0f;       // line width up to (excluding) the separator

		std::size_t byteIndex = 0;
		std::size_t charIndex = 0;

		const auto finalizeLine = [&](std::size_t endByte, std::size_t endChar, float width)
		{
			TextWrapLine line;
			line.startByte = lineStartByte;
			line.endByte = endByte;
			line.startChar = lineStartChar;
			line.endChar = endChar;
			line.width = width;
			lines.push_back(line);
		};

		const auto startNewLine = [&](std::size_t startByte, std::size_t startChar)
		{
			lineStartByte = startByte;
			lineStartChar = startChar;
			lineWidth = 0.0f;
			hasBreak = false;
			byteIndex = startByte;
			charIndex = startChar;
		};

		while (byteIndex < text.length())
		{
			const std::size_t charStartByte = byteIndex;

			float advance;
			bool isBreakChar = false;

			if (text[byteIndex] == '\n')
			{
				// Hard line break: the newline is consumed, not rendered.
				finalizeLine(charStartByte, charIndex, lineWidth);
				startNewLine(charStartByte + 1, charIndex + 1);
				continue;
			}

			if (text[byteIndex] == '\t')
			{
				advance = advanceFn(static_cast<uint32_t>(' ')) * 4.0f;
				isBreakChar = true;
				byteIndex++;
			}
			else if (text[byteIndex] == ' ')
			{
				advance = advanceFn(static_cast<uint32_t>(' '));
				isBreakChar = true;
				byteIndex++;
			}
			else
			{
				const uint32_t codepoint = utf8::next_codepoint(text, byteIndex);
				if (codepoint == 0 && byteIndex > charStartByte)
				{
					// Invalid byte sequence: skip it without counting a character.
					continue;
				}

				advance = advanceFn(codepoint);
			}

			if (isBreakChar)
			{
				// Record the break opportunity before the overflow check so that a
				// separator which itself overflows still acts as the break point.
				hasBreak = true;
				breakByte = charStartByte;
				breakChar = charIndex;
				afterBreakByte = byteIndex;
				afterBreakChar = charIndex + 1;
				widthAtBreak = lineWidth;
			}

			// Overflow? (Never wrap before the first character of a line so a single
			// oversized codepoint still makes forward progress.)
			if (lineWidth + advance > maxWidth && charIndex > lineStartChar)
			{
				if (hasBreak)
				{
					// Re-flow everything after the separator onto the next line.
					finalizeLine(breakByte, breakChar, widthAtBreak);
					startNewLine(afterBreakByte, afterBreakChar);
				}
				else
				{
					// Unbreakable word: character-level break before this codepoint.
					finalizeLine(charStartByte, charIndex, lineWidth);
					startNewLine(charStartByte, charIndex);
				}
				continue;
			}

			lineWidth += advance;
			charIndex++;
		}

		// Always emit the final line — this also yields the single empty line for
		// empty text and the trailing empty line after a final newline.
		finalizeLine(text.length(), charIndex, lineWidth);

		return lines;
	}

	TextCaretLocation GetCaretLocation(const std::vector<TextWrapLine>& lines, std::size_t caretCharIndex)
	{
		if (lines.empty())
		{
			return {};
		}

		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			const TextWrapLine& line = lines[i];

			if (caretCharIndex < line.endChar)
			{
				const std::size_t column = caretCharIndex > line.startChar ? caretCharIndex - line.startChar : 0;
				return { i, column };
			}

			if (caretCharIndex == line.endChar)
			{
				const bool isLastLine = (i + 1 == lines.size());

				// A caret on a consumed separator belongs to the end of this line; on
				// a mid-word wrap boundary it belongs to the start of the next line.
				if (isLastLine || lines[i + 1].startChar > line.endChar)
				{
					return { i, line.endChar - line.startChar };
				}
			}
		}

		const std::size_t lastIndex = lines.size() - 1;
		const TextWrapLine& last = lines[lastIndex];
		return { lastIndex, last.endChar - last.startChar };
	}

	std::size_t GetCaretIndexAt(const std::vector<TextWrapLine>& lines, std::size_t line, std::size_t column)
	{
		if (lines.empty())
		{
			return 0;
		}

		const std::size_t lineIndex = std::min(line, lines.size() - 1);
		const TextWrapLine& wrapLine = lines[lineIndex];
		const std::size_t lineLength = wrapLine.endChar - wrapLine.startChar;
		return wrapLine.startChar + std::min(column, lineLength);
	}
}
