// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/chat_text_tokens.h"

using namespace mmo;

TEST_CASE("SubstituteChatNameToken - the name token is replaced by the subject name", "[chat_text_tokens]")
{
	// The soul binder's bind trigger text, verbatim from the shipped trigger data.
	const String text = "Done. Walk whichever road you like, $N - this stone knows your name now.";

	REQUIRE(SubstituteChatNameToken(text, "Kyo") == "Done. Walk whichever road you like, Kyo - this stone knows your name now.");
}

TEST_CASE("SubstituteChatNameToken - the lower case name token is replaced as well", "[chat_text_tokens]")
{
	REQUIRE(SubstituteChatNameToken("Hello $n!", "Kyo") == "Hello Kyo!");
}

TEST_CASE("SubstituteChatNameToken - every occurrence is replaced", "[chat_text_tokens]")
{
	REQUIRE(SubstituteChatNameToken("$N, oh $N.", "Kyo") == "Kyo, oh Kyo.");
}

TEST_CASE("SubstituteChatNameToken - an unknown subject removes the token instead of showing it", "[chat_text_tokens]")
{
	REQUIRE(SubstituteChatNameToken("Hello $N!", "") == "Hello !");
}

TEST_CASE("SubstituteChatNameToken - other dollar sequences are left untouched", "[chat_text_tokens]")
{
	REQUIRE(SubstituteChatNameToken("Costs $5, $B and a trailing $", "Kyo") == "Costs $5, $B and a trailing $");
}

TEST_CASE("SubstituteChatNameToken - text without a token is returned unchanged", "[chat_text_tokens]")
{
	REQUIRE(SubstituteChatNameToken("Nothing to replace here.", "Kyo") == "Nothing to replace here.");
}
