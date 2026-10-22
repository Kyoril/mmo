// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "chat_text.h"

#include "base/typedefs.h"

#include <string>

namespace mmo
{
	/// The kind of player a bot pretends to be. Rolled once from the bot's seed, so the same bot
	/// keeps its voice across restarts.
	struct ChatPersona final
	{
		/// Personality, as told to the model: "laid back, here to chill".
		std::string temperament;

		/// Writing habits, as told to the model: "all lowercase, lots of abbreviations".
		std::string writingStyle;

		/// What the bot does to the model's text before sending it.
		TypingStyle typing;

		/// How eager the bot is to chat at all, 0.5 (quiet) to 1.5 (chatty). Scales the chance of
		/// answering channel chat and the rate of unprompted posts.
		float chattiness { 1.0f };
	};

	[[nodiscard]] ChatPersona MakeChatPersona(uint32 seed);
}
