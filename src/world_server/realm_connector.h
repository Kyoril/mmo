// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "auth_protocol/auth_connector.h"
#include "base/big_number.h"
#include "base/sha1.h"

#include "asio/io_service.hpp"

#include <set>
#include <vector>

namespace mmo
{
	class TimerQueue;
	class WorldInstanceManager;

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
		explicit RealmConnector(asio::io_service& io, TimerQueue& queue, const std::set<uint64>& defaultHostedMapIds, WorldInstanceManager& worldInstanceManager);
		/// Default destructor.
		virtual ~RealmConnector();

	public:
		bool Login(const std::string& serverAddress, uint16 port, const std::string& worldName, std::string password);

		void UpdateHostedMapList(const std::set<uint64>& mapIds);
		
	private:
		// Perform client-side srp6-a calculations after we received server values
		void DoSRP6ACalculation();
		// Handles the LogonChallenge packet from the server.
		PacketParseResult OnLogonChallenge(auth::Protocol::IncomingPacket& packet);
		// Handles the LogonProof packet from the server.
		PacketParseResult OnLogonProof(auth::IncomingPacket& packet);
		/// Handles a packet of a character that wants to enter a world.
		PacketParseResult OnPlayerCharacterJoin(auth::IncomingPacket& packet);
		/// Handles a packet of a character that should leave a world.
		PacketParseResult OnPlayerCharacterLeave(auth::IncomingPacket& packet);

		void Reset();
		
		void OnLoginError(auth::AuthResult result);

		void QueueTermination();

		void PropagateHostedMapIds();

	private:
		// Internal io service
		asio::io_service& m_ioService;
		TimerQueue& m_timerQueue;
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
		bool m_willTerminate;

		std::vector<uint64> m_hostedMapIds;

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(auth::IncomingPacket& packet) override;
		// ~ End IConnectorListener
	};
}

