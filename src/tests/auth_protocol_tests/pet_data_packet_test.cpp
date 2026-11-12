// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The realm <-> world pet packets: the stored pets of a character (PlayerPetData on join,
// PetData on save) and the creation of a new stored pet (CreatePet, answered by PetCreated).

#include "catch.hpp"

#include "auth_protocol/auth_protocol.h"
#include "auth_protocol/auth_incoming_packet.h"
#include "auth_protocol/auth_outgoing_packet.h"
#include "binary_io/container_source.h"
#include "binary_io/vector_sink.h"
#include "game_server/pets/persistent_pet.h"

using namespace mmo;

namespace
{
	constexpr uint64 CharacterGuid = 0x0000000100000042ull;

	PersistentPetData MakePet(const uint64 dbId, const uint32 petEntry, const String& name, std::vector<uint32> spells)
	{
		PersistentPetData pet;
		pet.dbId = dbId;
		pet.classId = 5;
		pet.petEntry = petEntry;
		pet.unitEntry = petEntry * 10;
		pet.name = name;
		pet.level = 7;
		pet.xp = 123;
		pet.health = 456;
		pet.power = 78;
		pet.isDead = dbId % 2 == 0;
		pet.wasSummoned = dbId % 2 == 1;
		pet.spells = std::move(spells);
		return pet;
	}

	void CheckSamePet(const PersistentPetData& read, const PersistentPetData& written)
	{
		CHECK(read.dbId == written.dbId);
		CHECK(read.classId == written.classId);
		CHECK(read.petEntry == written.petEntry);
		CHECK(read.unitEntry == written.unitEntry);
		CHECK(read.name == written.name);
		CHECK(read.level == written.level);
		CHECK(read.xp == written.xp);
		CHECK(read.health == written.health);
		CHECK(read.power == written.power);
		CHECK(read.isDead == written.isDead);
		CHECK(read.wasSummoned == written.wasSummoned);
		CHECK(read.spells == written.spells);
		CHECK(read == written);
	}

	/// Writes a pet list packet (PlayerPetData or PetData) into a buffer.
	std::vector<char> WritePetListPacket(const uint8 opCode, const std::vector<PersistentPetData>& pets)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		auth::OutgoingPacket packet{ sink };
		packet.Start(opCode);
		packet << io::write<uint64>(CharacterGuid);
		WritePersistentPets(packet, pets);
		packet.Finish();
		sink.Flush();
		return buffer;
	}

	/// Writes a single pet packet (CreatePet or PetCreated) into a buffer.
	std::vector<char> WritePetPacket(const uint8 opCode, const PersistentPetData& pet)
	{
		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		auth::OutgoingPacket packet{ sink };
		packet.Start(opCode);
		packet << io::write<uint64>(CharacterGuid) << pet;
		packet.Finish();
		sink.Flush();
		return buffer;
	}

	void CheckPetListRoundTrip(const uint8 opCode)
	{
		const std::vector<PersistentPetData> pets = {
			MakePet(1, 3, "Fido", { 10, 20 }),
			MakePet(2, 4, "Shade", { 30 }),
		};

		const std::vector<char> buffer = WritePetListPacket(opCode, pets);

		io::MemorySource source{ buffer };
		auth::IncomingPacket packet;
		REQUIRE(auth::IncomingPacket::Start(packet, source) == ReceiveState::Complete);
		CHECK(packet.GetId() == opCode);

		uint64 characterGuid = 0;
		std::vector<PersistentPetData> read;
		REQUIRE(packet >> io::read<uint64>(characterGuid));
		REQUIRE(ReadPersistentPets(packet, read));
		CHECK(characterGuid == CharacterGuid);
		REQUIRE(read.size() == 2);
		CheckSamePet(read[0], pets[0]);
		CheckSamePet(read[1], pets[1]);
	}

	void CheckPetRoundTrip(const uint8 opCode, const PersistentPetData& pet)
	{
		const std::vector<char> buffer = WritePetPacket(opCode, pet);

		io::MemorySource source{ buffer };
		auth::IncomingPacket packet;
		REQUIRE(auth::IncomingPacket::Start(packet, source) == ReceiveState::Complete);
		CHECK(packet.GetId() == opCode);

		uint64 characterGuid = 0;
		PersistentPetData read;
		REQUIRE(packet >> io::read<uint64>(characterGuid) >> read);
		CHECK(characterGuid == CharacterGuid);
		CheckSamePet(read, pet);
	}
}

TEST_CASE("PlayerPetData round-trips a list of two pets with spells", "[pets][auth_protocol]")
{
	CheckPetListRoundTrip(auth::realm_world_packet::PlayerPetData);
}

TEST_CASE("PetData round-trips a list of two pets with spells", "[pets][auth_protocol]")
{
	CheckPetListRoundTrip(auth::world_realm_packet::PetData);
}

TEST_CASE("an empty pet list round-trips", "[pets][auth_protocol]")
{
	const std::vector<char> buffer = WritePetListPacket(auth::world_realm_packet::PetData, {});

	io::MemorySource source{ buffer };
	auth::IncomingPacket packet;
	REQUIRE(auth::IncomingPacket::Start(packet, source) == ReceiveState::Complete);

	uint64 characterGuid = 0;
	std::vector<PersistentPetData> read = { MakePet(9, 9, "Stale", {}) };
	REQUIRE(packet >> io::read<uint64>(characterGuid));
	REQUIRE(ReadPersistentPets(packet, read));
	CHECK(read.empty());
}

TEST_CASE("a truncated pet list fails to read", "[pets][auth_protocol]")
{
	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	WritePersistentPets(writer, { MakePet(1, 3, "Fido", { 10 }), MakePet(2, 4, "Shade", {}) });
	sink.Flush();
	buffer.resize(buffer.size() - 3);

	io::MemorySource source{ buffer };
	io::Reader reader{ source };
	std::vector<PersistentPetData> read;
	CHECK_FALSE(ReadPersistentPets(reader, read));
}

TEST_CASE("CreatePet round-trips a creation request", "[pets][auth_protocol]")
{
	// A tamed pet: no database id yet, to be summoned once it exists.
	PersistentPetData request = MakePet(0, 3, "Boar", { 11 });
	request.isDead = false;
	request.wasSummoned = true;
	CheckPetRoundTrip(auth::world_realm_packet::CreatePet, request);
}

TEST_CASE("PetCreated round-trips the created pet", "[pets][auth_protocol]")
{
	PersistentPetData created = MakePet(0x00ABCDEF, 3, "Boar", { 11, 12 });
	created.wasSummoned = true;
	CheckPetRoundTrip(auth::realm_world_packet::PetCreated, created);
}
