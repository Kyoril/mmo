// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/trade_skill_book.h"
#include "game_server/character_data.h"
#include "game_server/objects/game_player_s.h"
#include "test_unit_factory.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"

#include "asio/io_service.hpp"
#include "catch.hpp"

using namespace mmo;

TEST_CASE("Learning a trade skill starts at 1 and re-learning never resets it", "[trade_skill]")
{
	TradeSkillBook book;
	CHECK(book.LearnOrRaise(1, 75) == TradeSkillBook::LearnResult::Learned);
	REQUIRE(book.Get(1) != nullptr);
	CHECK(book.Get(1)->value == 1);
	CHECK(book.Get(1)->max == 75);

	const TradeSkillThresholds orange = NormalizeTradeSkillThresholds({ 1, 50, 60, 70 });
	for (int i = 0; i < 39; ++i)
	{
		book.TrySkillUp(1, orange, 0.99f);
	}
	CHECK(book.Get(1)->value == 40);

	CHECK(book.LearnOrRaise(1, 75) == TradeSkillBook::LearnResult::Unchanged);
	CHECK(book.Get(1)->value == 40);
	CHECK(book.LearnOrRaise(1, 50) == TradeSkillBook::LearnResult::Unchanged);
	CHECK(book.Get(1)->max == 75);
	CHECK(book.LearnOrRaise(1, 150) == TradeSkillBook::LearnResult::CapRaised);
	CHECK(book.Get(1)->max == 150);
	CHECK(book.Get(1)->value == 40);
}

TEST_CASE("Skill-up respects cap, grey and the roll", "[trade_skill]")
{
	TradeSkillBook book;
	book.SetAll({ { 1, 75, 75 } });
	const TradeSkillThresholds t = NormalizeTradeSkillThresholds({ 1, 50, 60, 100 });
	CHECK_FALSE(book.TrySkillUp(1, t, 0.0f));
	CHECK(book.Get(1)->value == 75);

	book.SetAll({ { 1, 70, 75 } });
	const TradeSkillThresholds grey = NormalizeTradeSkillThresholds({ 1, 30, 40, 50 });
	CHECK_FALSE(book.TrySkillUp(1, grey, 0.0f));

	// Value 70 is green here: chance (80 - 70) / (80 - 60) = 0.5.
	const TradeSkillThresholds green = NormalizeTradeSkillThresholds({ 1, 60, 70, 80 });
	CHECK_FALSE(book.TrySkillUp(1, green, 0.6f));
	CHECK(book.TrySkillUp(1, green, 0.4f));
	CHECK(book.Get(1)->value == 71);

	CHECK_FALSE(book.TrySkillUp(99, t, 0.0f));
}

TEST_CASE("CharacterData round-trips trade skills", "[trade_skill]")
{
	CharacterData data;
	data.tradeSkills = { { 1, 12, 75 }, { 3, 1, 75 } };

	std::vector<char> buffer;
	io::VectorSink sink(buffer);
	io::Writer writer(sink);
	writer << data;

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);
	CharacterData read;
	REQUIRE(reader >> read);
	REQUIRE(read.tradeSkills.size() == 2);
	CHECK(read.tradeSkills[0].skillId == 1);
	CHECK(read.tradeSkills[0].value == 12);
	CHECK(read.tradeSkills[1].max == 75);
}

TEST_CASE("GamePlayerS round-trips trade skills", "[trade_skill]")
{
	proto::Project project;
	asio::io_service io;
	TimerQueue timers{ io };
	auto player = test::MakeUnit(project, timers);
	player->SetTradeSkills({ { 1, 5, 75 } });

	std::vector<char> buffer;
	io::VectorSink sink(buffer);
	io::Writer writer(sink);
	writer << *player;

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);
	auto other = test::MakeUnit(project, timers);
	REQUIRE(reader >> *other);
	REQUIRE(other->GetTradeSkills().Get(1) != nullptr);
	CHECK(other->GetTradeSkills().Get(1)->value == 5);
	CHECK(other->GetTradeSkills().Get(1)->max == 75);
}
