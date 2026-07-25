// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Enumerates the high-nibble type of an object guid, shared between client and server.
	enum class GuidType
	{
		Player = 0,
		Object = 1,
		Transport = 2,
		Unit = 3,
		Pet = 4,
		Item = 5
	};

	/// Gets the high part of a guid which can be used to determine the object type by it's GUID.
	inline GuidType GuidTypeID(uint64 guid) {
		return static_cast<GuidType>(static_cast<uint32>((guid >> 52) & 0xF));
	}
	/// Gets the realm id of a guid.
	inline uint16 GuidRealmID(uint64 guid) {
		return static_cast<uint16>((guid >> 56) & 0xFF);
	}

	/// Determines whether the given GUID belongs to a creature.
	inline bool IsCreatureGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Unit;
	}

	/// Determines whether the given GUID belongs to a pet.
	inline bool IsPetGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Pet;
	}

	/// Determines whether the given GUID belongs to a player.
	inline bool IsPlayerGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Player;
	}

	/// Determines whether the given GUID belongs to a unit.
	inline bool IsUnitGUID(uint64 guid) {
		return IsPlayerGUID(guid) || IsCreatureGUID(guid) || IsPetGUID(guid);
	}

	/// Determines whether the given GUID belongs to an item.
	inline bool IsItemGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Item;
	}

	/// Determines whether the given GUID belongs to a game object (chest for example).
	inline bool IsGameObjectGUID(uint64 guid) {
		return GuidTypeID(guid) == GuidType::Object;
	}

	/// Creates a GUID based on some settings.
	inline uint64_t CreateRealmGUID(uint64 low, uint64 realm, GuidType type) {
		return static_cast<uint64>(low | (realm << 56) | (static_cast<uint64_t>(type) << 52));
	}

	inline uint64_t CreateEntryGUID(uint64 low, uint64 entry, GuidType type) {
		return static_cast<uint64>(low | (entry << 24) | (static_cast<uint64_t>(type) << 52) | 0xF100000000000000);
	}

	/// Determines if a GUID has an entry part based on it's type.
	inline bool GuidHasEntryPart(uint64 guid)
	{
		switch (static_cast<GuidType>(guid))
		{
		case GuidType::Item:
		case GuidType::Player:
			return false;
		default:
			return true;
		}
	}
	/// Gets the entry part of a GUID or 0 if the GUID does not have an entry part.
	inline uint32 GuidEntryPart(uint64 guid)
	{
		if (GuidHasEntryPart(guid))
		{
			return static_cast<uint32>((guid >> 24) & static_cast<uint64>(0x0000000000FFFFFF));
		}

		return 0;
	}
	/// Gets the lower part of a GUID based on it's type.
	inline uint32 guidLowerPart(uint64 guid)
	{
		const uint64 low2 = 0x00000000FFFFFFFF;
		const uint64 low3 = 0x0000000000FFFFFF;

		return GuidHasEntryPart(guid) ?
			static_cast<uint32>(guid & low3) : static_cast<uint32>(guid & low2);
	}
}
