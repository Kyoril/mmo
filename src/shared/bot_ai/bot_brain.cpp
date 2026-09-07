// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_brain.h"

#include "bot_ai_registry.h"
#include "bot_core/bot_context.h"
#include "bot_core/bot_session.h"
#include "bot_core/bot_unit.h"

#include <utility>

namespace mmo
{
	BotBrain::BotBrain(BotSession& session, const BotAiRegistry& registry, const uint32 botIndex, BotBrainSettings settings)
		: m_context(&session, botIndex)
		, m_engines(registry)
		, m_settings(std::move(settings))
	{
		m_engines.GetEngine(BotAiState::NonCombat).AddStrategy(m_settings.nonCombatStrategy);
		m_engines.GetEngine(BotAiState::Combat).AddStrategy(m_settings.combatStrategy);
		m_engines.GetEngine(BotAiState::Dead).AddStrategy(m_settings.deadStrategy);

		// A fixed offset per bot rather than a random one: the swarm stays spread out, and a bot
		// keeps the same phase across restarts, so a run can be compared with the one before it.
		if (m_settings.decisionJitterMs > 0)
		{
			m_decisionOffsetMs = botIndex % m_settings.decisionJitterMs;
		}

		for (const BotAiState state : { BotAiState::NonCombat, BotAiState::Combat, BotAiState::Dead })
		{
			m_engines.GetEngine(state).ActionExecuted.connect([this](const std::string& action, const float relevance)
				{
					ActionExecuted(action, relevance);
				});
		}

		m_engines.StateChanged.connect([this](const BotAiState from, const BotAiState to)
			{
				StateChanged(from, to);
			});
	}

	void BotBrain::SetGrindSpots(const GrindSpotIndex* spots)
	{
		m_context.SetGrindSpots(spots);
	}

	void BotBrain::Reset()
	{
		m_engines.Reset();
		m_context.GetGrindState().ClearSpot();
		m_context.GetGrindState().blacklist.clear();
		m_lastLiveTargetGuid = 0;
		m_nextDecisionMs = 0;
	}

	void BotBrain::DetectKill()
	{
		const BotPerception& perception = m_context.GetPerception();

		BotSession* session = m_context.GetSession();
		if (!session)
		{
			return;
		}

		const BotContext& world = session->GetContext();

		if (m_lastLiveTargetGuid != 0)
		{
			// Ask about the unit itself rather than about the current target, so that switching
			// targets is not mistaken for a kill. A unit that is gone counts: the bot cannot tell
			// a despawned corpse from one it never saw fall, and does not need to.
			const BotUnit* previous = world.GetUnit(m_lastLiveTargetGuid);
			if (!previous || !previous->IsAlive())
			{
				++m_context.GetGrindState().kills;
				TargetKilled(m_lastLiveTargetGuid);
				m_lastLiveTargetGuid = 0;
			}
		}

		if (perception.targetExists && perception.targetAlive && perception.IsInCombat())
		{
			m_lastLiveTargetGuid = perception.targetGuid;
		}
	}

	void BotBrain::Update(const GameTime nowMs)
	{
		if (m_nextDecisionMs == 0)
		{
			// The first decision is offset, and every later one is spaced from it, so the phase
			// this bot ends up on is the one it keeps.
			m_nextDecisionMs = nowMs + m_decisionOffsetMs;
		}

		if (nowMs < m_nextDecisionMs)
		{
			return;
		}

		m_nextDecisionMs = nowMs + m_settings.decisionIntervalMs;

		BotSession* session = m_context.GetSession();
		if (!session || !session->IsWorldReady())
		{
			return;
		}

		m_context.SetNow(nowMs);

		// One read of the world per decision, shared by every trigger and action that follows.
		m_context.RefreshPerception();

		DetectKill();

		static_cast<void>(m_engines.Update(m_context));
	}
}
