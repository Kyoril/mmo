// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_context.h"
#include "bot_realm_connector.h"
#include "bot_object_manager.h"
#include "bot_unit.h"

#include "base/clock.h"
#include "log/default_log_levels.h"

#include <cmath>

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

	// ============================================================
	// Unit Awareness Methods
	// ============================================================

	BotObjectManager& BotContext::GetObjectManager()
	{
		return m_realmConnector->GetObjectManager();
	}

	const BotObjectManager& BotContext::GetObjectManager() const
	{
		return m_realmConnector->GetObjectManager();
	}

	const BotUnit* BotContext::GetSelf() const
	{
		if (!m_realmConnector)
		{
			return nullptr;
		}

		return m_realmConnector->GetObjectManager().GetSelf();
	}

	Vector3 BotContext::GetPosition() const
	{
		const BotUnit* self = GetSelf();
		if (self)
		{
			return self->GetPosition();
		}

		// Fall back to cached movement info if unit not yet available
		return m_cachedMovementInfo.position;
	}

	std::vector<const BotUnit*> BotContext::GetNearbyUnits(float radius) const
	{
		if (!m_realmConnector)
		{
			return {};
		}

		const BotUnit* self = GetSelf();
		if (!self)
		{
			return {};
		}

		return m_realmConnector->GetObjectManager().GetNearbyUnits(self->GetPosition(), radius);
	}

	std::vector<const BotUnit*> BotContext::GetNearbyPlayers(float radius) const
	{
		if (!m_realmConnector)
		{
			return {};
		}

		const BotUnit* self = GetSelf();
		if (!self)
		{
			return {};
		}

		return m_realmConnector->GetObjectManager().GetNearbyPlayers(self->GetPosition(), radius);
	}

	std::vector<const BotUnit*> BotContext::GetNearbyCreatures(float radius) const
	{
		if (!m_realmConnector)
		{
			return {};
		}

		const BotUnit* self = GetSelf();
		if (!self)
		{
			return {};
		}

		return m_realmConnector->GetObjectManager().GetNearbyCreatures(self->GetPosition(), radius);
	}

	const BotUnit* BotContext::GetNearestHostile(float maxRange) const
	{
		if (!m_realmConnector)
		{
			return nullptr;
		}

		return m_realmConnector->GetObjectManager().GetNearestHostile(maxRange);
	}

	const BotUnit* BotContext::GetNearestAttackable(float maxRange) const
	{
		if (!m_realmConnector)
		{
			return nullptr;
		}

		return m_realmConnector->GetObjectManager().GetNearestAttackable(maxRange);
	}

	const BotUnit* BotContext::GetNearestFriendly(float maxRange) const
	{
		if (!m_realmConnector)
		{
			return nullptr;
		}

		return m_realmConnector->GetObjectManager().GetNearestFriendly(maxRange);
	}

	const BotUnit* BotContext::GetNearestFriendlyPlayer(float maxRange) const
	{
		if (!m_realmConnector)
		{
			return nullptr;
		}

		return m_realmConnector->GetObjectManager().GetNearestFriendlyPlayer(maxRange);
	}

	std::vector<const BotUnit*> BotContext::GetHostilesInRange(float maxRange) const
	{
		if (!m_realmConnector)
		{
			return {};
		}

		return m_realmConnector->GetObjectManager().GetHostilesInRange(maxRange);
	}

	std::vector<const BotUnit*> BotContext::GetFriendlyPlayersInRange(float maxRange) const
	{
		if (!m_realmConnector)
		{
			return {};
		}

		return m_realmConnector->GetObjectManager().GetFriendlyPlayersInRange(maxRange);
	}

	std::vector<const BotUnit*> BotContext::GetUnitsTargetingSelf(float maxRange) const
	{
		if (!m_realmConnector)
		{
			return {};
		}

		return m_realmConnector->GetObjectManager().GetUnitsTargetingSelf(maxRange);
	}

	const BotUnit* BotContext::GetUnit(uint64 guid) const
	{
		if (!m_realmConnector)
		{
			return nullptr;
		}

		return m_realmConnector->GetObjectManager().GetUnit(guid);
	}

	bool BotContext::HasUnit(uint64 guid) const
	{
		if (!m_realmConnector)
		{
			return false;
		}

		return m_realmConnector->GetObjectManager().HasUnit(guid);
	}

	size_t BotContext::GetUnitCount() const
	{
		if (!m_realmConnector)
		{
			return 0;
		}

		return m_realmConnector->GetObjectManager().GetUnitCount();
	}

	void BotContext::ForEachUnit(const std::function<void(const BotUnit&)>& callback) const
	{
		if (!m_realmConnector)
		{
			return;
		}

		m_realmConnector->GetObjectManager().ForEachUnit(callback);
	}

	// ============================================================
	// Combat Methods
	// ============================================================

	void BotContext::StartAutoAttack(uint64 targetGuid)
	{
		if (!m_realmConnector)
		{
			WLOG("Cannot start attack: No realm connector available");
			return;
		}

		m_realmConnector->SendAttackStart(targetGuid);
	}

	void BotContext::StopAutoAttack()
	{
		if (!m_realmConnector)
		{
			WLOG("Cannot stop attack: No realm connector available");
			return;
		}

		m_realmConnector->SendAttackStop();
	}

	bool BotContext::IsAutoAttacking() const
	{
		if (!m_realmConnector)
		{
			return false;
		}

		return m_realmConnector->IsAutoAttacking();
	}

	uint64 BotContext::GetAutoAttackTarget() const
	{
		if (!m_realmConnector)
		{
			return 0;
		}

		return m_realmConnector->GetAutoAttackTarget();
	}

	// ============================================================
	// Movement Methods
	// ============================================================

	void BotContext::StartMovingForward()
	{
		if (!m_realmConnector)
		{
			return;
		}

		MovementInfo info = GetMovementInfo();
		info.movementFlags |= movement_flags::Forward;
		info.timestamp = GetServerTime();
		SendMovementUpdate(game::client_realm_packet::MoveStartForward, info);
	}

	void BotContext::StopMoving()
	{
		if (!m_realmConnector)
		{
			return;
		}

		MovementInfo info = GetMovementInfo();
		info.movementFlags &= ~movement_flags::Moving;
		info.timestamp = GetServerTime();
		SendMovementUpdate(game::client_realm_packet::MoveStop, info);
	}

	bool BotContext::IsMoving() const
	{
		const MovementInfo& info = GetMovementInfo();
		return (info.movementFlags & movement_flags::Moving) != 0;
	}

	void BotContext::FacePosition(const Vector3& targetPosition)
	{
		if (!m_realmConnector)
		{
			return;
		}

		// Calculate direction to target using our cached position
		const Vector3 myPos = m_cachedMovementInfo.position;
		const Vector3 direction = targetPosition - myPos;
		
		// Calculate yaw angle (facing direction in the XZ plane)
		// atan2(x, z) gives the angle from the Z axis (forward)
		const Radian newFacing(std::atan2(direction.x, direction.z));

		MovementInfo info = GetMovementInfo();
		info.facing = newFacing;
		info.timestamp = GetServerTime();
		SendMovementUpdate(game::client_realm_packet::MoveSetFacing, info);
	}

	void BotContext::FaceUnit(uint64 targetGuid)
	{
		const BotUnit* target = GetUnit(targetGuid);
		if (target)
		{
			FacePosition(target->GetPosition());
		}
	}

	float BotContext::GetDistanceTo(const Vector3& position) const
	{
		// Use cached movement info position (our simulated position)
		// instead of the BotUnit position (last server update)
		const Vector3 myPos = m_cachedMovementInfo.position;
		const Vector3 diff = position - myPos;
		return std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
	}

	float BotContext::GetDistanceToUnit(uint64 guid) const
	{
		const BotUnit* target = GetUnit(guid);
		if (!target)
		{
			return 99999.0f;
		}

		return GetDistanceTo(target->GetPosition());
	}

	Radian BotContext::GetAngleTo(const Vector3& targetPosition) const
	{
		// Use cached movement info position
		const Vector3 myPos = m_cachedMovementInfo.position;
		const Vector3 direction = targetPosition - myPos;
		return Radian(std::atan2(direction.x, direction.z));
	}
}
