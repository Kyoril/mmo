// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_text_tokens.h"

namespace mmo
{
	String SubstituteChatTextTokens(const String& text, const String& addresseeName)
	{
		if (addresseeName.empty())
		{
			return text;
		}

		String result;
		result.reserve(text.size());

		for (size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '$' && i + 1 < text.size() && (text[i + 1] == 'N' || text[i + 1] == 'n'))
			{
				result += addresseeName;
				++i;
				continue;
			}

			result += text[i];
		}

		return result;
	}
}
