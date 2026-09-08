// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_ai_context.h"

#include "bot_core/bot_context.h"
#include "bot_core/bot_nav_service.h"
#include "bot_core/bot_session.h"

#include "proto_data/project.h"

namespace mmo
{
	BotAiContext::BotAiContext(BotSession* session, const uint32 botIndex)
		: m_session(session)
		, m_botIndex(botIndex)
		// Seeding from the index rather than from a clock is what makes a bot's personality
		// reproducible: the same bot makes the same choices across restarts, so a swarm run can
		// be replayed. The mix keeps neighbouring indices from producing correlated streams.
		, m_random(0x9e3779b9u ^ (botIndex * 2654435761u))
		, m_personality(BotPersonality::FromSeed(0x9e3779b9u ^ (botIndex * 2654435761u)))
	{
	}

	BotContext* BotAiContext::GetWorld() const
	{
		return m_session ? &m_session->GetContext() : nullptr;
	}

	void BotAiContext::RefreshPerception()
	{
		if (const BotContext* world = GetWorld())
		{
			m_perception.Refresh(*world, m_grindState, m_nowMs);
			return;
		}

		m_perception = BotPerception{};
	}

	void BotAiContext::RefreshRotation()
	{
		BotContext* world = GetWorld();
		if (!world)
		{
			return;
		}

		// The spell book arrives after the bot is in the world, and grows again on every level.
		// Comparing counts catches both without needing a signal, and costs a vector copy that the
		// rebuild would have made anyway.
		if (world->GetKnownSpellIds().size() == m_rotation.GetSourceSpellCount())
		{
			return;
		}

		const BotNavService* navService = world->GetNavService();
		if (!navService)
		{
			return;
		}

		if (const proto::Project* project = navService->GetProject())
		{
			static_cast<void>(m_rotation.Rebuild(*project, *world));
		}
	}

	bool BotAiContext::HasRecentSwingError(const AttackSwingEvent event) const
	{
		// Swing errors repeat every swing while the cause persists, so a short window is enough -
		// and it has to be short, or the bot keeps correcting for a problem it already fixed.
		constexpr GameTime SwingErrorWindowMs = 3000;

		return m_lastSwingErrorMs != 0
			&& m_lastSwingError == event
			&& m_nowMs - m_lastSwingErrorMs < SwingErrorWindowMs;
	}

	uint32 BotAiContext::RollRange(const uint32 minValue, const uint32 maxValue)
	{
		if (maxValue <= minValue)
		{
			return minValue;
		}

		std::uniform_int_distribution<uint32> distribution(minValue, maxValue);
		return distribution(m_random);
	}
}
