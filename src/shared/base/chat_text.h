// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cstddef>
#include <string>

namespace mmo
{
	/// Maximum number of characters (code points, not bytes) of a chat message. Matches the
	/// varchar(255) message column of the realm's character_chat table.
	constexpr std::size_t MaxChatMessageLength = 255;

	/// @brief Cleans player-typed chat text before it is sent, broadcast or stored.
	///
	/// Any language is kept (umlauts, accents, Cyrillic, CJK, ...). Removed are: malformed
	/// UTF-8 (truncated or overlong sequences, surrogates, values above U+10FFFF), control
	/// characters (a tab becomes a space), noncharacters and bidi embedding/override/isolate
	/// controls, which can flip the text of everything after them. Whitespace and invisible
	/// characters (zero-width spaces, Hangul fillers, soft hyphens, ...) are trimmed from both
	/// ends, and the result is cut to @p maxLength characters.
	///
	/// @param text Chat text as received, possibly not even valid UTF-8.
	/// @param maxLength Maximum number of characters of the result.
	/// @returns Valid UTF-8 text, empty if @p text holds nothing visible.
	std::string SanitizeChatText(const std::string& text, std::size_t maxLength = MaxChatMessageLength);

	/// @brief Whether a whisper is addressed to the sending character itself.
	///
	/// A private message to the own character is refused, like a group invite or a mail
	/// addressed to the own character. Names are compared ignoring ASCII letter case, so a
	/// differently typed own name is recognized as well.
	///
	/// @param senderName Name of the character sending the whisper.
	/// @param targetName Name the whisper is addressed to.
	/// @returns true if both names denote the same character; an empty name is never self.
	bool IsWhisperToSelf(const std::string& senderName, const std::string& targetName);
}
