// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "frame_ui/text_wrap.h"

using namespace mmo;

namespace
{
	// Every codepoint is 10 pixels wide unless stated otherwise.
	float fixedAdvance(uint32_t)
	{
		return 10.0f;
	}
}

// ============================================================================
// ComputeLineBreaks
// ============================================================================

TEST_CASE("ComputeLineBreaks returns a single empty line for empty text", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("", 100.0f, fixedAdvance);

	REQUIRE(lines.size() == 1);
	CHECK(lines[0].startByte == 0);
	CHECK(lines[0].endByte == 0);
	CHECK(lines[0].startChar == 0);
	CHECK(lines[0].endChar == 0);
	CHECK(lines[0].width == 0.0f);
}

TEST_CASE("ComputeLineBreaks keeps fitting text on one line", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("abc", 100.0f, fixedAdvance);

	REQUIRE(lines.size() == 1);
	CHECK(lines[0].startChar == 0);
	CHECK(lines[0].endChar == 3);
	CHECK(lines[0].startByte == 0);
	CHECK(lines[0].endByte == 3);
	CHECK(lines[0].width == 30.0f);
}

TEST_CASE("ComputeLineBreaks wraps at the last word boundary", "[text_wrap]")
{
	// "abc de" with 4 chars per line: 'd' overflows, break at the space.
	const auto lines = ComputeLineBreaks("abc de", 40.0f, fixedAdvance);

	REQUIRE(lines.size() == 2);
	CHECK(lines[0].startChar == 0);
	CHECK(lines[0].endChar == 3);      // "abc" — separator space is consumed
	CHECK(lines[0].width == 30.0f);
	CHECK(lines[1].startChar == 4);
	CHECK(lines[1].endChar == 6);      // "de"
	CHECK(lines[1].width == 20.0f);
}

TEST_CASE("ComputeLineBreaks breaks an unbreakable word at character level", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("abcdef", 30.0f, fixedAdvance);

	REQUIRE(lines.size() == 2);
	CHECK(lines[0].startChar == 0);
	CHECK(lines[0].endChar == 3);
	CHECK(lines[1].startChar == 3);
	CHECK(lines[1].endChar == 6);
}

TEST_CASE("ComputeLineBreaks honors hard newlines", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("ab\ncd", 100.0f, fixedAdvance);

	REQUIRE(lines.size() == 2);
	CHECK(lines[0].startChar == 0);
	CHECK(lines[0].endChar == 2);      // "ab" — the \n is consumed, not rendered
	CHECK(lines[0].endByte == 2);
	CHECK(lines[1].startChar == 3);
	CHECK(lines[1].endChar == 5);      // "cd"
	CHECK(lines[1].startByte == 3);
}

TEST_CASE("ComputeLineBreaks yields a trailing empty line after a final newline", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("ab\n", 100.0f, fixedAdvance);

	REQUIRE(lines.size() == 2);
	CHECK(lines[1].startChar == 3);
	CHECK(lines[1].endChar == 3);
	CHECK(lines[1].width == 0.0f);
}

TEST_CASE("ComputeLineBreaks measures tab as four spaces", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("a\tb", 100.0f, fixedAdvance);

	REQUIRE(lines.size() == 1);
	CHECK(lines[0].width == 60.0f);    // 10 + 4*10 + 10
}

TEST_CASE("ComputeLineBreaks treats tab as a break opportunity", "[text_wrap]")
{
	// a=10, b=20, tab would reach 60 > 45: wrap, tab consumed as separator.
	const auto lines = ComputeLineBreaks("ab\tcd", 45.0f, fixedAdvance);

	REQUIRE(lines.size() == 2);
	CHECK(lines[0].startChar == 0);
	CHECK(lines[0].endChar == 2);      // "ab"
	CHECK(lines[1].startChar == 3);
	CHECK(lines[1].endChar == 5);      // "cd"
}

TEST_CASE("ComputeLineBreaks keeps a single oversized codepoint per line", "[text_wrap]")
{
	// Every glyph is wider than the area: one glyph per line, always making progress.
	const auto wideAdvance = [](uint32_t) { return 50.0f; };
	const auto lines = ComputeLineBreaks("ab", 30.0f, wideAdvance);

	REQUIRE(lines.size() == 2);
	CHECK(lines[0].startChar == 0);
	CHECK(lines[0].endChar == 1);
	CHECK(lines[1].startChar == 1);
	CHECK(lines[1].endChar == 2);
}

TEST_CASE("ComputeLineBreaks tracks byte indices for multi-byte UTF-8", "[text_wrap]")
{
	// "äöü" — each character is 2 bytes. Two chars per line at width 25.
	const auto lines = ComputeLineBreaks("\xC3\xA4\xC3\xB6\xC3\xBC", 25.0f, fixedAdvance);

	REQUIRE(lines.size() == 2);
	CHECK(lines[0].startByte == 0);
	CHECK(lines[0].endByte == 4);
	CHECK(lines[0].startChar == 0);
	CHECK(lines[0].endChar == 2);
	CHECK(lines[1].startByte == 4);
	CHECK(lines[1].endByte == 6);
	CHECK(lines[1].startChar == 2);
	CHECK(lines[1].endChar == 3);
}

// ============================================================================
// Caret mapping
// ============================================================================

TEST_CASE("GetCaretLocation maps carets around a soft word wrap", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("abc de", 40.0f, fixedAdvance);

	// End of first line: after 'c' and on the consumed separator.
	CHECK(GetCaretLocation(lines, 3).line == 0);
	CHECK(GetCaretLocation(lines, 3).column == 3);
	// Start of the second line.
	CHECK(GetCaretLocation(lines, 4).line == 1);
	CHECK(GetCaretLocation(lines, 4).column == 0);
	// End of text.
	CHECK(GetCaretLocation(lines, 6).line == 1);
	CHECK(GetCaretLocation(lines, 6).column == 2);
}

TEST_CASE("GetCaretLocation puts a mid-word wrap caret at the next line start", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("abcdef", 30.0f, fixedAdvance);

	CHECK(GetCaretLocation(lines, 3).line == 1);
	CHECK(GetCaretLocation(lines, 3).column == 0);
	CHECK(GetCaretLocation(lines, 6).line == 1);
	CHECK(GetCaretLocation(lines, 6).column == 3);
}

TEST_CASE("GetCaretLocation maps carets around a hard newline", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("ab\ncd", 100.0f, fixedAdvance);

	CHECK(GetCaretLocation(lines, 2).line == 0);
	CHECK(GetCaretLocation(lines, 2).column == 2);
	CHECK(GetCaretLocation(lines, 3).line == 1);
	CHECK(GetCaretLocation(lines, 3).column == 0);
	CHECK(GetCaretLocation(lines, 5).line == 1);
	CHECK(GetCaretLocation(lines, 5).column == 2);
}

TEST_CASE("GetCaretLocation clamps out-of-range carets to the end", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("ab", 100.0f, fixedAdvance);

	CHECK(GetCaretLocation(lines, 99).line == 0);
	CHECK(GetCaretLocation(lines, 99).column == 2);
}

TEST_CASE("GetCaretIndexAt returns the caret index for a line and column", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("abc de", 40.0f, fixedAdvance);

	CHECK(GetCaretIndexAt(lines, 0, 0) == 0);
	CHECK(GetCaretIndexAt(lines, 0, 3) == 3);
	CHECK(GetCaretIndexAt(lines, 1, 0) == 4);
	CHECK(GetCaretIndexAt(lines, 1, 2) == 6);
}

TEST_CASE("GetCaretIndexAt clamps line and column", "[text_wrap]")
{
	const auto lines = ComputeLineBreaks("abc de", 40.0f, fixedAdvance);

	// Column beyond the line length clamps to the line end.
	CHECK(GetCaretIndexAt(lines, 0, 99) == 3);
	// Line beyond the last line clamps to the last line.
	CHECK(GetCaretIndexAt(lines, 99, 0) == 4);
	CHECK(GetCaretIndexAt(lines, 99, 99) == 6);
}
