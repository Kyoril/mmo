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
		return m_player.HasProficiency(proficiencyId);
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
