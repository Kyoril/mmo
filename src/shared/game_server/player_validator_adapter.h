// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file player_validator_adapter.h
 *
 * @brief Adapter connecting GamePlayerS to IPlayerValidatorContext.
 *
 * Implements the Adapter pattern to bridge between the domain interface
 * required by ItemValidator and the concrete GamePlayerS implementation.
 * This keeps ItemValidator testable while working with existing infrastructure.
 */

#pragma once

#include "i_player_validator_context.h"

namespace mmo
{
	class GamePlayerS;

	/**
	 * @brief Adapter that exposes GamePlayerS through IPlayerValidatorContext.
	 *
	 * This lightweight adapter wraps a GamePlayerS reference and forwards
	 * validation-relevant queries to it. It enables ItemValidator to work
	 * with real game objects without direct coupling.
	 *
	 * Usage:
	 * @code
	 * GamePlayerS player = ...;
	 * PlayerValidatorAdapter adapter(player);
	 * ItemValidator validator(adapter);
	 * @endcode
	 */
	class PlayerValidatorAdapter final : public IPlayerValidatorContext
	{
	public:
		/**
		 * @brief Constructs adapter for a specific player.
		 * @param player The game player to adapt.
		 */
		explicit PlayerValidatorAdapter(const GamePlayerS& player) noexcept;

		// IPlayerValidatorContext implementation
		uint32 GetLevel() const noexcept override;
		uint32 GetWeaponProficiency() const noexcept override;
		uint32 GetArmorProficiency() const noexcept override;
		bool IsAlive() const noexcept override;
		bool IsInCombat() const noexcept override;
		bool CanDualWield() const noexcept override;

	private:
		const GamePlayerS& m_player;
	};
}
