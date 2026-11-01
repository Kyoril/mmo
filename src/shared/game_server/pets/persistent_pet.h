// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "game/guid_utils.h"

#include <vector>

namespace mmo
{
	/// A stored (persistent) pet of a character, as the realm keeps it and hands it to the world
	/// node. The world node owns the live copy while the character is in the world and reports
	/// changes back (GamePlayerS::storedPetsChanged).
	struct PersistentPetData
	{
		/// Database id of the pet, unique per realm; 0 for a pet the realm has not created yet.
		/// Part of the pet's guid (MakePetGuid).
		uint64 dbId = 0;

		/// The class that granted or tamed the pet. Only that class can summon it, and its class
		/// level is the pet's ClassLevel.
		uint32 classId = 0;

		/// Id of the pet entry (pets.proto).
		uint32 petEntry = 0;

		/// Creature entry of a tamed pet (its model and stats). 0 = the pet entry's unit_entry.
		uint32 unitEntry = 0;

		/// Name of the pet.
		String name;

		/// The pet's level (meaningful for OwnLevel pets; MasterLevel pets take their master's).
		uint32 level = 1;

		/// Experience towards the next level (OwnLevel pets only).
		uint32 xp = 0;

		/// Health when it was stored. 0 means a fresh pet: summoned at full health and power.
		uint32 health = 0;

		/// Power (of the pet's power type) when it was stored.
		uint32 power = 0;

		/// Whether the pet died. A dead tamed pet needs to be revived; a dead summon-style pet
		/// comes back at full health on its next summon.
		bool isDead = false;

		/// Whether the pet was summoned when it was stored. Such a pet comes back when its owner
		/// enters the world or is resurrected.
		bool wasSummoned = false;

		/// Spells the pet learned (LearnPetSpell). Default and level spells come from the entry.
		std::vector<uint32> spells;
	};

	/// Writes a stored pet: the fields in declaration order, bools as uint8, the name with a
	/// uint8 length, the spells as a uint16 count and one uint32 each.
	io::Writer& operator<<(io::Writer& writer, const PersistentPetData& data);

	/// Reads a stored pet written with operator<<.
	io::Reader& operator>>(io::Reader& reader, PersistentPetData& data);
}
