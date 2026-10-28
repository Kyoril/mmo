// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_text_layout.h"

#include "frame_ui/utf8_utils.h"

namespace mmo
{
	std::vector<uint32> BuildWorldTextCodepoints(const std::string& text)
	{
		std::vector<uint32> codepoints;
		codepoints.reserve(text.length());

		for (std::size_t byteIndex = 0; byteIndex < text.length(); /* advanced while decoding */)
		{
			const std::size_t charStart = byteIndex;
			const uint32 codepoint = utf8::next_codepoint(text, byteIndex);

			if (codepoint == 0)
			{
				// Undecodable byte: it has already been stepped over, so drop it. Guard
				// against a decoder which does not advance, or this would never finish.
				if (byteIndex == charStart)
				{
					break;
				}

				continue;
			}

			codepoints.push_back(codepoint);
		}

		return codepoints;
	}
}
