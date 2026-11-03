// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "movement_client.h"

#include "player_controller.h"
#include "base/utilities.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"
#include "log/default_log_levels.h"

#include <optional>

namespace mmo
{
	MovementClient::MovementClient(RealmConnector &connector)
		: m_connector(connector)
	{
	}

	void MovementClient::Initialize(PlayerController &playerController, IMovementClientListener &listener)
	{
		m_playerController = &playerController;
		m_listener = &listener;

		const uint16 remoteMovementPackets[] = {
			game::realm_client_packet::MoveStartForward,
			game::realm_client_packet::MoveStartBackward,
			game::realm_client_packet::MoveStop,
			game::realm_client_packet::MoveStartStrafeLeft,
			game::realm_client_packet::MoveStartStrafeRight,
			game::realm_client_packet::MoveStopStrafe,
			game::realm_client_packet::MoveStartTurnLeft,
			game::realm_client_packet::MoveStartTurnRight,
			game::realm_client_packet::MoveStopTurn,
			game::realm_client_packet::MoveHeartBeat,
			game::realm_client_packet::MoveSetFacing,
			game::realm_client_packet::MoveJump,
			game::realm_client_packet::MoveFallLand,
			game::realm_client_packet::MoveStartSwim,
			game::realm_client_packet::MoveStopSwim,
			game::realm_client_packet::MoveStartWalk,
			game::realm_client_packet::MoveStopWalk,
			game::realm_client_packet::MoveEnded,
			game::realm_client_packet::MoveSplineDone };
		for (const uint16 opCode : remoteMovementPackets)
		{
			m_handlers += m_connector.RegisterAutoPacketHandler(opCode, *this, &MovementClient::OnMovement);
		}

		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::CreatureMove, *this, &MovementClient::OnCreatureMove);

		const uint16 forceSpeedPackets[] = {
			game::realm_client_packet::ForceMoveSetWalkSpeed,
			game::realm_client_packet::ForceMoveSetRunSpeed,
			game::realm_client_packet::ForceMoveSetRunBackSpeed,
			game::realm_client_packet::ForceMoveSetSwimSpeed,
			game::realm_client_packet::ForceMoveSetSwimBackSpeed,
			game::realm_client_packet::ForceMoveSetTurnRate,
			game::realm_client_packet::ForceSetFlightSpeed,
			game::realm_client_packet::ForceSetFlightBackSpeed };
		for (const uint16 opCode : forceSpeedPackets)
		{
			m_handlers += m_connector.RegisterAutoPacketHandler(opCode, *this, &MovementClient::OnForceMovementSpeedChange);
		}

		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::MoveTeleportAck, *this, &MovementClient::OnMoveTeleport);

		const uint16 speedPackets[] = {
			game::realm_client_packet::MoveSetWalkSpeed,
			game::realm_client_packet::MoveSetRunSpeed,
			game::realm_client_packet::MoveSetRunBackSpeed,
			game::realm_client_packet::MoveSetSwimSpeed,
			game::realm_client_packet::MoveSetSwimBackSpeed,
			game::realm_client_packet::MoveSetTurnRate,
			game::realm_client_packet::SetFlightSpeed,
			game::realm_client_packet::SetFlightBackSpeed };
		for (const uint16 opCode : speedPackets)
		{
			m_handlers += m_connector.RegisterAutoPacketHandler(opCode, *this, &MovementClient::OnMovementSpeedChanged);
		}

		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::MoveRoot, *this, &MovementClient::OnMoveRoot);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStun, *this, &MovementClient::OnMoveStun);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSleep, *this, &MovementClient::OnMoveSleep);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::MoveFear, *this, &MovementClient::OnMoveFear);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::MoveDisorient, *this, &MovementClient::OnMoveDisorient);
		m_handlers += m_connector.RegisterAutoPacketHandler(game::realm_client_packet::MoveCharge, *this, &MovementClient::OnMoveCharge);
	}

	void MovementClient::Shutdown()
	{
		m_handlers.Clear();
		m_playerController = nullptr;
		m_listener = nullptr;
	}

	PacketParseResult MovementClient::OnMovement(game::IncomingPacket &packet)
	{
		uint64 characterGuid;
		MovementInfo movementInfo;
		if (!(packet >> io::read<uint64>(characterGuid) >> movementInfo))
		{
			return PacketParseResult::Disconnect;
		}

		const auto unitPtr = ObjectMgr::Get<GameUnitC>(characterGuid);
		if (!unitPtr)
		{
			WLOG("Received movement packet for unknown unit " << log_hex_digit(characterGuid));
			return PacketParseResult::Pass;
		}

		if (ObjectMgr::GetActivePlayerGuid() != characterGuid)
		{
			// Route all movement to the dead-reckoning renderer.
			// MoveSetFacing and MoveStopTurn are treated as facing-only updates
			// (no position extrapolation reset) — the renderer handles this distinction.
			const uint16 opcode = packet.GetId();
			const bool facingOnly =
			    opcode == game::realm_client_packet::MoveSetFacing;

			if (facingOnly)
			{
				unitPtr->ApplyRemoteFacing(movementInfo.facing);
			}
			else
			{
				unitPtr->EnqueueRemoteMovement(movementInfo);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult MovementClient::OnCreatureMove(game::IncomingPacket &packet)
	{
		std::vector<Vector3> path;

		uint64 guid;
		Vector3 startPosition;
		Vector3 endPosition;
		GameTime startTime;
		GameTime endTime;
		uint32 pathSize;
		std::optional<Radian> targetRotation;
		UnitMovementMode movementMode = unit_movement_mode::Run;

		if (!(packet >> io::read_packed_guid(guid) >> io::read<float>(startPosition.x) >> io::read<float>(startPosition.y) >> io::read<float>(startPosition.z) >> io::read<GameTime>(startTime) >> io::read<GameTime>(endTime) >> io::read<uint32>(pathSize) >> io::read<float>(endPosition.x) >> io::read<float>(endPosition.y) >> io::read<float>(endPosition.z)))
		{
			return PacketParseResult::Disconnect;
		}

		uint8 hasTargetRotation;
		if (!(packet >> io::read<uint8>(hasTargetRotation)))
		{
			return PacketParseResult::Disconnect;
		}

		if (hasTargetRotation)
		{
			float rotation;
			if (!(packet >> io::read<float>(rotation)))
			{
				return PacketParseResult::Disconnect;
			}

			targetRotation = Radian(rotation);
		}

		uint8 rawMovementMode = unit_movement_mode::Run;
		if (!(packet >> io::read<uint8>(rawMovementMode)))
		{
			return PacketParseResult::Disconnect;
		}

		if (rawMovementMode < unit_movement_mode::Count_)
		{
			movementMode = static_cast<UnitMovementMode>(rawMovementMode);
		}

		// Find unit by guid
		const auto unitPtr = ObjectMgr::Get<GameUnitC>(guid);
		if (!unitPtr)
		{
			// We don't know the unit
			WLOG("Received movement packet for unknown unit id " << log_hex_digit(guid));
			return PacketParseResult::Pass;
		}

		path.push_back(startPosition);

		// Read intermediate waypoints written as raw floats.
		// pathSize = total waypoints not counting startPosition; endPosition is #1.
		// So there are (pathSize - 1) intermediate points to read.
		// Previously this used a lossy 11/10-bit packed format relative to the
		// route midpoint, which overflowed for long multi-waypoint NPC routes.
		if (pathSize > 1)
		{
			for (uint32 i = 1; i < pathSize; ++i)  // pathSize-1 iterations (was pathSize-2, off by one)
			{
				Vector3 p;
				if (!(packet >> io::read<float>(p.x) >> io::read<float>(p.y) >> io::read<float>(p.z)))
				{
					return PacketParseResult::Disconnect;
				}

				path.push_back(p);
			}
		}

		path.push_back(endPosition);

		m_listener->OnCreaturePathReceived(guid, path);

		unitPtr->SetMovementPath(path, endTime - startTime, targetRotation, movementMode);

		return PacketParseResult::Pass;
	}

	PacketParseResult MovementClient::OnMovementSpeedChanged(game::IncomingPacket &packet)
	{
		uint64 guid;
		MovementInfo movementInfo;
		float speed;
		if (!(packet >> io::read_packed_guid(guid) >> movementInfo >> io::read<float>(speed)))
		{
			return PacketParseResult::Disconnect;
		}

		// Find unit
		std::shared_ptr<GameUnitC> unit = ObjectMgr::Get<GameUnitC>(guid);
		if (!unit)
		{
			return PacketParseResult::Pass;
		}

		// Adjust position
		unit->GetSceneNode()->SetPosition(movementInfo.position);

		MovementType type = movement_type::Run;
		switch (packet.GetId())
		{
		case game::realm_client_packet::MoveSetWalkSpeed:
			type = movement_type::Walk;
			break;
		case game::realm_client_packet::MoveSetRunSpeed:
			type = movement_type::Run;
			break;
		case game::realm_client_packet::MoveSetRunBackSpeed:
			type = movement_type::Backwards;
			break;
		case game::realm_client_packet::MoveSetSwimSpeed:
			type = movement_type::Swim;
			break;
		case game::realm_client_packet::MoveSetSwimBackSpeed:
			type = movement_type::SwimBackwards;
			break;
		case game::realm_client_packet::MoveSetTurnRate:
			type = movement_type::Turn;
			break;
		case game::realm_client_packet::SetFlightSpeed:
			type = movement_type::Flight;
			break;
		case game::realm_client_packet::SetFlightBackSpeed:
			type = movement_type::FlightBackwards;
			break;
		}

		unit->SetSpeed(type, speed);

		return PacketParseResult::Pass;
	}

	PacketParseResult MovementClient::OnForceMovementSpeedChange(game::IncomingPacket &packet)
	{
		const std::shared_ptr<GameUnitC> &unit = m_playerController->GetControlledUnit();
		if (!unit)
		{
			WLOG("No controlled unit found!");
			return PacketParseResult::Pass;
		}

		MovementType type = movement_type::Run;
		switch (packet.GetId())
		{
		case game::realm_client_packet::ForceMoveSetWalkSpeed:
			type = movement_type::Walk;
			break;
		case game::realm_client_packet::ForceMoveSetRunSpeed:
			type = movement_type::Run;
			break;
		case game::realm_client_packet::ForceMoveSetRunBackSpeed:
			type = movement_type::Backwards;
			break;
		case game::realm_client_packet::ForceMoveSetSwimSpeed:
			type = movement_type::Swim;
			break;
		case game::realm_client_packet::ForceMoveSetSwimBackSpeed:
			type = movement_type::SwimBackwards;
			break;
		case game::realm_client_packet::ForceMoveSetTurnRate:
			type = movement_type::Turn;
			break;
		case game::realm_client_packet::ForceSetFlightSpeed:
			type = movement_type::Flight;
			break;
		case game::realm_client_packet::ForceSetFlightBackSpeed:
			type = movement_type::FlightBackwards;
			break;
		}

		uint32 ackId;
		float speed;

		if (!(packet >> io::read<uint32>(ackId) >> io::read<float>(speed)))
		{
			WLOG("Failed to read force movement speed change packet!");
			return PacketParseResult::Disconnect;
		}

		// Apply speed modifier announce locally and send ack to the server
		unit->SetSpeed(type, speed);
		m_connector.SendMovementSpeedAck(type, ackId, speed, unit->GetMovementInfo());

		return PacketParseResult::Pass;
	}

	PacketParseResult MovementClient::OnMoveTeleport(game::IncomingPacket &packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			ELOG("Failed to read teleported mover guid!");
			return PacketParseResult::Disconnect;
		}

		const std::shared_ptr<GameUnitC> &unit = m_playerController->GetControlledUnit();
		if (!unit || unit->GetGuid() != guid)
		{
			WLOG("Received teleport for unknown or non-controlled unit!");
			return PacketParseResult::Pass;
		}

		uint32 ackId;
		MovementInfo movementInfo;
		if (!(packet >> io::read<uint32>(ackId) >> movementInfo))
		{
			ELOG("Failed to read move teleport packet");
			return PacketParseResult::Disconnect;
		}

		m_playerController->StopAllMovement();

		const Vector3 previousPosition = unit->GetPosition();

		// Teleport player
		DLOG("Received teleport notification to " << movementInfo.position << ": Applying...");
		unit->ApplyMovementInfo(movementInfo);

		// Send ack to the server
		m_connector.SendMoveTeleportAck(ackId, unit->GetMovementInfo());

		m_listener->OnControlledUnitTeleported(previousPosition, unit->GetPosition());

		return PacketParseResult::Pass;
	}

	PacketParseResult MovementClient::OnMoveRoot(game::IncomingPacket &packet)
	{
		return HandleRootingState(packet, "MoveRoot", &RealmConnector::SendMoveRootAck);
	}

	PacketParseResult MovementClient::OnMoveStun(game::IncomingPacket &packet)
	{
		return HandleRootingState(packet, "MoveStun", &RealmConnector::SendMoveStunAck);
	}

	PacketParseResult MovementClient::OnMoveSleep(game::IncomingPacket &packet)
	{
		return HandleRootingState(packet, "MoveSleep", &RealmConnector::SendMoveSleepAck);
	}

	PacketParseResult MovementClient::OnMoveFear(game::IncomingPacket &packet)
	{
		return HandleRootingState(packet, "MoveFear", &RealmConnector::SendMoveFearAck);
	}

	PacketParseResult MovementClient::OnMoveDisorient(game::IncomingPacket &packet)
	{
		return HandleRootingState(packet, "MoveDisorient", &RealmConnector::SendMoveDisorientAck);
	}

	PacketParseResult MovementClient::HandleRootingState(game::IncomingPacket &packet, const char *packetName, const MovementStateAck sendAck)
	{
		uint32 ackId;
		bool applied;
		if (!(packet >> io::read<uint32>(ackId) >> io::read<uint8>(applied)))
		{
			ELOG("Failed to read " << packetName << " packet!");
			return PacketParseResult::Disconnect;
		}

		auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		MovementInfo info = player->GetMovementInfo();
		if (applied)
		{
			info.movementFlags |= movement_flags::Rooted;
			info.movementFlags &= ~movement_flags::Moving;
			m_playerController->StopAllMovement();
		}
		else
		{
			info.movementFlags &= ~movement_flags::Rooted;
		}
		player->ApplyMovementInfo(info);

		(m_connector.*sendAck)(ackId, info);
		return PacketParseResult::Pass;
	}

	PacketParseResult MovementClient::OnMoveCharge(game::IncomingPacket &packet)
	{
		uint32 ackId;
		float speed;
		if (!(packet >> io::read<uint32>(ackId) >> io::read<float>(speed)))
		{
			ELOG("Failed to read MoveCharge packet!");
			return PacketParseResult::Disconnect;
		}

		auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		// Release all held input control bits so no stop packets are generated afterwards,
		// then hand movement control over to the server: this stops local movement without
		// sending packets and suppresses all further movement packets until the movement
		// path finishes (or a timeout hits, in case the path never arrives).
		m_playerController->StopAllMovement();
		player->SetServerControlledMovement(true);

		// Acknowledge the charge with the cleaned movement state; the server adopts this
		// state and only then starts moving the character.
		m_connector.SendMoveChargeAck(ackId, player->GetMovementInfo(), speed);
		return PacketParseResult::Pass;
	}
}
