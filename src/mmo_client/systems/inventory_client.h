// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"
#include "game_protocol/game_connector.h"
#include "net/realm_connector.h"

namespace mmo
{
	/// This class handles inventory-related packets on the client side.
	class InventoryClient final
	{
	public:
		/// @brief Initializes a new instance of the InventoryClient class.
		/// @param realmConnector The realm connector instance used for packet handling.
		explicit InventoryClient(RealmConnector& realmConnector);

		/// @brief Finalizes an instance of the InventoryClient class.
		~InventoryClient() = default;

		/// @brief Initializes the inventory client by registering packet handlers.
		void Initialize();

		/// @brief Shuts down the inventory client by disconnecting packet handlers.
		void Shutdown();

	private:
		/// @brief Handles an incoming inventory error packet from the server.
		/// @param packet The incoming packet to read from.
		/// @return The packet parse result.
		PacketParseResult OnInventoryError(game::IncomingPacket& packet);

	private:
		/// Reference to the realm connector.
		RealmConnector& m_realmConnector;
		/// Packet handler connection handles.
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerConnections;
	};
}
