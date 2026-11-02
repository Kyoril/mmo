// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The owner-aware reaction rule shared by the client's unit types: a unit (pet, summon, totem) is
// treated like the unit at the top of its owner chain (object_fields::Owner). Units with the same
// top owner are always friendly; otherwise the factions of both top owners decide. The world
// server applies the same rule in GameUnitS::UnitIsFriendly / UnitIsEnemy.

#pragma once

#include "base/typedefs.h"
#include "game/guid_utils.h"

namespace mmo
{
	namespace owner_chain
	{
		/// The most owner links followed from a unit to its top owner. Longer chains, and chains that
		/// loop, end at the unit reached after this many links.
		constexpr uint32 MaxDepth = 4;

		/// Follows the owner chain of a unit to its top.
		/// @param unit The unit to start at.
		/// @param findOwner Callable (const Unit&) -> const Unit*: the unit's owner, or nullptr if it
		///        has none or the owner is not known.
		/// @return The top owner, or @p unit itself if it has no known owner.
		template <typename Unit, typename FindOwner>
		const Unit& ResolveTopOwner(const Unit& unit, FindOwner&& findOwner)
		{
			const Unit* current = &unit;
			for (uint32 depth = 0; depth < MaxDepth; ++depth)
			{
				const Unit* owner = findOwner(*current);
				if (owner == nullptr || owner == current)
				{
					break;
				}

				current = owner;
			}

			return *current;
		}

		/// Checks whether a unit is owned by a player, directly or further up its owner chain (a player's
		/// pet, or a summon of that pet). Units owned only by creatures (boss adds, NPC swarms) are not.
		/// When the chain leaves the known units (an owner out of sight), the guid of the last known
		/// owner link decides.
		/// @param unit The unit to check.
		/// @param findOwner See ResolveTopOwner.
		/// @param guidOf Callable (const Unit&) -> uint64: the unit's guid.
		/// @param ownerGuidOf Callable (const Unit&) -> uint64: the unit's owner guid (0 if none).
		/// @return true if the top of the owner chain is a player other than @p unit itself.
		template <typename Unit, typename FindOwner, typename GuidOf, typename OwnerGuidOf>
		bool IsOwnedByPlayer(const Unit& unit, FindOwner&& findOwner, GuidOf&& guidOf, OwnerGuidOf&& ownerGuidOf)
		{
			const Unit& top = ResolveTopOwner(unit, findOwner);
			if (&top != &unit && IsPlayerGUID(guidOf(top)))
			{
				return true;
			}

			const uint64 ownerGuid = ownerGuidOf(top);
			return ownerGuid != 0 && ownerGuid != guidOf(top) && IsPlayerGUID(ownerGuid);
		}

		/// Checks whether two units are friendly, resolving their owners first.
		/// @param unit The first unit.
		/// @param other The second unit.
		/// @param findOwner See ResolveTopOwner.
		/// @param factionIsFriendly Callable (const Unit&, const Unit&) -> bool: the faction check
		///        between two top owners.
		template <typename Unit, typename FindOwner, typename FactionCheck>
		bool IsFriendly(const Unit& unit, const Unit& other, FindOwner&& findOwner, FactionCheck&& factionIsFriendly)
		{
			const Unit& top = ResolveTopOwner(unit, findOwner);
			const Unit& otherTop = ResolveTopOwner(other, findOwner);
			if (&top == &otherTop)
			{
				return true;
			}

			return factionIsFriendly(top, otherTop);
		}

		/// Checks whether two units are hostile, resolving their owners first.
		/// @param unit The first unit.
		/// @param other The second unit.
		/// @param findOwner See ResolveTopOwner.
		/// @param factionIsHostile Callable (const Unit&, const Unit&) -> bool: the faction check
		///        between two top owners.
		template <typename Unit, typename FindOwner, typename FactionCheck>
		bool IsHostile(const Unit& unit, const Unit& other, FindOwner&& findOwner, FactionCheck&& factionIsHostile)
		{
			const Unit& top = ResolveTopOwner(unit, findOwner);
			const Unit& otherTop = ResolveTopOwner(other, findOwner);
			if (&top == &otherTop)
			{
				return false;
			}

			return factionIsHostile(top, otherTop);
		}
	}
}
