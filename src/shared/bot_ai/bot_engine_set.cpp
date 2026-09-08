// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_engine_set.h"

#include "bot_ai_context.h"

#include "base/macros.h"

namespace mmo
{
	const char* GetBotAiStateName(const BotAiState state)
	{
		switch (state)
		{
		case BotAiState::NonCombat: return "non_combat";
		case BotAiState::Combat: return "combat";
		case BotAiState::Dead: return "dead";
		}

		UNREACHABLE();
		return "unknown";
	}

	BotEngineSet::BotEngineSet(const BotAiRegistry& registry, const BotEngineSettings settings)
		: m_nonCombat(std::make_unique<BotEngine>(registry, "non_combat", settings))
		, m_combat(std::make_unique<BotEngine>(registry, "combat", settings))
		, m_dead(std::make_unique<BotEngine>(registry, "dead", settings))
	{
	}

	BotEngine& BotEngineSet::GetEngine(const BotAiState state)
	{
		switch (state)
		{
		case BotAiState::NonCombat: return *m_nonCombat;
		case BotAiState::Combat: return *m_combat;
		case BotAiState::Dead: return *m_dead;
		}

		UNREACHABLE();
		return *m_nonCombat;
	}

	BotAiState BotEngineSet::ResolveState(const BotAiContext& context)
	{
		const BotPerception& perception = context.GetPerception();

		// An invalid perception means the bot is not in the world yet. Treating that as
		// non-combat is safe: every action guards on the perception being valid anyway, so the
		// engine simply finds nothing useful to do.
		if (!perception.valid)
		{
			return BotAiState::NonCombat;
		}

		if (!perception.alive)
		{
			return BotAiState::Dead;
		}

		return perception.IsInCombat() ? BotAiState::Combat : BotAiState::NonCombat;
	}

	bool BotEngineSet::Update(BotAiContext& context)
	{
		const BotAiState resolved = ResolveState(context);
		if (resolved != m_state)
		{
			const BotAiState previous = m_state;
			m_state = resolved;

			// The engine being entered starts from nothing. Whatever it queued the last time it
			// was driving was decided under circumstances that have since changed - most starkly
			// when the change is dying.
			GetEngine(m_state).Reset();
			StateChanged(previous, m_state);
		}

		return GetEngine(m_state).DoNextAction(context);
	}

	void BotEngineSet::Reset()
	{
		m_nonCombat->Reset();
		m_combat->Reset();
		m_dead->Reset();
		m_state = BotAiState::NonCombat;
	}
}
