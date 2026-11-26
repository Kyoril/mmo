// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_text_tokens.h"

namespace mmo
{
	String SubstituteChatNameToken(const String& text, const String& name)
	{
		String result;
		result.reserve(text.size());

		for (size_t index = 0; index < text.size(); ++index)
		{
			if (text[index] == '$' && index + 1 < text.size() && (text[index + 1] == 'N' || text[index + 1] == 'n'))
			{
				result.append(name);
				++index;
				continue;
			}

			result.push_back(text[index]);
		}

		return result;
	}
}
