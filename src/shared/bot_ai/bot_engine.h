// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action_queue.h"
#include "bot_multiplier.h"
#include "bot_strategy.h"
#include "bot_trigger.h"

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"

#include <string>
#include <vector>

namespace mmo
{
	class BotAiContext;
	class BotAiRegistry;

	/// Tuning knobs of the decision loop. Defaults are the ones the grind strategy was built
	/// against; a strategy set that needs different ones should say so explicitly.
	struct BotEngineSettings final
	{
		/// How long a queued action stays relevant. Past this it is answering a question nobody
		/// is asking any more - the trigger that pushed it will push it again if it still holds.
		GameTime actionExpiryMs { 5000 };

		/// Upper bound on how many actions the engine will consider before giving up on the tick.
		/// Reached only when actions keep failing or keep being vetoed; the normal path runs one.
		uint32 maxIterations { 10 };

		/// Relevance bump for a prerequisite over the action that needs it, so it pops first.
		float prerequisiteBoost { 0.02f };

		/// Relevance bump for an action put back to wait for its prerequisite. Smaller than the
		/// prerequisite boost, and larger than zero so it does not lose its place to whatever
		/// else is queued at the same relevance.
		float retryBoost { 0.01f };

		/// Relevance bump for an alternative over the action that failed.
		float alternativeBoost { 0.03f };
	};

	/// The decision loop: turn the state of the world into exactly one action per tick.
	///
	/// This is a utility priority queue rather than a behaviour tree. Triggers push named actions
	/// with a relevance; multipliers scale or veto them; the most relevant survivor runs. Nothing
	/// is nested and nothing has to be traversed, so two strategies that know nothing about each
	/// other compose correctly just by agreeing on relevance bands.
	///
	/// Exactly one action runs per tick, on purpose. It bounds the work per bot per tick, and it
	/// forces multi-step behaviour to be expressed as chains - prerequisites, alternatives,
	/// continuers - which survive the world changing under them between ticks. A loop that ran
	/// until the queue drained would commit to a plan made from a snapshot.
	class BotEngine final : public NonCopyable
	{
	public:
		/// Fired with the name and final relevance of each action the engine executes. The
		/// behaviour trace a swarm run is read back from.
		signal<void(const std::string&, float)> ActionExecuted;

	public:
		BotEngine(const BotAiRegistry& registry, std::string name, BotEngineSettings settings = {});

		[[nodiscard]] const std::string& GetName() const { return m_name; }

		/// Adds a strategy by name. Unknown names are ignored, and reported by
		/// BotAiRegistry::FindDanglingReferences rather than failing here.
		void AddStrategy(const std::string& name);

		[[nodiscard]] const std::vector<std::string>& GetStrategyNames() const { return m_strategyNames; }

		/// Evaluates triggers and runs at most one action.
		/// @return True if an action executed.
		bool DoNextAction(BotAiContext& context);

		/// Drops all pending actions. Used when the engine is swapped, so that a plan made while
		/// alive does not carry over into being dead.
		void Reset();

		[[nodiscard]] const BotActionQueue& GetQueue() const { return m_queue; }

	private:
		void ProcessTriggers(BotAiContext& context);
		void PushDefaultActions(BotAiContext& context);
		/// Effective relevance of an action after every multiplier, or a negative value if one
		/// of them vetoed it.
		[[nodiscard]] float ScoreAction(BotAiContext& context, const std::string& actionName, float relevance) const;

	private:
		/// A trigger node plus the only thing about it that is per-engine rather than shared:
		/// when it was last polled.
		struct TriggerSlot final
		{
			const BotTrigger* trigger { nullptr };
			BotNextActionList actions;
			GameTime lastCheckMs { 0 };
			bool everChecked { false };
		};

		const BotAiRegistry& m_registry;
		std::string m_name;
		BotEngineSettings m_settings;
		std::vector<std::string> m_strategyNames;
		std::vector<TriggerSlot> m_triggerSlots;
		BotNextActionList m_defaultActions;
		std::vector<const BotMultiplier*> m_multipliers;
		BotActionQueue m_queue;
	};
}
