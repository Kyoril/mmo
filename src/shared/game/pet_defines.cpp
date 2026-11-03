// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pet_defines.h"

namespace mmo
{
	namespace
	{
		bool isAsciiLetter(const char c)
		{
			return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		}
	}

	bool IsValidPetName(const String& name)
	{
		if (name.empty() || name.size() > MaxPetNameLength)
		{
			return false;
		}

		for (const char c : name)
		{
			if (!isAsciiLetter(c))
			{
				return false;
			}
		}

		return true;
	}

	String NormalizePetName(const String& name)
	{
		String result = name;
		for (size_t i = 0; i < result.size(); ++i)
		{
			char& c = result[i];
			if (i == 0)
			{
				if (c >= 'a' && c <= 'z')
				{
					c = static_cast<char>(c - 'a' + 'A');
				}
			}
			else if (c >= 'A' && c <= 'Z')
			{
				c = static_cast<char>(c - 'A' + 'a');
			}
		}

		return result;
	}

	String ClampStoredPetName(const String& name)
	{
		if (name.size() <= MaxStoredPetNameBytes)
		{
			return name;
		}

		// Back off while the first byte cut off is a continuation byte (10xxxxxx): the cut then
		// lies inside a multi-byte character, which is dropped whole.
		size_t length = MaxStoredPetNameBytes;
		while (length > 0 && (static_cast<uint8>(name[length]) & 0xC0) == 0x80)
		{
			--length;
		}

		return name.substr(0, length);
	}
}
