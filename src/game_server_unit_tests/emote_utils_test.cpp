// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "game_server/emote_utils.h"
#include "game_server/character_data.h"
#include "shared/proto_data/emotes.pb.h"

#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"

using namespace mmo;

namespace
{
	proto::EmoteEntry makeWaveEmote()
	{
		proto::EmoteEntry entry;
		entry.set_id(1);
		entry.set_name("Wave");
		entry.set_textnotarget("%s waves.");
		entry.set_texttarget("%s waves at %t.");
		entry.set_textself("%s waves at %s.");
		return entry;
	}
}

TEST_CASE("ComposeEmoteText - no-target template is used without a target", "[emote_utils]")
{
	const proto::EmoteEntry emote = makeWaveEmote();
	REQUIRE(ComposeEmoteText(emote, LocaleIndex::enUS, "Bob", "", false, false) == "Bob waves.");
}

TEST_CASE("ComposeEmoteText - target template substitutes both names", "[emote_utils]")
{
	const proto::EmoteEntry emote = makeWaveEmote();
	REQUIRE(ComposeEmoteText(emote, LocaleIndex::enUS, "Bob", "Alice", true, false) == "Bob waves at Alice.");
}

TEST_CASE("ComposeEmoteText - self template wins when the emote targets the performer", "[emote_utils]")
{
	const proto::EmoteEntry emote = makeWaveEmote();
	REQUIRE(ComposeEmoteText(emote, LocaleIndex::enUS, "Bob", "Bob", false, true) == "Bob waves at Bob.");
}

TEST_CASE("ComposeEmoteText - missing target template falls back to the no-target template", "[emote_utils]")
{
	proto::EmoteEntry emote = makeWaveEmote();
	emote.clear_texttarget();
	REQUIRE(ComposeEmoteText(emote, LocaleIndex::enUS, "Bob", "Alice", true, false) == "Bob waves.");
}

TEST_CASE("ComposeEmoteText - emotes without any template are silent", "[emote_utils]")
{
	proto::EmoteEntry emote;
	emote.set_id(2);
	emote.set_name("Silent");
	REQUIRE(ComposeEmoteText(emote, LocaleIndex::enUS, "Bob", "", false, false).empty());
}

TEST_CASE("ComposeEmoteText - per-locale override is preferred for non-enUS locales", "[emote_utils]")
{
	proto::EmoteEntry emote = makeWaveEmote();
	auto* loc = emote.add_textnotarget_loc();
	loc->set_locale(static_cast<uint32>(LocaleIndex::deDE));
	loc->set_value("%s winkt.");

	REQUIRE(ComposeEmoteText(emote, LocaleIndex::deDE, "Bob", "", false, false) == "Bob winkt.");

	// Other locales without an override fall back to the base string.
	REQUIRE(ComposeEmoteText(emote, LocaleIndex::frFR, "Bob", "", false, false) == "Bob waves.");
}

TEST_CASE("SelectNextPoseVariant - cycles through variants and wraps to the default pose", "[emote_utils]")
{
	const std::vector<std::pair<uint32, uint32>> variants = { {0, 10}, {1, 20}, {2, 30} };

	REQUIRE(SelectNextPoseVariant(variants, 0) == 10);
	REQUIRE(SelectNextPoseVariant(variants, 10) == 20);
	REQUIRE(SelectNextPoseVariant(variants, 20) == 30);
	REQUIRE(SelectNextPoseVariant(variants, 30) == 0);
}

TEST_CASE("SelectNextPoseVariant - empty variant list is a no-op", "[emote_utils]")
{
	REQUIRE(SelectNextPoseVariant({}, 0) == 0);
	REQUIRE(SelectNextPoseVariant({}, 42) == 0);
}

TEST_CASE("SelectNextPoseVariant - stale selection resets to the default pose", "[emote_utils]")
{
	const std::vector<std::pair<uint32, uint32>> variants = { {0, 10}, {1, 20} };
	REQUIRE(SelectNextPoseVariant(variants, 999) == 0);
}

TEST_CASE("CharacterData - emote fields round-trip through serialization", "[emote_utils]")
{
	CharacterData source;
	source.characterId = 1234;
	source.name = "Bob";
	source.emoteIds = { 5, 7, 42 };
	source.moodEmote = 3;
	source.idlePoseEmote = 11;
	source.sitPoseEmote = 12;
	source.sleepPoseEmote = 13;

	std::vector<char> buffer;
	io::VectorSink sink(buffer);
	io::Writer writer(sink);
	writer << source;

	io::MemorySource memorySource(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(memorySource);

	CharacterData loaded;
	REQUIRE(reader >> loaded);

	REQUIRE(loaded.characterId == source.characterId);
	REQUIRE(loaded.name == source.name);
	REQUIRE(loaded.emoteIds == source.emoteIds);
	REQUIRE(loaded.moodEmote == source.moodEmote);
	REQUIRE(loaded.idlePoseEmote == source.idlePoseEmote);
	REQUIRE(loaded.sitPoseEmote == source.sitPoseEmote);
	REQUIRE(loaded.sleepPoseEmote == source.sleepPoseEmote);
}
