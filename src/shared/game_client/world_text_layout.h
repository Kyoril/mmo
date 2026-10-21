// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <string>
#include <vector>

namespace mmo
{
	/// Splits a UTF-8 world text (a unit's name plus the optional subtitle line below it)
	/// into the glyph codepoints it is laid out from. One entry per character and never one
	/// per byte: a multi-byte character such as the french "a" with a circumflex yields the
	/// single codepoint U+00E2, which is what Font::GetGlyphData expects. Control characters
	/// keep their ASCII codepoint so the caller can still act on '\n' and '\t'.
	/// @param text The UTF-8 text to split.
	/// @return The codepoints in text order. Undecodable bytes are dropped.
	std::vector<uint32> BuildWorldTextCodepoints(const std::string& text);
}
