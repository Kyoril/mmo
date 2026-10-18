// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <optional>
#include <random>
#include <string>

namespace mmo
{
	/// Longest line a bot posts. Real players rarely type more in one go.
	constexpr std::size_t MaxBotChatLength = 200;

	/// Turns raw model output into a chat line, or rejects it.
	///
	/// Keeps the first line, strips quotes and a "Name:" prefix the model sometimes echoes, drops
	/// anything outside printable ASCII and shortens to MaxBotChatLength at a word boundary.
	///
	/// @param raw The model's answer.
	/// @param speakerName The bot's own name, stripped if the answer starts with it.
	/// @return Nothing if the answer is empty, the model chose to stay silent ("SKIP"), or it is
	///		something no player would type: a link, or the model talking about being a model.
	[[nodiscard]] std::optional<std::string> SanitizeChatReply(const std::string& raw, const std::string& speakerName);

	/// How sloppily a bot types.
	struct TypingStyle final
	{
		/// Writes everything in lower case.
		bool lowercase { false };

		/// Leaves off a trailing full stop.
		bool dropTrailingPeriod { true };

		/// Writes "dont" for "don't".
		bool dropApostrophes { false };

		/// Chance per word of three or more letters to get a typo.
		float typoChancePerWord { 0.0f };

		/// Chance to send a "*fixed" correction after a message that got a typo.
		float correctionChance { 0.0f };
	};

	/// Result of roughening a line up.
	struct HumanizedChat final
	{
		std::string text;

		/// The corrected word to send as "*word", if the bot decided to correct itself.
		std::optional<std::string> correction;
	};

	/// Makes a clean line look typed by a person: case, punctuation and the odd typo.
	/// At most one typo per message, so that the line stays readable.
	[[nodiscard]] HumanizedChat HumanizeChat(const std::string& text, const TypingStyle& style, std::mt19937& rng);

	/// Whether a message names a character, as a whole word and ignoring case.
	[[nodiscard]] bool MentionsName(const std::string& message, const std::string& name);
}
