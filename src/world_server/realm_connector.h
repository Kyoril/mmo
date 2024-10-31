// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "auth_protocol/auth_connector.h"
#include "base/big_number.h"
#include "base/sha1.h"
#include "game/game.h"

#include "asio/io_service.hpp"

#include <set>
#include <vector>


namespace mmo
{
	class GamePlayerS;

	namespace proto
	{
		class Project;
	}

	class TimerQueue;
	class WorldInstanceManager;
	class PlayerManager;

	/// A connector which will try to log in to a realm server.
	class RealmConnector final
		: public auth::Connector
		, public auth::IConnectorListener
	{
	public:
		/// Initializes a new instance of the TestConnector class.
		/// @param io The io service to be used in order to create the internal socket.
		/// @param queue A timer queue.
		/// @param defaultHostedMapIds A set of map ids that can be hosted by default.
		explicit RealmConnector(asio::io_service& io, TimerQueue& queue, const std::set<uint64>& defaultHostedMapIds, PlayerManager& playerManager, WorldInstanceManager& worldInstanceManager,
			const proto::Project& project);

		/// Default destructor.
		~RealmConnector() override;

	public:
		/// Sends a login request to the realm in order to authenticate this world node.
		bool Login(const std::string& serverAddress, uint16 port, const std::string& worldName, std::string password);

		/// Updates the list of map ids that can be hosted by this world node and if connected, propagates this
		///	list to the realm server.
		///	@param mapIds Set of map ids that can be hosted.
		void UpdateHostedMapList(const std::set<uint64>& mapIds);
		
		/// Notifies the realm that a world instance has been created.
		///	@param instanceId The id of the instance that has been created.
		void NotifyInstanceCreated(InstanceId instanceId);

		/// Notifies the realm that a world instance has been destroyed.
		///	@param instanceId The id of the instance that has been destroyed.
		void NotifyInstanceDestroyed(InstanceId instanceId);

		/// @brief Sends a proxy packet directly to the client with the given character guid.
		/// @param characterGuid 
		/// @param packetId 
		/// @param packetSize 
		/// @param packetContent 
		void SendProxyPacket(uint64 characterGuid, uint16 packetId, uint32 packetSize, const std::vector<char>& packetContent);

		void SendCharacterData(const GamePlayerS& character);

	private:
		/// Perform client-side srp6-a calculations after we received server values
		void DoSRP6ACalculation();

		/// Handles the LogonChallenge packet from the server.
		///	@param packet Incoming packet which contains the data sent by the realm.
		///	@returns Enum value which decides whether to continue the connection or destroy it.
		PacketParseResult OnLogonChallenge(auth::Protocol::IncomingPacket& packet);

		/// Handles the LogonProof packet from the server.
		///	@param packet Incoming packet which contains the data sent by the realm.
		///	@returns Enum value which decides whether to continue the connection or destroy it.
		PacketParseResult OnLogonProof(auth::IncomingPacket& packet);

		/// Handles a packet of a character that wants to enter a world.
		///	@param packet Incoming packet which contains the data sent by the realm.
		///	@returns Enum value which decides whether to continue the connection or destroy it.
		PacketParseResult OnPlayerCharacterJoin(auth::IncomingPacket& packet);

		/// Handles a packet of a character that should leave a world.
		///	@param packet Incoming packet which contains the data sent by the realm.
		///	@returns Enum value which decides whether to continue the connection or destroy it.
		PacketParseResult OnPlayerCharacterLeave(auth::IncomingPacket& packet);

		/// @brief Handles a proxy packet forwarded directly from the game client.
		/// @param packet The packet that has been forwarded.
		/// @return Enum value which decides whether to continue the connection or destroy it.
		PacketParseResult OnProxyPacket(auth::IncomingPacket& packet);

		PacketParseResult OnLocalChatMessage(auth::IncomingPacket& packet);

		/// Resets this instance to an unauthenticated state.
		void Reset();

		/// Handles login error results received by the realm server after a login attempt.
		///	@param result The error code received by the realm server.
		void OnLoginError(auth::AuthResult result);

		/// Adds a termination event to the global queue to terminate the server after a certain amount of time.
		void QueueReconnect();

		/// Sends the set of map ids that can be hosted to the realm server.
		void PropagateHostedMapIds();

	private:
		// Internal io service
		asio::io_service& m_ioService;
		TimerQueue& m_timerQueue;
		PlayerManager& m_playerManager;
		WorldInstanceManager& m_worldInstanceManager;
		
		std::string m_authName;

		// Server srp6 numbers
		BigNumber m_B;
		BigNumber m_s;
		BigNumber m_unk;

		// Client srp6 numbers
		BigNumber m_a;
		BigNumber m_x;
		BigNumber m_v;
		BigNumber m_u;
		BigNumber m_A;
		BigNumber m_S;

		// Session key
		BigNumber m_sessionKey;

		// Used for check
		SHA1Hash M1hash;
		SHA1Hash M2hash;

		/// A hash that is built by the salted password provided to the Connect method.
		SHA1Hash m_authHash;

		String m_realmAddress;
		uint16 m_realmPort;
		
		/// Whether the realm connector will request application termination due to wrong login requests at the
		/// realm server (termination is logical since we have to guess for wrong credentials, which can only be
		/// fixed after a server restart anyway).
		bool m_willReconnect;

		std::vector<uint64> m_hostedMapIds;

		const proto::Project& m_project;

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(auth::IncomingPacket& packet) override;
		// ~ End IConnectorListener
	};
}

