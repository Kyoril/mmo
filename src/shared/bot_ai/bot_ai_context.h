// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_grind_state.h"
#include "bot_perception.h"
#include "bot_rotation.h"

#include "game/auto_attack.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <random>
#include <string>

namespace mmo
{
	class BotSession;
	class BotContext;
	class GrindSpotIndex;

	/// Per-bot state the engine, its triggers and its actions all read and write.
	///
	/// Everything a bot decides with lives here; the engine, strategies, triggers and actions are
	/// shared, stateless definitions. That split is what lets one set of definitions - and one
	/// Lua state - drive a whole swarm.
	///
	/// The session may be null. The engine never dereferences it, so its logic can be tested
	/// against a context with no network behind it at all.
	class BotAiContext final : public NonCopyable
	{
	public:
		/// @param session The bot this context belongs to, or nullptr in tests.
		/// @param botIndex Stable index of this bot in the swarm. Seeds the random engine and is
		///        the input to every "is this bot doing X right now" hash, so a bot behaves the
		///        same way across restarts.
		BotAiContext(BotSession* session, uint32 botIndex);

		[[nodiscard]] BotSession* GetSession() const { return m_session; }

		/// The world-facing action surface, or nullptr when there is no session.
		[[nodiscard]] BotContext* GetWorld() const;

		[[nodiscard]] uint32 GetBotIndex() const { return m_botIndex; }

		[[nodiscard]] const BotPerception& GetPerception() const { return m_perception; }

		/// Re-reads the world. Called once per AI tick, before any trigger runs.
		void RefreshPerception();

		[[nodiscard]] GameTime GetNow() const { return m_nowMs; }
		void SetNow(const GameTime nowMs) { m_nowMs = nowMs; }

		[[nodiscard]] std::mt19937& GetRandom() { return m_random; }

		/// Rolls a uniform integer in [minValue, maxValue].
		[[nodiscard]] uint32 RollRange(uint32 minValue, uint32 maxValue);

		[[nodiscard]] BotRotation& GetRotation() { return m_rotation; }
		[[nodiscard]] const BotRotation& GetRotation() const { return m_rotation; }

		/// Re-derives the rotation if the spell book has changed since it was last built. Cheap
		/// enough to call every tick; the check is a size comparison.
		void RefreshRotation();

		/// Whether the bot may start another cast.
		///
		/// The bot models the global cooldown itself rather than waiting to be told. Sending a
		/// cast only queues a packet, and the send reports success whether or not the server will
		/// accept it, so without this the bot casts on every tick and the server refuses almost
		/// all of them.
		[[nodiscard]] bool CanStartCast() const { return m_nowMs >= m_nextCastMs; }

		/// Blocks casting until nowMs. Called with the greater of the global cooldown and the
		/// cast time of the spell just started.
		void SetNextCastTime(const GameTime nowMs) { m_nextCastMs = nowMs; }

		[[nodiscard]] BotGrindState& GetGrindState() { return m_grindState; }
		[[nodiscard]] const BotGrindState& GetGrindState() const { return m_grindState; }

		/// The shared, read-only spawn index every bot picks its grind spots from, or nullptr if
		/// the host did not build one.
		[[nodiscard]] const GrindSpotIndex* GetGrindSpots() const { return m_grindSpots; }
		void SetGrindSpots(const GrindSpotIndex* spots) { m_grindSpots = spots; }

		/// Records a swing the server refused, and why.
		///
		/// This is the bot's only feedback on the details of melee: it cannot see its own facing
		/// or the server's reach calculation, but it is told, precisely, which of them was wrong.
		/// Reacting to that is far more reliable than trying to predict either.
		void NoteSwingError(const AttackSwingEvent event, const GameTime nowMs)
		{
			m_lastSwingError = event;
			m_lastSwingErrorMs = nowMs;
		}

		/// Whether the given error was reported recently enough to still be worth acting on.
		[[nodiscard]] bool HasRecentSwingError(AttackSwingEvent event) const;

		void ClearSwingError() { m_lastSwingErrorMs = 0; }

		/// Name of the last action the engine executed. Diagnostics and telemetry.
		[[nodiscard]] const std::string& GetLastAction() const { return m_lastAction; }
		void SetLastAction(std::string action) { m_lastAction = std::move(action); }

	private:
		BotSession* m_session { nullptr };
		uint32 m_botIndex { 0 };
		BotPerception m_perception;
		BotGrindState m_grindState;
		BotRotation m_rotation;
		const GrindSpotIndex* m_grindSpots { nullptr };
		std::mt19937 m_random;
		GameTime m_nowMs { 0 };
		GameTime m_nextCastMs { 0 };
		AttackSwingEvent m_lastSwingError { attack_swing_event::Unknown };
		GameTime m_lastSwingErrorMs { 0 };
		std::string m_lastAction;
	};
}
