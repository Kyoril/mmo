// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// How a pet gets its name.
	namespace pet_name_mode
	{
		enum Type
		{
			/// The pet uses the name of its pet entry.
			Fixed,
			/// The pet picks a random name from the entry's random name list.
			RandomFromList
		};
	}

	/// Which level a pet uses.
	namespace pet_level_mode
	{
		enum Type
		{
			/// The pet levels on its own.
			OwnLevel,
			/// The pet follows the level of its master.
			MasterLevel
		};
	}

	/// Which level is used for a pet that follows its master.
	namespace pet_level_source
	{
		enum Type
		{
			/// The creature's own level (only valid for temporary pets).
			Creature,
			/// The character level of the owner.
			CharacterLevel,
			/// The active class level of the owner.
			ClassLevel
		};
	}

	/// Which persistent pet stat a ModPetStatsPct aura of the owner scales (its miscvaluea).
	namespace pet_stat_kind
	{
		enum Type
		{
			/// Melee damage (min and max damage).
			Damage = 0,
			/// Max health.
			Health = 1,

			Count_
		};
	}

	/// What happens if a summon would exceed the persistent pet cap.
	namespace pet_limit_policy
	{
		enum Type
		{
			/// Dismiss the oldest pet of the group.
			ReplaceOldest,
			/// Fail the summon.
			FailAtCap
		};
	}

	/// Pet behavior strategies.
	namespace pet_strategy
	{
		enum Type
		{
			/// Assist the owner in combat.
			Assist,
			/// Do not engage on its own.
			Passive
		};
	}

	/// Commands an owner can give to a pet.
	namespace pet_command
	{
		enum Type
		{
			Attack,
			Follow,
			Stay,
			SetStrategy,
			Dismiss
		};
	}

	/// Result codes of pet related actions.
	namespace pet_result
	{
		enum Type
		{
			Ok,
			TooManyPets,
			NoPet,
			PetDead,
			NotTamable,
			TooHighLevel,
			NoFreeSlot,
			AlreadyHavePet,
			WrongClass,
			InvalidName,
			NamingNotAllowed,
			Disabled
		};
	}

	/// Checks whether the client counts a unit as one of the active player's temporary pets: a
	/// unit owned by the player that is neither the player nor its persistent pet.
	/// @param unitGuid The unit.
	/// @param ownerGuid The unit's object_fields::Owner.
	/// @param playerGuid The active player (0 if there is none yet).
	/// @param persistentPetGuid The player's summoned persistent pet (0 if none).
	inline bool IsTemporaryPetOf(const uint64 unitGuid, const uint64 ownerGuid, const uint64 playerGuid, const uint64 persistentPetGuid)
	{
		return playerGuid != 0 && unitGuid != playerGuid && unitGuid != persistentPetGuid && ownerGuid == playerGuid;
	}

	/// Maximum number of stored (stabled) pets per character.
	constexpr uint32 MaxStoredPets = 5;

	/// Maximum length of a pet name.
	constexpr uint32 MaxPetNameLength = 12;

	/// Maximum size of a stored pet name in bytes: the size of the realm's character_pets.name
	/// column. Names a player chooses are shorter (MaxPetNameLength), but a tamed pet takes the
	/// creature's name, which can be longer.
	constexpr uint32 MaxStoredPetNameBytes = 32;

	/// Checks whether a pet name is valid: 1 to MaxPetNameLength ASCII letters. Case is not validated, see NormalizePetName.
	/// @param name The name to check.
	bool IsValidPetName(const String& name);

	/// Normalizes a pet name so that the first letter is upper case and all others are lower case.
	/// @param name The name to normalize.
	String NormalizePetName(const String& name);

	/// Shortens a UTF-8 name to at most MaxStoredPetNameBytes bytes, cutting at a character
	/// boundary so no partial character is left at the end.
	/// @param name The name to clamp.
	/// @return The name, or its longest prefix that fits.
	String ClampStoredPetName(const String& name);
}
