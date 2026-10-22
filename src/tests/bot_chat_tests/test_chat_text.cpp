// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_chat/anthropic_chat_model.h"
#include "bot_chat/chat_persona.h"
#include "bot_chat/chat_text.h"

#include "nlohmann/json.hpp"

#include <cctype>

using namespace mmo;

TEST_CASE("Sanitizing keeps one clean line", "[chat_text]")
{
	CHECK(SanitizeChatReply("  \"lol yeah\"  ", "Borin") == std::optional<std::string>("lol yeah"));
	CHECK(SanitizeChatReply("Borin: anyone up for a group?", "Borin") == std::optional<std::string>("anyone up for a group?"));
	CHECK(SanitizeChatReply("sure thing\nBob: cool\nBorin: ok", "Borin") == std::optional<std::string>("sure thing"));
}

TEST_CASE("Sanitizing rejects what a player would not type", "[chat_text]")
{
	CHECK_FALSE(SanitizeChatReply("SKIP", "Borin"));
	CHECK_FALSE(SanitizeChatReply("skip.", "Borin"));
	CHECK_FALSE(SanitizeChatReply("", "Borin"));
	CHECK_FALSE(SanitizeChatReply("check https://example.com", "Borin"));
	CHECK_FALSE(SanitizeChatReply("As an AI language model I cannot", "Borin"));

	// "skip" as part of a real sentence is fine.
	CHECK(SanitizeChatReply("gonna skip that quest tbh", "Borin"));
}

TEST_CASE("Sanitizing shortens long lines at a word boundary", "[chat_text]")
{
	std::string longLine;
	while (longLine.size() < 400)
	{
		longLine += "grinding wolves ";
	}

	const auto result = SanitizeChatReply(longLine, "Borin");
	REQUIRE(result);
	CHECK(result->size() <= MaxBotChatLength);
	CHECK(result->back() != ' ');
}

TEST_CASE("Humanizing applies the typing style", "[chat_text]")
{
	std::mt19937 rng(1);

	TypingStyle style;
	style.lowercase = true;
	style.dropApostrophes = true;
	style.dropTrailingPeriod = true;

	const HumanizedChat typed = HumanizeChat("I don't Know.", style, rng);
	CHECK(typed.text == "i dont know");
	CHECK_FALSE(typed.correction);
}

TEST_CASE("Humanizing makes at most one typo and can correct it", "[chat_text]")
{
	TypingStyle style;
	style.typoChancePerWord = 1.0f;
	style.correctionChance = 1.0f;

	for (uint32 seed = 0; seed < 50; ++seed)
	{
		std::mt19937 rng(seed);
		const std::string original = "looking for group wolves";
		const HumanizedChat typed = HumanizeChat(original, style, rng);

		REQUIRE(typed.correction);
		CHECK(typed.text != original);

		// The correction is the word that got mangled.
		CHECK(MentionsName(original, *typed.correction));
		CHECK_FALSE(MentionsName(typed.text, *typed.correction));
	}
}

TEST_CASE("Names are matched as whole words", "[chat_text]")
{
	CHECK(MentionsName("hey borin wanna group", "Borin"));
	CHECK(MentionsName("Borin?", "Borin"));
	CHECK_FALSE(MentionsName("borinson is afk", "Borin"));
	CHECK_FALSE(MentionsName("anything", ""));
}

TEST_CASE("Personas are stable per seed and vary across seeds", "[chat_text]")
{
	const ChatPersona a = MakeChatPersona(7);
	const ChatPersona b = MakeChatPersona(7);
	CHECK(a.temperament == b.temperament);
	CHECK(a.writingStyle == b.writingStyle);

	bool differs = false;
	for (uint32 seed = 0; seed < 20 && !differs; ++seed)
	{
		differs = MakeChatPersona(seed).temperament != a.temperament;
	}
	CHECK(differs);
}

TEST_CASE("The request body disables thinking and carries the prompt", "[anthropic]")
{
	ChatCompletionRequest request;
	request.system = "be a player";
	request.prompt = "say hi";
	request.maxOutputTokens = 42;

	const nlohmann::json body = nlohmann::json::parse(AnthropicChatModel::BuildRequestBody("claude-haiku-5-5", request));
	CHECK(body["model"] == "claude-haiku-5-5");
	CHECK(body["max_tokens"] == 42);
	CHECK(body["system"] == "be a player");
	CHECK(body["messages"][0]["role"] == "user");
	CHECK(body["messages"][0]["content"] == "say hi");
	CHECK(body["thinking"]["type"] == "disabled");
	CHECK_FALSE(body.contains("temperature"));
}

TEST_CASE("A successful response yields its text and usage", "[anthropic]")
{
	const std::string body = R"({"content":[{"type":"text","text":"lol same"}],"stop_reason":"end_turn",
		"usage":{"input_tokens":300,"output_tokens":5,"cache_read_input_tokens":0}})";

	const ChatCompletionResult result = AnthropicChatModel::ParseResponse(9, 200, body);
	CHECK(result.ticket == 9);
	CHECK(result.succeeded);
	CHECK(result.reachedModel);
	CHECK(result.text == "lol same");
	CHECK(result.inputTokens == 300);
	CHECK(result.outputTokens == 5);
}

TEST_CASE("Errors and refusals yield no text", "[anthropic]")
{
	const ChatCompletionResult rejected = AnthropicChatModel::ParseResponse(1, 429,
		R"({"type":"error","error":{"type":"rate_limit_error","message":"slow down"}})");
	CHECK_FALSE(rejected.succeeded);
	CHECK_FALSE(rejected.reachedModel);
	CHECK(rejected.error.find("slow down") != std::string::npos);

	const ChatCompletionResult refused = AnthropicChatModel::ParseResponse(2, 200,
		R"({"content":[],"stop_reason":"refusal","usage":{"input_tokens":300,"output_tokens":0}})");
	CHECK_FALSE(refused.succeeded);
	CHECK(refused.reachedModel);
	CHECK(refused.inputTokens == 300);

	const ChatCompletionResult garbage = AnthropicChatModel::ParseResponse(3, 200, "not json");
	CHECK_FALSE(garbage.succeeded);
	CHECK(garbage.reachedModel);
}

TEST_CASE("Long replies are cut after a whole sentence", "[chat_text]")
{
	const std::string reply = "Oakenshire, nice! I'm on my way, just gotta dodge the boars on the road. "
		"Mages are fun, you'll be blasting stuff in no time once you get your next few spells";

	const auto result = SanitizeChatReply(reply, "Borin");
	REQUIRE(result);
	CHECK(*result == "Oakenshire, nice! I'm on my way, just gotta dodge the boars on the road.");
}
