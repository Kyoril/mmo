// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player_validator_adapter.h"
#include "game_server/objects/game_player_s.h"

namespace mmo
{
	PlayerValidatorAdapter::PlayerValidatorAdapter(const GamePlayerS& player) noexcept
		: m_player(player)
	{
	}

	uint32 PlayerValidatorAdapter::GetLevel() const noexcept
	{
		return m_player.GetLevel();
	}

	bool PlayerValidatorAdapter::HasProficiency(uint32 proficiencyId) const noexcept
	{
		// Proficiency ID 0 means no proficiency required
		if (proficiencyId == 0)
		{
			return true;
		}

		// Check if the player has the proficiency bit set in their proficiency mask
		// Note: We're using a combined proficiency check here. In the future, if weapon
		// and armor proficiencies are separated in the data, we might need to check both masks.
		// For now, we check both weapon and armor proficiency masks.
		const uint32 proficiencyBit = (1 << proficiencyId);
		const uint32 weaponProf = m_player.GetWeaponProficiency();
		const uint32 armorProf = m_player.GetArmorProficiency();

		return ((weaponProf & proficiencyBit) != 0) || ((armorProf & proficiencyBit) != 0);
	}

	bool PlayerValidatorAdapter::IsAlive() const noexcept
	{
		return m_player.IsAlive();
	}

	bool PlayerValidatorAdapter::IsInCombat() const noexcept
	{
		return m_player.IsInCombat();
	}

	bool PlayerValidatorAdapter::CanDualWield() const noexcept
	{
		return m_player.CanDualWield();
	}
}
