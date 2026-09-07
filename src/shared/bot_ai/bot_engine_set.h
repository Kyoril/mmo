// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_engine.h"

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"

#include <memory>
#include <string>

namespace mmo
{
	class BotAiContext;
	class BotAiRegistry;

	/// The coarse situation a bot is in. One engine per state, swapped as the state changes.
	enum class BotAiState : uint8
	{
		/// Alive, nothing hitting us, nothing being hit by us.
		NonCombat = 0,

		/// Fighting, or being fought.
		Combat = 1,

		/// Dead. Almost nothing that applies while alive applies here, which is exactly why it
		/// gets its own engine rather than a multiplier vetoing half the strategies.
		Dead = 2,
	};

	[[nodiscard]] const char* GetBotAiStateName(BotAiState state);

	/// The three engines a bot has, and the rule for which one is driving.
	///
	/// Splitting by state rather than layering everything into one engine keeps each engine small
	/// enough to reason about, and means a strategy never has to ask whether the bot is alive
	/// before doing anything. The cost is that a plan does not survive a state change - which is
	/// the right answer: a plan made while alive is worthless once dead.
	class BotEngineSet final : public NonCopyable
	{
	public:
		/// Fired when the driving engine changes, with the state being left and the one entered.
		signal<void(BotAiState, BotAiState)> StateChanged;

	public:
		BotEngineSet(const BotAiRegistry& registry, BotEngineSettings settings = {});

		[[nodiscard]] BotEngine& GetEngine(BotAiState state);
		[[nodiscard]] BotAiState GetState() const { return m_state; }
		[[nodiscard]] BotEngine& GetCurrentEngine() { return GetEngine(m_state); }

		/// Picks the engine matching the world as the context currently sees it, then runs one
		/// action on it.
		/// @return True if an action executed.
		bool Update(BotAiContext& context);

		/// Forgets every plan in every engine. Used when a bot re-enters the world.
		void Reset();

	private:
		[[nodiscard]] static BotAiState ResolveState(const BotAiContext& context);

	private:
		std::unique_ptr<BotEngine> m_nonCombat;
		std::unique_ptr<BotEngine> m_combat;
		std::unique_ptr<BotEngine> m_dead;
		BotAiState m_state { BotAiState::NonCombat };
	};
}
