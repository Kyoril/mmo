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

	void BotContext::AcceptPartyInvitation()
	{
		if (!m_realmConnector)
		{
			WLOG("Cannot accept party invitation: No realm connector available");
			return;
		}

		m_realmConnector->AcceptPartyInvitation();
	}

	void BotContext::DeclinePartyInvitation()
	{
		if (!m_realmConnector)
		{
			WLOG("Cannot decline party invitation: No realm connector available");
			return;
		}

		m_realmConnector->DeclinePartyInvitation();
	}

	// ============================================================
	// Party Information Methods
	// ============================================================

	bool BotContext::IsInParty() const
	{
		if (!m_realmConnector)
		{
			return false;
		}

		return m_realmConnector->IsInParty();
	}

	uint32 BotContext::GetPartyMemberCount() const
	{
		if (!m_realmConnector)
		{
			return 0;
		}

		return m_realmConnector->GetPartyMemberCount();
	}

	uint64 BotContext::GetPartyLeaderGuid() const
	{
		if (!m_realmConnector)
		{
			return 0;
		}

		return m_realmConnector->GetPartyLeaderGuid();
	}

	bool BotContext::IsPartyLeader() const
	{
		if (!m_realmConnector)
		{
			return false;
		}

		return m_realmConnector->IsPartyLeader();
	}

	uint64 BotContext::GetPartyMemberGuid(uint32 index) const
	{
		if (!m_realmConnector)
		{
			return 0;
		}

		const auto* member = m_realmConnector->GetPartyMember(index);
		return member ? member->guid : 0;
	}

	std::string BotContext::GetPartyMemberName(uint32 index) const
	{
		if (!m_realmConnector)
		{
			return "";
		}

		const auto* member = m_realmConnector->GetPartyMember(index);
		return member ? member->name : "";
	}

	std::vector<uint64> BotContext::GetPartyMemberGuids() const
	{
		if (!m_realmConnector)
		{
			return {};
		}

		return m_realmConnector->GetPartyMemberGuids();
	}

	// ============================================================
	// Party Action Methods
	// ============================================================

	void BotContext::LeaveParty()
	{
		if (!m_realmConnector)
		{
			WLOG("Cannot leave party: No realm connector available");
			return;
		}

		m_realmConnector->LeaveParty();
	}

	void BotContext::KickFromParty(const std::string& playerName)
	{
		if (!m_realmConnector)
		{
			WLOG("Cannot kick from party: No realm connector available");
			return;
		}

		m_realmConnector->KickFromParty(playerName);
	}

	void BotContext::InviteToParty(const std::string& playerName)
	{
		if (!m_realmConnector)
		{
			WLOG("Cannot invite to party: No realm connector available");
			return;
		}

		m_realmConnector->InviteToParty(playerName);
	}
}
