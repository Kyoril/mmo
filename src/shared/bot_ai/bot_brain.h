// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_ai_context.h"
#include "bot_engine_set.h"

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"

#include <memory>
#include <string>

namespace mmo
{
	class BotSession;
	class BotAiRegistry;
	class GrindSpotIndex;

	struct BotBrainSettings final
	{
		/// How often the bot decides what to do. Deliberately far slower than the host frame:
		/// the expensive part of a tick is re-reading the world, and a bot that reconsiders its
		/// life twenty times a second is not more convincing, only more expensive.
		GameTime decisionIntervalMs { 200 };

		/// Spread across bots so that a swarm does not decide in lockstep. Each bot gets a fixed
		/// offset derived from its index, which keeps the load flat instead of spiky.
		GameTime decisionJitterMs { 200 };

		/// Names of the strategies driving each engine.
		std::string nonCombatStrategy { "grind_world" };
		std::string combatStrategy { "grind_combat" };
		std::string deadStrategy { "grind_dead" };
	};

	/// One bot's decision making: the context it thinks with, the engines it thinks in, and the
	/// clock that decides when it thinks.
	///
	/// Separate from BotSession on purpose. The session is a network peer and knows nothing about
	/// wanting things; the brain is the only part that does. That is also what keeps the e2e
	/// scenario client - a session with no brain - working exactly as it did.
	class BotBrain final : public NonCopyable
	{
	public:
		/// Fired with the action name and its effective relevance whenever the bot acts.
		signal<void(const std::string&, float)> ActionExecuted;

		/// Fired when the bot moves between non-combat, combat and dead.
		signal<void(BotAiState, BotAiState)> StateChanged;

		/// Fired when something the bot was fighting dies while it was the bot's target.
		signal<void(uint64)> TargetKilled;

	public:
		BotBrain(BotSession& session, const BotAiRegistry& registry, uint32 botIndex, BotBrainSettings settings = {});

		/// Points the bot at the shared spawn index. Without one it can still fight what walks
		/// into it, but it will never go looking.
		void SetGrindSpots(const GrindSpotIndex* spots);

		/// Runs one decision if enough time has passed since the last one. Cheap to call every
		/// frame; that is how it is meant to be used.
		void Update(GameTime nowMs);

		/// Forgets every plan and every memory of where it was fighting. Called when the bot
		/// re-enters the world, because none of it survives a world change.
		void Reset();

		[[nodiscard]] BotAiContext& GetContext() { return m_context; }
		[[nodiscard]] BotAiState GetState() const { return m_engines.GetState(); }
		[[nodiscard]] uint32 GetKillCount() const { return m_context.GetGrindState().kills; }
		[[nodiscard]] const std::string& GetLastAction() const { return m_context.GetLastAction(); }

	private:
		/// Notices that the unit the bot was fighting has died. Kills are counted here rather
		/// than from a packet because the bot is not told who landed the killing blow; what it
		/// can see is that its target stopped being alive while it was swinging at it.
		void DetectKill();

	private:
		BotAiContext m_context;
		BotEngineSet m_engines;
		BotBrainSettings m_settings;
		GameTime m_nextDecisionMs { 0 };
		GameTime m_decisionOffsetMs { 0 };
		uint64 m_lastLiveTargetGuid { 0 };
		scoped_connection m_swingErrorConnection;
	};
}
