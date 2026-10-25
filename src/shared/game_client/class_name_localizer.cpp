// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "class_name_localizer.h"

#include <cctype>

namespace mmo
{
	std::string GetClassNameLocalizationKey(const std::string& className)
	{
		if (className.empty())
		{
			return std::string();
		}

		std::string key = "CLASS_";
		key.reserve(key.size() + className.size());

		for (const char character : className)
		{
			// Keys never contain spaces or dashes, those become underscores.
			if (character == ' ' || character == '-')
			{
				key.push_back('_');
			}
			else
			{
				key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
			}
		}

		return key;
	}

	const std::string& LocalizeClassName(const std::string& className, const LocalizationLookup& lookup)
	{
		if (className.empty() || !lookup)
		{
			return className;
		}

		// A locale which does not define the key (or leaves it empty) keeps the english name.
		const std::string* translation = lookup(GetClassNameLocalizationKey(className));
		if (translation && !translation->empty())
		{
			return *translation;
		}

		return className;
	}
}
