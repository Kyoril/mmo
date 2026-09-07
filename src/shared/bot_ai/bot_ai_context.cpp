// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_ai_context.h"

#include "bot_core/bot_session.h"

namespace mmo
{
	BotAiContext::BotAiContext(BotSession* session, const uint32 botIndex)
		: m_session(session)
		, m_botIndex(botIndex)
		// Seeding from the index rather than from a clock is what makes a bot's personality
		// reproducible: the same bot makes the same choices across restarts, so a swarm run can
		// be replayed. The mix keeps neighbouring indices from producing correlated streams.
		, m_random(0x9e3779b9u ^ (botIndex * 2654435761u))
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
			m_perception.Refresh(*world);
			return;
		}

		m_perception = BotPerception{};
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
