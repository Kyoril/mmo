// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The pet packets of the game protocol (game/pet_packets.h): every payload survives a round trip,
// has the documented layout, and a truncated payload fails to read.

#include "catch.hpp"

#include "game/pet_defines.h"
#include "game/pet_packets.h"
#include "game/spell_target_map.h"
#include "game_protocol/game_protocol.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

#include <vector>

using namespace mmo;

namespace
{
	template<typename T>
	std::vector<char> Serialize(const T& payload)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		io::Writer writer{ sink };
		writer << payload;
		return buffer;
	}

	template<typename T>
	bool Deserialize(const std::vector<char>& buffer, T& out)
	{
		io::MemorySource source{ buffer.data(), buffer.data() + buffer.size() };
		io::Reader reader{ source };
		return static_cast<bool>(reader >> out);
	}

	template<typename T>
	bool DeserializeTruncated(const std::vector<char>& buffer, T& out)
	{
		REQUIRE(!buffer.empty());
		const std::vector<char> truncated(buffer.begin(), buffer.end() - 1);
		return Deserialize(truncated, out);
	}
}

TEST_CASE("PetCommand round-trips", "[pets][protocol]")
{
	game::PetCommandRequest request;
	request.command = pet_command::SetStrategy;
	request.target = 0x0102030405060708ull;
	request.param = pet_strategy::Passive;

	const auto buffer = Serialize(request);
	CHECK(buffer.size() == 1 + 8 + 4);

	game::PetCommandRequest read;
	REQUIRE(Deserialize(buffer, read));
	CHECK(read.command == pet_command::SetStrategy);
	CHECK(read.target == request.target);
	CHECK(read.param == static_cast<uint32>(pet_strategy::Passive));

	game::PetCommandRequest truncated;
	CHECK_FALSE(DeserializeTruncated(buffer, truncated));
}

TEST_CASE("PetCastSpell round-trips with a unit target", "[pets][protocol]")
{
	game::PetCastSpellRequest request;
	request.spellId = 4711;
	request.targetMap.SetTargetMap(spell_cast_target_flags::Unit);
	request.targetMap.SetUnitTarget(0xABCDull);

	const auto buffer = Serialize(request);

	game::PetCastSpellRequest read;
	REQUIRE(Deserialize(buffer, read));
	CHECK(read.spellId == 4711);
	CHECK(read.targetMap.GetTargetMap() == spell_cast_target_flags::Unit);
	CHECK(read.targetMap.GetUnitTarget() == 0xABCDull);
}

TEST_CASE("PetRename round-trips and carries a uint8-length name", "[pets][protocol]")
{
	game::PetRenameRequest request;
	request.name = "Shade";

	const auto buffer = Serialize(request);
	REQUIRE(buffer.size() == 1 + 5);
	CHECK(static_cast<uint8>(buffer[0]) == 5);

	game::PetRenameRequest read;
	REQUIRE(Deserialize(buffer, read));
	CHECK(read.name == "Shade");

	game::PetRenameRequest truncated;
	CHECK_FALSE(DeserializeTruncated(buffer, truncated));
}

TEST_CASE("PetNameQuery and PetNameQueryResult round-trip", "[pets][protocol]")
{
	game::PetNameQueryRequest query;
	query.guid = 0x1122334455667788ull;
	const auto queryBuffer = Serialize(query);
	CHECK(queryBuffer.size() == 8);

	game::PetNameQueryRequest readQuery;
	REQUIRE(Deserialize(queryBuffer, readQuery));
	CHECK(readQuery.guid == query.guid);

	SECTION("found")
	{
		game::PetNameQueryResultData result;
		result.guid = query.guid;
		result.found = true;
		result.name = "Grimlock";

		const auto buffer = Serialize(result);
		CHECK(buffer.size() == 8 + 1 + 1 + 8);

		game::PetNameQueryResultData read;
		REQUIRE(Deserialize(buffer, read));
		CHECK(read.guid == result.guid);
		CHECK(read.found);
		CHECK(read.name == "Grimlock");

		game::PetNameQueryResultData truncated;
		CHECK_FALSE(DeserializeTruncated(buffer, truncated));
	}

	SECTION("not found")
	{
		game::PetNameQueryResultData result;
		result.guid = query.guid;
		result.found = false;

		const auto buffer = Serialize(result);
		CHECK(buffer.size() == 8 + 1 + 1);

		game::PetNameQueryResultData read;
		read.name = "stale";
		REQUIRE(Deserialize(buffer, read));
		CHECK_FALSE(read.found);
		CHECK(read.name.empty());
	}
}

TEST_CASE("PetSpells round-trips", "[pets][protocol]")
{
	game::PetSpellsData data;
	data.petGuid = 0xF000000000000123ull;
	data.petEntry = 3;
	data.name = "Shade";
	data.levelMode = pet_level_mode::OwnLevel;
	data.strategy = pet_strategy::Passive;
	data.canRename = true;
	data.spells = { 100, 200, 300 };

	const auto buffer = Serialize(data);
	CHECK(buffer.size() == 8 + 4 + (1 + 5) + 1 + 1 + 1 + 1 + 3 * 4);

	game::PetSpellsData read;
	REQUIRE(Deserialize(buffer, read));
	CHECK(read.petGuid == data.petGuid);
	CHECK(read.petEntry == 3);
	CHECK(read.name == "Shade");
	CHECK(read.levelMode == pet_level_mode::OwnLevel);
	CHECK(read.strategy == pet_strategy::Passive);
	CHECK(read.canRename);
	CHECK(read.spells == data.spells);

	game::PetSpellsData truncated;
	CHECK_FALSE(DeserializeTruncated(buffer, truncated));
}

TEST_CASE("an empty PetSpells means there is no persistent pet", "[pets][protocol]")
{
	const game::PetSpellsData none;
	const auto buffer = Serialize(none);
	CHECK(buffer.size() == 8 + 4 + 1 + 1 + 1 + 1 + 1);

	game::PetSpellsData read;
	read.petGuid = 1;
	read.spells = { 1 };
	REQUIRE(Deserialize(buffer, read));
	CHECK(read.petGuid == 0);
	CHECK(read.spells.empty());
	CHECK(read.name.empty());
}

TEST_CASE("PetSpells writes at most 255 spells", "[pets][protocol]")
{
	game::PetSpellsData data;
	data.petGuid = 1;
	for (uint32 i = 0; i < 300; ++i)
	{
		data.spells.push_back(i + 1);
	}

	const auto buffer = Serialize(data);
	game::PetSpellsData read;
	REQUIRE(Deserialize(buffer, read));
	CHECK(read.spells.size() == 255);
	CHECK(read.spells.back() == 255);
}

TEST_CASE("PetLearnedSpell, PetXp and PetResult round-trip", "[pets][protocol]")
{
	game::PetLearnedSpellData learned;
	learned.spellId = 777;
	auto buffer = Serialize(learned);
	CHECK(buffer.size() == 4);
	game::PetLearnedSpellData readLearned;
	REQUIRE(Deserialize(buffer, readLearned));
	CHECK(readLearned.spellId == 777);

	game::PetXpData xp;
	xp.xp = 120;
	xp.nextLevelXp = 400;
	buffer = Serialize(xp);
	CHECK(buffer.size() == 8);
	game::PetXpData readXp;
	REQUIRE(Deserialize(buffer, readXp));
	CHECK(readXp.xp == 120);
	CHECK(readXp.nextLevelXp == 400);

	game::PetResultData result;
	result.result = pet_result::NamingNotAllowed;
	buffer = Serialize(result);
	CHECK(buffer.size() == 1);
	game::PetResultData readResult;
	REQUIRE(Deserialize(buffer, readResult));
	CHECK(readResult.result == pet_result::NamingNotAllowed);
}

TEST_CASE("pet opcodes are appended at the end of the opcode lists", "[pets][protocol]")
{
	using namespace game;

	CHECK(client_realm_packet::PetCommand == client_realm_packet::BuyBackItem + 1);
	CHECK(client_realm_packet::PetCastSpell == client_realm_packet::PetCommand + 1);
	CHECK(client_realm_packet::PetRename == client_realm_packet::PetCastSpell + 1);
	CHECK(client_realm_packet::PetNameQuery == client_realm_packet::PetRename + 1);
	CHECK(client_realm_packet::CheatPetAdd == client_realm_packet::PetNameQuery + 1);
	CHECK(client_realm_packet::CheatPetRemove == client_realm_packet::CheatPetAdd + 1);
	CHECK(client_realm_packet::CheatPetTame == client_realm_packet::CheatPetRemove + 1);
	CHECK(client_realm_packet::CheatPetMake == client_realm_packet::CheatPetTame + 1);
	CHECK(client_realm_packet::GuildMemberPositions == client_realm_packet::CheatPetMake + 1);

	CHECK(realm_client_packet::PetSpells == realm_client_packet::BuyBackList + 1);
	CHECK(realm_client_packet::PetLearnedSpell == realm_client_packet::PetSpells + 1);
	CHECK(realm_client_packet::PetXp == realm_client_packet::PetLearnedSpell + 1);
	CHECK(realm_client_packet::PetNameQueryResult == realm_client_packet::PetXp + 1);
	CHECK(realm_client_packet::PetResult == realm_client_packet::PetNameQueryResult + 1);
	CHECK(realm_client_packet::GuildMemberPositions == realm_client_packet::PetResult + 1);

	// Later opcodes (version 39: guild member positions) follow the pet opcodes.
	CHECK(client_realm_packet::Count_ == client_realm_packet::GuildMemberPositions + 1);
	CHECK(realm_client_packet::Count_ == realm_client_packet::GuildMemberPositions + 1);

	// The pet opcodes arrived with version 35; later bumps must not fail this test.
	CHECK(ProtocolVersion >= 0x23);
}
