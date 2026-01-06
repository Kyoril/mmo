// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_context.h"
#include "bot_realm_connector.h"

#include "base/clock.h"
#include "log/default_log_levels.h"

namespace mmo
{
	// Forward declaration of BotConfig (defined in bot_main.cpp)
	struct BotConfig;

	BotContext::BotContext(
		std::shared_ptr<BotRealmConnector> realmConnector,
		const BotConfig& config)
		: m_realmConnector(std::move(realmConnector))
		, m_config(config)
	{
	}

	uint64 BotContext::GetSelectedCharacterGuid() const
	{
		if (!m_realmConnector)
		{
			return 0;
		}

		return m_realmConnector->GetSelectedGuid();
	}

	const MovementInfo& BotContext::GetMovementInfo() const
	{
		// Return our cached movement info which is kept up-to-date after sending movement packets
		// The realm connector's movement info is only updated by server (teleports, speed changes)
		// and would be stale for client-initiated movement
		return m_cachedMovementInfo;
	}

	void BotContext::SendChatMessage(const std::string& message, ChatType chatType, const std::string& target)
	{
		if (!m_realmConnector)
		{
			WLOG("Cannot send chat message: No realm connector available");
			return;
		}

		m_realmConnector->SendChatMessage(message, chatType, target);
	}

	void BotContext::SendMovementUpdate(uint16 opCode, const MovementInfo& info)
	{
		if (!m_realmConnector)
		{
			WLOG("Cannot send movement update: No realm connector available");
			return;
		}

		const uint64 guid = GetSelectedCharacterGuid();
		if (guid == 0)
		{
			WLOG("Cannot send movement update: No character selected");
			return;
		}

		m_realmConnector->SendMovementUpdate(guid, opCode, info);
		m_cachedMovementInfo = info;
	}

	void BotContext::UpdateMovementInfo(const MovementInfo& info)
	{
		m_cachedMovementInfo = info;
	}

	void BotContext::SendLandedPacket()
	{
		// Get current movement info and remove the FALLING flag
		MovementInfo landedMovement = GetMovementInfo();
		landedMovement.movementFlags &= ~movement_flags::Falling;
		landedMovement.timestamp = GetServerTime();

		// Send MoveFallLand packet
		SendMovementUpdate(game::client_realm_packet::MoveFallLand, landedMovement);
		UpdateMovementInfo(landedMovement);
	}

	GameTime BotContext::GetServerTime() const
	{
		return GetAsyncTimeMs();
	}

	void BotContext::SetState(const std::string& key, const std::string& value)
	{
		m_customState[key] = value;
	}

	std::string BotContext::GetState(const std::string& key, const std::string& defaultValue) const
	{
		const auto it = m_customState.find(key);
		if (it != m_customState.end())
		{
			return it->second;
		}

		return defaultValue;
	}

	bool BotContext::HasState(const std::string& key) const
	{
		return m_customState.find(key) != m_customState.end();
	}

	void BotContext::ClearState(const std::string& key)
	{
		m_customState.erase(key);
	}
}
