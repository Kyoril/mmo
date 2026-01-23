// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/signal.h"

#include <map>

namespace mmo
{
	/// @brief Manages spell cooldowns on the client side.
	/// Tracks active cooldowns and provides progress information for UI display.
	class CooldownManager final : public NonCopyable
	{
	public:
		/// @brief Signal emitted when a cooldown starts.
		signal<void(uint32 /*spellId*/, GameTime /*durationMs*/)> CooldownStarted;

		/// @brief Signal emitted when a cooldown ends.
		signal<void(uint32 /*spellId*/)> CooldownEnded;

	public:
		CooldownManager() = default;
		~CooldownManager() override = default;

	public:
		/// @brief Called when entering the world to reset cooldown state.
		void OnEnterWorld();

		/// @brief Called when leaving the world to clear cooldowns.
		void OnLeftWorld();

		/// @brief Starts or updates a cooldown for a spell.
		/// @param spellId The spell ID.
		/// @param durationMs The cooldown duration in milliseconds.
		void StartCooldown(uint32 spellId, GameTime durationMs);

		/// @brief Clears the cooldown for a spell.
		/// @param spellId The spell ID.
		void ClearCooldown(uint32 spellId);

		/// @brief Gets the cooldown progress for a spell.
		/// @param spellId The spell ID.
		/// @return Progress from 0.0 (just started) to 1.0 (ready/no cooldown).
		[[nodiscard]] float GetCooldownProgress(uint32 spellId) const;

		/// @brief Gets the remaining cooldown time in seconds.
		/// @param spellId The spell ID.
		/// @return Remaining time in seconds, or 0 if no cooldown.
		[[nodiscard]] float GetCooldownRemaining(uint32 spellId) const;

		/// @brief Checks if a spell is on cooldown.
		/// @param spellId The spell ID.
		/// @return True if the spell is on cooldown.
		[[nodiscard]] bool IsOnCooldown(uint32 spellId) const;

		/// @brief Updates cooldowns, checking for expired ones.
		/// Should be called once per frame.
		/// @param deltaSeconds Time since last update.
		void Update(float deltaSeconds);

	private:
		/// @brief Information about an active cooldown.
		struct CooldownInfo
		{
			GameTime startTime;     ///< When the cooldown started.
			GameTime duration;      ///< Total duration in milliseconds.
		};

		/// @brief Map of spell ID to cooldown info.
		std::map<uint32, CooldownInfo> m_cooldowns;
	};
}
