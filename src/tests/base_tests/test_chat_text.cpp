// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/chat_text.h"

using namespace mmo;

TEST_CASE("Chat text keeps plain ASCII", "[chat_text]")
{
	CHECK(SanitizeChatText("Hello world!") == "Hello world!");
	CHECK(SanitizeChatText("a  b") == "a  b");
}

TEST_CASE("Chat text keeps German, French and Russian", "[chat_text]")
{
	const std::string german = "Gr\xC3\xBC\xC3\x9F" "e, sch\xC3\xB6n!";		// Grüße, schön!
	const std::string french = "\xC3\x87" "a va tr\xC3\xA8s bien";			// Ça va très bien
	const std::string russian = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";	// Привет
	const std::string japanese = "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF";	// こんにちは
	const std::string emoji = "gg \xF0\x9F\x98\x80";	// U+1F600

	CHECK(SanitizeChatText(german) == german);
	CHECK(SanitizeChatText(french) == french);
	CHECK(SanitizeChatText(russian) == russian);
	CHECK(SanitizeChatText(japanese) == japanese);
	CHECK(SanitizeChatText(emoji) == emoji);
}

TEST_CASE("Chat text with nothing visible is empty", "[chat_text]")
{
	CHECK(SanitizeChatText("").empty());
	CHECK(SanitizeChatText("   ").empty());
	CHECK(SanitizeChatText("\t \t").empty());
	CHECK(SanitizeChatText("\xE2\x80\x8B").empty());				// zero width space
	CHECK(SanitizeChatText("\xC2\xA0\xE3\x80\x80").empty());		// no-break + ideographic space
	CHECK(SanitizeChatText("\xE3\x85\xA4\xEF\xBB\xBF").empty());	// Hangul filler + BOM
	CHECK(SanitizeChatText("\x01\x1B\x7F").empty());				// control characters
}

TEST_CASE("Chat text drops orphaned UTF-8 bytes", "[chat_text]")
{
	// What a backspace after 'ü' used to leave behind in the text field
	CHECK(SanitizeChatText("\xBC").empty());
	CHECK(SanitizeChatText("\xC3").empty());
	CHECK(SanitizeChatText("ab\xBC" "c") == "abc");
	CHECK(SanitizeChatText("ab\xC3") == "ab");
	CHECK(SanitizeChatText("\xC3" "a") == "a");
}

TEST_CASE("Chat text drops malformed UTF-8", "[chat_text]")
{
	CHECK(SanitizeChatText("a\xC0\xAF" "b") == "ab");					// overlong '/'
	CHECK(SanitizeChatText("a\xE0\x80\xAF" "b") == "ab");				// overlong '/'
	CHECK(SanitizeChatText("a\xED\xA0\x80" "b") == "ab");				// encoded surrogate
	CHECK(SanitizeChatText("a\xF4\x90\x80\x80" "b") == "ab");			// beyond U+10FFFF
	CHECK(SanitizeChatText("a\xFF\xFE" "b") == "ab");					// never valid
	CHECK(SanitizeChatText("a\xE2\x82" "b") == "ab");					// truncated
}

TEST_CASE("Chat text drops controls and bidi overrides", "[chat_text]")
{
	CHECK(SanitizeChatText("a\nb\rc") == "abc");
	CHECK(SanitizeChatText("a\tb") == "a b");
	CHECK(SanitizeChatText("a\xC2\x85" "b") == "ab");					// C1 next line
	CHECK(SanitizeChatText("a\xE2\x80\xAE" "b") == "ab");				// right-to-left override
	CHECK(SanitizeChatText("a\xE2\x81\xA6" "b") == "ab");				// left-to-right isolate
	CHECK(SanitizeChatText("a\xE2\x80\xA8" "b") == "ab");				// line separator
	CHECK(SanitizeChatText("a\xEF\xBF\xBF" "b") == "ab");				// noncharacter U+FFFF
}

TEST_CASE("Chat text trims blanks at both ends only", "[chat_text]")
{
	CHECK(SanitizeChatText("  hi  ") == "hi");
	CHECK(SanitizeChatText("\xE2\x80\x8Bhi\xE2\x80\x8B") == "hi");
	CHECK(SanitizeChatText("h\xE2\x80\x8Bi") == "h\xE2\x80\x8Bi");
}

TEST_CASE("Chat text length is limited in characters, not bytes", "[chat_text]")
{
	std::string umlauts;
	for (int i = 0; i < 300; ++i)
	{
		umlauts += "\xC3\xBC";
	}

	const std::string limited = SanitizeChatText(umlauts);
	CHECK(limited.size() == MaxChatMessageLength * 2);

	CHECK(SanitizeChatText("abcdef", 3) == "abc");
	CHECK(SanitizeChatText("ab cd", 3) == "ab");
}

TEST_CASE("A whisper addressed to the own character is recognized", "[chat_text]")
{
	// The realm refuses a private message to the sending character itself.
	CHECK(IsWhisperToSelf("Ruiad", "Ruiad"));
	CHECK(IsWhisperToSelf("Ruiad", "ruiad"));
	CHECK(IsWhisperToSelf("Ruiad", "RUIAD"));

	// Anyone else is a valid whisper target.
	CHECK_FALSE(IsWhisperToSelf("Ruiad", "Ruiada"));
	CHECK_FALSE(IsWhisperToSelf("Ruiad", "Rui"));
	CHECK_FALSE(IsWhisperToSelf("Ruiad", "Kyoril"));

	// A missing name on either side is never the own character.
	CHECK_FALSE(IsWhisperToSelf("", ""));
	CHECK_FALSE(IsWhisperToSelf("Ruiad", ""));
	CHECK_FALSE(IsWhisperToSelf("", "Ruiad"));

	// Non-ASCII letters are compared as they are: Juergen with an umlaut stays itself.
	CHECK(IsWhisperToSelf("J\xC3\xBCrgen", "j\xC3\xBCrgen"));
	CHECK_FALSE(IsWhisperToSelf("J\xC3\xBCrgen", "Jurgen"));
}
