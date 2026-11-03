// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "math/vector3.h"
#include "net/realm_connector.h"

#include <vector>

namespace mmo
{
	class PlayerController;

	/// @brief Receives the movement events that need the world around the player.
	class IMovementClientListener
	{
	public:
		virtual ~IMovementClientListener() = default;

	public:
		/// @brief Called after a creature received a new movement path from the server.
		virtual void OnCreaturePathReceived(uint64 guid, const std::vector<Vector3> &path) = 0;

		/// @brief Called after the server teleported the controlled unit and the teleport was acknowledged.
		virtual void OnControlledUnitTeleported(const Vector3 &previousPosition, const Vector3 &newPosition) = 0;
	};

	/// @brief Handles the server's movement packets: remote unit movement, creature paths, speed
	///        changes, teleports and the movement-restricting states (root, stun, sleep, fear,
	///        disorient, charge) of the controlled unit, including their acknowledgements.
	class MovementClient final : public NonCopyable
	{
	public:
		explicit MovementClient(RealmConnector &connector);

	public:
		/// @brief Registers the packet handlers. The player controller must outlive Shutdown.
		void Initialize(PlayerController &playerController, IMovementClientListener &listener);

		/// @brief Unregisters the packet handlers.
		void Shutdown();

	private:
		PacketParseResult OnMovement(game::IncomingPacket &packet);

		PacketParseResult OnCreatureMove(game::IncomingPacket &packet);

		PacketParseResult OnMovementSpeedChanged(game::IncomingPacket &packet);

		PacketParseResult OnForceMovementSpeedChange(game::IncomingPacket &packet);

		PacketParseResult OnMoveTeleport(game::IncomingPacket &packet);

		PacketParseResult OnMoveRoot(game::IncomingPacket &packet);

		PacketParseResult OnMoveStun(game::IncomingPacket &packet);

		PacketParseResult OnMoveSleep(game::IncomingPacket &packet);

		PacketParseResult OnMoveFear(game::IncomingPacket &packet);

		PacketParseResult OnMoveDisorient(game::IncomingPacket &packet);

		PacketParseResult OnMoveCharge(game::IncomingPacket &packet);

		/// Sends one of the root-like movement state acknowledgements.
		typedef void (RealmConnector::*MovementStateAck)(uint32 ackId, const MovementInfo &movementInfo);

		/// @brief Shared handling of root, stun, sleep, fear and disorient: all of them root the
		///        controlled unit while applied and differ only in their acknowledgement.
		PacketParseResult HandleRootingState(game::IncomingPacket &packet, const char *packetName, MovementStateAck sendAck);

	private:
		RealmConnector &m_connector;
		RealmConnector::PacketHandlerHandleContainer m_handlers;
		PlayerController *m_playerController = nullptr;
		IMovementClientListener *m_listener = nullptr;
	};
}
