// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_persona.h"

#include <iterator>
#include <random>

namespace mmo
{
	namespace
	{
		const char* const temperaments[] = {
			"laid back, mostly here to chill after work",
			"competitive, wants to hit max level fast and min-max everything",
			"fairly new to MMOs, curious and asks basic questions sometimes",
			"a joker who likes dumb puns and banter",
			"a veteran of many MMOs, a bit jaded but helpful when asked",
			"slightly grumpy, complains about drop rates and respawn times",
			"friendly and social, likes grouping up with people",
			"quiet, answers briefly and gets back to grinding",
			"an explorer who cares more about the world and lore than levels",
			"a completionist who wants every quest and every item",
		};

		struct WritingStyle
		{
			const char* description;
			bool lowercase;
			bool dropApostrophes;
			float typoChance;
		};

		const WritingStyle writingStyles[] = {
			{ "all lowercase, lots of abbreviations like u, ur, lol, ty, np, idk", true, true, 0.06f },
			{ "mostly lowercase, skips punctuation, uses lol and xd", true, false, 0.05f },
			{ "normal capitalization but short and to the point", false, false, 0.02f },
			{ "lowercase, types fast and sloppy, sometimes drops small words", true, true, 0.09f },
			{ "proper sentences most of the time, occasional :) or :D", false, false, 0.015f },
			{ "terse, few words, rarely more than five", true, false, 0.03f },
		};
	}

	ChatPersona MakeChatPersona(const uint32 seed)
	{
		// Its own stream, so that adding a field here does not shift any other bot randomness.
		std::mt19937 rng(seed ^ 0x6A09E667u);

		ChatPersona persona;
		persona.temperament = temperaments[std::uniform_int_distribution<std::size_t>(0, std::size(temperaments) - 1)(rng)];

		const WritingStyle& style = writingStyles[std::uniform_int_distribution<std::size_t>(0, std::size(writingStyles) - 1)(rng)];
		persona.writingStyle = style.description;
		persona.typing.lowercase = style.lowercase;
		persona.typing.dropApostrophes = style.dropApostrophes;
		persona.typing.dropTrailingPeriod = true;
		persona.typing.typoChancePerWord = style.typoChance;
		persona.typing.correctionChance = 0.25f;
		persona.chattiness = std::uniform_real_distribution<float>(0.5f, 1.5f)(rng);
		return persona;
	}
}
