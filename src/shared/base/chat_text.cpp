// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_text.h"

#include <cstdint>
#include <vector>

namespace mmo
{
	namespace
	{
		constexpr uint32_t invalidCodepoint = 0xFFFFFFFF;

		/// Decodes one strictly valid UTF-8 sequence at @p pos and advances @p pos past it. On a
		/// malformed sequence only the first byte is consumed and invalidCodepoint is returned.
		uint32_t decodeStrict(const std::string& text, std::size_t& pos)
		{
			const auto lead = static_cast<unsigned char>(text[pos++]);
			if (lead < 0x80)
			{
				return lead;
			}

			std::size_t continuationCount;
			uint32_t codepoint;
			uint32_t minimum;
			if ((lead & 0xE0) == 0xC0)
			{
				continuationCount = 1;
				codepoint = lead & 0x1F;
				minimum = 0x80;
			}
			else if ((lead & 0xF0) == 0xE0)
			{
				continuationCount = 2;
				codepoint = lead & 0x0F;
				minimum = 0x800;
			}
			else if ((lead & 0xF8) == 0xF0)
			{
				continuationCount = 3;
				codepoint = lead & 0x07;
				minimum = 0x10000;
			}
			else
			{
				// Stray continuation byte or an invalid lead byte (0xF8 and above)
				return invalidCodepoint;
			}

			if (pos + continuationCount > text.size())
			{
				return invalidCodepoint;
			}

			for (std::size_t i = 0; i < continuationCount; ++i)
			{
				const auto c = static_cast<unsigned char>(text[pos + i]);
				if ((c & 0xC0) != 0x80)
				{
					return invalidCodepoint;
				}

				codepoint = (codepoint << 6) | (c & 0x3F);
			}

			// Overlong encodings, UTF-16 surrogates and values beyond Unicode are not valid UTF-8
			if (codepoint < minimum || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF)
			{
				return invalidCodepoint;
			}

			pos += continuationCount;
			return codepoint;
		}

		void appendUtf8(std::string& out, const uint32_t codepoint)
		{
			if (codepoint < 0x80)
			{
				out.push_back(static_cast<char>(codepoint));
			}
			else if (codepoint < 0x800)
			{
				out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
				out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			}
			else if (codepoint < 0x10000)
			{
				out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
				out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			}
			else
			{
				out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
				out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			}
		}

		/// Characters that must never be part of a chat message.
		bool isForbidden(const uint32_t codepoint)
		{
			// C0 controls, DEL and C1 controls
			if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F))
			{
				return true;
			}

			// Line and paragraph separators: chat messages are a single line
			if (codepoint == 0x2028 || codepoint == 0x2029)
			{
				return true;
			}

			// Bidi embeddings, overrides and isolates: they reorder the text following them
			if ((codepoint >= 0x202A && codepoint <= 0x202E) || (codepoint >= 0x2066 && codepoint <= 0x2069))
			{
				return true;
			}

			// Interlinear annotation controls and the object replacement character
			if (codepoint >= 0xFFF9 && codepoint <= 0xFFFC)
			{
				return true;
			}

			// Noncharacters
			if ((codepoint >= 0xFDD0 && codepoint <= 0xFDEF) || (codepoint & 0xFFFE) == 0xFFFE)
			{
				return true;
			}

			return false;
		}

		/// Whitespace and characters that render as nothing. A message made only of these is empty.
		bool isBlank(const uint32_t codepoint)
		{
			switch (codepoint)
			{
			case 0x0020:	// space
			case 0x00A0:	// no-break space
			case 0x00AD:	// soft hyphen
			case 0x034F:	// combining grapheme joiner
			case 0x061C:	// Arabic letter mark
			case 0x115F:	// Hangul choseong filler
			case 0x1160:	// Hangul jungseong filler
			case 0x1680:	// Ogham space mark
			case 0x17B4:	// Khmer vowel inherent aq
			case 0x17B5:	// Khmer vowel inherent aa
			case 0x180E:	// Mongolian vowel separator
			case 0x202F:	// narrow no-break space
			case 0x205F:	// medium mathematical space
			case 0x2800:	// Braille pattern blank
			case 0x3000:	// ideographic space
			case 0x3164:	// Hangul filler
			case 0xFEFF:	// zero width no-break space / byte order mark
			case 0xFFA0:	// halfwidth Hangul filler
				return true;
			default:
				break;
			}

			// En quad .. hair space, zero width space/joiners, LRM/RLM, word joiner .. invisible plus
			return (codepoint >= 0x2000 && codepoint <= 0x200F) || (codepoint >= 0x2060 && codepoint <= 0x2064);
		}
	}

	std::string SanitizeChatText(const std::string& text, const std::size_t maxLength)
	{
		std::vector<uint32_t> codepoints;
		codepoints.reserve(text.size());

		for (std::size_t pos = 0; pos < text.size(); )
		{
			uint32_t codepoint = decodeStrict(text, pos);
			if (codepoint == '\t')
			{
				codepoint = ' ';
			}

			if (codepoint == invalidCodepoint || isForbidden(codepoint))
			{
				continue;
			}

			codepoints.push_back(codepoint);
		}

		std::size_t begin = 0;
		while (begin < codepoints.size() && isBlank(codepoints[begin]))
		{
			++begin;
		}

		std::size_t end = codepoints.size();
		if (end - begin > maxLength)
		{
			end = begin + maxLength;
		}

		while (end > begin && isBlank(codepoints[end - 1]))
		{
			--end;
		}

		std::string result;
		result.reserve(text.size());
		for (std::size_t i = begin; i < end; ++i)
		{
			appendUtf8(result, codepoints[i]);
		}

		return result;
	}
}
