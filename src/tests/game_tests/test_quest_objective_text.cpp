// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// An objective which is not completed by killing its creature needs the author's own wording
// ("Sows returned" instead of "Sow slain"). QuestRequirement::text carries it (quests.proto),
// so QuestInfo has to hand it to the client along with the entry and the count.

#include "catch.hpp"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "game/quest_info.h"

using namespace mmo;

namespace
{
	QuestInfo RoundTrip(const QuestInfo& quest)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << quest;
		sink.Flush();

		io::MemorySource source{ buffer };
		io::Reader reader{ source };

		QuestInfo result;
		REQUIRE(reader >> result);
		return result;
	}
}

TEST_CASE("Quest objectives carry the author's custom objective text", "[quest_info]")
{
	QuestInfo quest;
	quest.id = 82;
	quest.title = "Pigs in the Corn";

	// The sows are led back to the farm, not killed.
	quest.requiredCreatures.emplace_back(134u, 4u, "Sows returned to Hesper");
	quest.requiredObjects.emplace_back(7u, 2u, "Scarecrows righted");
	quest.requiredItems.emplace_back(9u, 3u, "Turnips gathered");

	const QuestInfo read = RoundTrip(quest);

	REQUIRE(read.requiredCreatures.size() == 1);
	CHECK(read.requiredCreatures[0].creatureId == 134u);
	CHECK(read.requiredCreatures[0].count == 4u);
	CHECK(read.requiredCreatures[0].text == "Sows returned to Hesper");

	REQUIRE(read.requiredObjects.size() == 1);
	CHECK(read.requiredObjects[0].text == "Scarecrows righted");

	REQUIRE(read.requiredItems.size() == 1);
	CHECK(read.requiredItems[0].text == "Turnips gathered");
}

TEST_CASE("Quest objectives without custom text round trip unchanged", "[quest_info]")
{
	QuestInfo quest;
	quest.id = 83;
	quest.requiredCreatures.emplace_back(130u, 6u, "");

	const QuestInfo read = RoundTrip(quest);

	REQUIRE(read.requiredCreatures.size() == 1);
	CHECK(read.requiredCreatures[0].creatureId == 130u);
	CHECK(read.requiredCreatures[0].count == 6u);
	CHECK(read.requiredCreatures[0].text.empty());
}
