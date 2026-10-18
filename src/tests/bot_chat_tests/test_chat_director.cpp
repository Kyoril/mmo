// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_chat/chat_director.h"

#include <algorithm>
#include <deque>
#include <memory>

using namespace mmo;

namespace
{
	constexpr std::time_t Noon = 1792324800;
	constexpr GameTime Minute = 60 * 1000;

	/// Answers every request with a fixed line, when told to.
	class FakeModel final : public IChatModel
	{
	public:
		bool Submit(ChatCompletionRequest request) override
		{
			submitted.push_back(std::move(request));
			return true;
		}

		void Poll(std::vector<ChatCompletionResult>& outResults) override
		{
			for (const ChatCompletionRequest& request : submitted)
			{
				if (answered.count(request.ticket))
				{
					continue;
				}

				ChatCompletionResult result;
				result.ticket = request.ticket;
				result.succeeded = true;
				result.reachedModel = true;
				result.inputTokens = 300;
				result.outputTokens = 8;
				result.text = reply;
				outResults.push_back(result);
				answered.insert(request.ticket);
			}
		}

		std::size_t GetPendingCount() const override { return submitted.size() - answered.size(); }
		void Stop() override {}

		std::vector<ChatCompletionRequest> submitted;
		std::set<uint64> answered;
		std::string reply = "sure, lets go";
	};

	class FakeBot final : public IChatParticipant
	{
	public:
		explicit FakeBot(std::string name) : m_name(std::move(name)) {}

		const std::string& GetCharacterName() const override { return m_name; }
		bool IsInWorld() const override { return inWorld; }
		std::string DescribeSelf() const override { return "a level 5 human warrior in Elwynn Forest"; }
		std::vector<std::string> GetPartyMemberNames() const override { return party; }
		uint32 GetGeneralChannelId() const override { return 1; }

		void SendChat(const ChatType type, const std::string& text, const std::string& target) override
		{
			sent.push_back({ type, text, target });
		}

		struct Sent
		{
			ChatType type;
			std::string text;
			std::string target;
		};

		bool inWorld { true };
		std::vector<std::string> party;
		std::vector<Sent> sent;

	private:
		std::string m_name;
	};

	struct Fixture
	{
		explicit Fixture(ChatBudgetLimits limits = ChatBudgetLimits())
			: budget(limits, ChatPricing { 0.10, 0.50 }, "")
		{
			ChatDirectorSettings settings;
			settings.seed = 1;
			director = std::make_unique<ChatDirector>(settings, model, budget, []() { return Noon; });
			director->AddBot(0, alice, 100);
			director->AddBot(1, bob, 200);
		}

		/// Runs the director for a while in small steps, as the host frame would.
		void Run(GameTime fromMs, GameTime durationMs)
		{
			for (GameTime t = fromMs; t <= fromMs + durationMs; t += 50)
			{
				director->Update(t);
			}
		}

		FakeModel model;
		ChatBudget budget;
		FakeBot alice { "Alice" };
		FakeBot bob { "Bob" };
		std::unique_ptr<ChatDirector> director;
	};
}

TEST_CASE("A whisper from a player gets a whispered answer after a typing delay", "[chat_director]")
{
	Fixture f;
	f.director->OnChatReceived(0, ChatType::Whisper, "Steve", "hey wanna group?", 0, 1000);

	// Answered once the sender has been quiet for a moment, and sent only after "typing".
	f.Run(1000, 2000);
	CHECK(f.model.submitted.size() == 1);
	CHECK(f.alice.sent.empty());

	f.Run(3050, 30000);
	REQUIRE_FALSE(f.alice.sent.empty());
	CHECK(f.alice.sent.front().type == ChatType::Whisper);
	CHECK(f.alice.sent.front().target == "Steve");
	CHECK(f.bob.sent.empty());

	// The player's line made it into the prompt.
	CHECK(f.model.submitted.front().prompt.find("Steve: hey wanna group?") != std::string::npos);
}

TEST_CASE("Several quick whispers get one answer", "[chat_director]")
{
	Fixture f;
	f.director->OnChatReceived(0, ChatType::Whisper, "Steve", "hey", 0, 1000);
	f.director->Update(1100);
	f.director->OnChatReceived(0, ChatType::Whisper, "Steve", "u there", 0, 1200);
	f.director->Update(1300);
	f.director->OnChatReceived(0, ChatType::Whisper, "Steve", "wanna group", 0, 1400);

	f.Run(1450, 3000);
	CHECK(f.model.submitted.size() == 1);
}

TEST_CASE("Bots never answer whispers from other bots", "[chat_director]")
{
	Fixture f;
	f.director->OnChatReceived(0, ChatType::Whisper, "Bob", "hey alice", 0, 1000);
	f.Run(1000, 10000);
	CHECK(f.model.submitted.empty());
}

TEST_CASE("A player spamming a bot is answered only so often", "[chat_director]")
{
	Fixture f;
	GameTime now = 1000;
	for (int i = 0; i < 30; ++i)
	{
		f.director->OnChatReceived(0, ChatType::Whisper, "Steve", "spam " + std::to_string(i), 0, now);
		f.Run(now, 20000);
		now += 20050;
	}

	CHECK(f.model.submitted.size() <= 8 + 1);
	CHECK(f.director->GetStats().whispersThrottled > 0);
}

TEST_CASE("A channel line heard by every bot is answered by one at most", "[chat_director]")
{
	Fixture f;
	uint32 submittedTotal = 0;

	for (int round = 0; round < 40; ++round)
	{
		const GameTime start = 1000 + round * 10 * Minute;
		const std::string line = "Alice Bob anyone know where the wolves are? " + std::to_string(round);
		f.director->OnChatReceived(0, ChatType::Channel, "Steve", line, 1, start);
		f.director->OnChatReceived(1, ChatType::Channel, "Steve", line, 1, start);

		const std::size_t before = f.model.submitted.size();
		f.Run(start, 1000);

		// Unprompted posts may land in the same second; only answers to this line count.
		std::size_t answered = 0;
		for (std::size_t i = before; i < f.model.submitted.size(); ++i)
		{
			const std::string& prompt = f.model.submitted[i].prompt;
			const bool isAnswer = prompt.rfind("Chat:", 0) == 0 && prompt.find(line) != std::string::npos;
			answered += isAnswer ? 1 : 0;
		}
		CHECK(answered <= 1);
		submittedTotal += static_cast<uint32>(answered);
	}

	// Naming the bots makes them very likely to answer.
	CHECK(submittedTotal > 20);
}

TEST_CASE("Nothing is requested once the budget is spent", "[chat_director]")
{
	ChatBudgetLimits limits;
	limits.dailyUsd = 0.0;
	Fixture f(limits);

	f.director->OnChatReceived(0, ChatType::Whisper, "Steve", "hello?", 0, 1000);
	f.Run(1000, 10000);

	CHECK(f.model.submitted.empty());
	CHECK(f.director->GetStats().budgetDenied > 0);
	CHECK(f.alice.sent.empty());
}

TEST_CASE("Spend is settled to the real cost", "[chat_director]")
{
	Fixture f;
	f.director->OnChatReceived(0, ChatType::Whisper, "Steve", "hey", 0, 1000);
	f.Run(1000, 5000);

	REQUIRE(f.model.submitted.size() == 1);
	CHECK(f.budget.GetSpentToday() == Approx(f.budget.GetPricing().CostUsd(300, 8)));
}

TEST_CASE("Bots stay quiet unprompted while no real player is around", "[chat_director]")
{
	Fixture f;
	f.Run(0, 200 * Minute);
	CHECK(f.model.submitted.empty());

	SECTION("and start talking once one shows up")
	{
		GameTime now = 200 * Minute;
		for (int i = 0; i < 30; ++i)
		{
			f.director->NoteHumanSeen(now);
			f.Run(now, 10 * Minute);
			now += 10 * Minute;
		}

		CHECK_FALSE(f.model.submitted.empty());
	}
}

TEST_CASE("The model choosing silence sends nothing", "[chat_director]")
{
	Fixture f;
	f.model.reply = "SKIP";
	f.director->OnChatReceived(0, ChatType::Whisper, "Steve", "k", 0, 1000);
	f.Run(1000, 30000);

	CHECK(f.model.submitted.size() == 1);
	CHECK(f.alice.sent.empty());
	CHECK(f.director->GetStats().repliesSkipped == 1);
}

TEST_CASE("A bot that went offline drops its pending line", "[chat_director]")
{
	Fixture f;
	f.director->OnChatReceived(0, ChatType::Whisper, "Steve", "hey", 0, 1000);
	f.Run(1000, 2000);
	REQUIRE(f.model.submitted.size() == 1);

	f.alice.inWorld = false;
	f.Run(3050, 30000);
	CHECK(f.alice.sent.empty());
	CHECK(f.director->GetStats().linesDropped >= 1);
}

TEST_CASE("Lines stamped later than the frame still wait for every listener", "[chat_director]")
{
	// The host stamps chat with the time it was read off the socket, which is later than the
	// frame start it updates the director with. That must not make a decision resolve at once,
	// or a second bot hearing the same line a moment later would answer it again.
	Fixture f;
	std::size_t worstRound = 0;

	for (int round = 0; round < 30; ++round)
	{
		const GameTime frame = 1000 + round * 10 * Minute;
		const std::string line = "Alice Bob where do i train? " + std::to_string(round);
		const std::size_t before = f.model.submitted.size();

		f.director->OnChatReceived(0, ChatType::Channel, "Steve", line, 1, frame + 20);
		f.director->Update(frame);
		f.director->OnChatReceived(1, ChatType::Channel, "Steve", line, 1, frame + 70);
		f.Run(frame + 50, 1000);

		std::size_t answers = 0;
		for (std::size_t i = before; i < f.model.submitted.size(); ++i)
		{
			const std::string& prompt = f.model.submitted[i].prompt;
			answers += (prompt.rfind("Chat:", 0) == 0 && prompt.find(line) != std::string::npos) ? 1 : 0;
		}
		worstRound = std::max(worstRound, answers);
	}

	CHECK(worstRound <= 1);
}
