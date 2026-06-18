// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/signal.h"
#include "client_data/project.h"

#include <map>
#include <optional>

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
		explicit CooldownManager(const proto_client::SpellManager& spells);
		~CooldownManager() override = default;

	public:
		/// @brief Called when entering the world to reset cooldown state.
		void OnEnterWorld();

		/// @brief Called when leaving the world to clear cooldowns.
		void OnLeftWorld();

		/// @brief Starts or updates the individual cooldown for a spell.
		/// @param spellId The spell ID.
		/// @param durationMs The cooldown duration in milliseconds.
		void StartCooldown(uint32 spellId, GameTime durationMs);

		/// @brief Starts or updates the shared global cooldown.
		/// @param durationMs The global cooldown duration in milliseconds.
		void StartGlobalCooldown(GameTime durationMs);

		/// @brief Clears the individual cooldown for a spell.
		/// @param spellId The spell ID.
		void ClearCooldown(uint32 spellId);

		/// @brief Clears the shared global cooldown.
		void ClearGlobalCooldown();

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

		/// @brief Information about an active cooldown.
		struct CooldownInfo
		{
			GameTime startTime;     ///< When the cooldown started.
			GameTime duration;      ///< Total duration in milliseconds.
		};

	private:
		/// @brief Whether the given spell is affected by the global cooldown.
		/// Every spell is affected unless it is flagged spell_cooldown_flags::NoGlobalCooldown.
		[[nodiscard]] bool IsAffectedByGlobalCooldown(uint32 spellId) const;
		[[nodiscard]] float GetCooldownProgress(const CooldownInfo& info) const;
		[[nodiscard]] float GetCooldownRemaining(const CooldownInfo& info) const;
		[[nodiscard]] bool IsCooldownExpired(const CooldownInfo& info, GameTime now) const;

		const proto_client::SpellManager& m_spells;

		/// @brief Map of spell ID to cooldown info.
		std::map<uint32, CooldownInfo> m_cooldowns;
		std::optional<CooldownInfo> m_globalCooldown;
	};
}
