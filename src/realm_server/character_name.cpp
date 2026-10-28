// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "character_name.h"

namespace mmo
{
	namespace
	{
		bool isAsciiLetter(const unsigned char c)
		{
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
		}

		/// German letters are all two-byte sequences starting with 0xC3. Only these are
		/// allowed: the characters.name column is latin1, which holds them, and they cannot be
		/// combined into look-alikes of other names.
		bool isGermanLetterTrailByte(const unsigned char c)
		{
			switch (c)
			{
			case 0x84:	// Ä
			case 0x96:	// Ö
			case 0x9C:	// Ü
			case 0x9F:	// ß
			case 0xA4:	// ä
			case 0xB6:	// ö
			case 0xBC:	// ü
				return true;
			default:
				return false;
			}
		}
	}

	bool IsValidCharacterName(const std::string& name)
	{
		std::size_t characterCount = 0;

		for (std::size_t i = 0; i < name.size(); ++characterCount)
		{
			const auto c = static_cast<unsigned char>(name[i]);
			if (isAsciiLetter(c))
			{
				++i;
				continue;
			}

			if (c == 0xC3 && i + 1 < name.size() && isGermanLetterTrailByte(static_cast<unsigned char>(name[i + 1])))
			{
				i += 2;
				continue;
			}

			return false;
		}

		return characterCount >= MinCharacterNameLength && characterCount <= MaxCharacterNameLength;
	}
}
