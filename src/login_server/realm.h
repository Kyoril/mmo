// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "realm_manager.h"
#include "base/non_copyable.h"
#include "auth_protocol/auth_protocol.h"
#include "auth_protocol/auth_connection.h"
#include "base/signal.h"
#include "base/big_number.h"
#include <memory>
#include <functional>
#include <map>
#include <cassert>

namespace mmo
{
	class AsyncDatabase;

	/// This class represents a realm connction on the login server.
	class Realm final
		: public NonCopyable
		, public auth::IConnectionListener
		, public std::enable_shared_from_this<Realm>
	{
	public:
		typedef AbstractConnection<auth::Protocol> Client;
		typedef std::function<PacketParseResult(auth::IncomingPacket &)> PacketHandler;

	public:
		explicit Realm(
			RealmManager &manager,
			AsyncDatabase &database,
			std::shared_ptr<Client> connection,
			const std::string &address);

		/// Gets the player connection class used to send packets to the client.
		inline Client &GetConnection() { assert(m_connection); return *m_connection; }
		/// Gets the player manager which manages all connected players.
		inline RealmManager &GetManager() const { return m_manager; }
		/// Determines whether the realm is authentificated.
		/// @returns true if the realm is authentificated.
		inline bool IsAuthentificated() const { return m_authenticated; }
		/// Gets the name of the realm.
		inline const std::string &GetRealmName() const { return m_realmName; }
		/// 
		inline uint32 GetRealmId() const { return m_realmId; }
		/// Realm address string.
		inline const std::string& GetAddress() const { return m_address; }
		/// Gets the realm list address string.
		inline const std::string& GetRealmListAddress() const { return m_realmListAddress; }
		/// Gets the realm list port.
		inline uint16 GetRealmListPort() const { return m_realmListPort; }

	public:
		/// Registers a packet handler.
		void RegisterPacketHandler(uint8 opCode, PacketHandler &&handler);
		/// Syntactic sugar implementation of RegisterPacketHandler to avoid having to use std::bind.
		template <class Instance, class Class, class... Args1>
		void RegisterPacketHandler(uint8 opCode, Instance& object, PacketParseResult(Class::*method)(Args1...))
		{
			RegisterPacketHandler(opCode, [&object, method](Args1... args) {
				return (object.*method)(Args1(args)...);
			});
		}
		/// Clears a packet handler so that the opcode is no longer handled.
		void ClearPacketHandler(uint8 opCode);

	private:
		RealmManager &m_manager;
		AsyncDatabase &m_database;
		std::shared_ptr<Client> m_connection;
		std::string m_address;					// IP address of the realm server in string format
		std::string m_realmName;				// Realm name
		std::string m_realmListAddress;			// Address of the realm server which will appear in the realm list
		uint16 m_realmListPort;					// Port of the realm server which will appear in the realm list.
		uint8 m_version1;						// Major version: X.0.0.00000
		uint8 m_version2;						// Minor version: 0.X.0.00000
		uint8 m_version3;						// Patch version: 0.0.X.00000
		uint16 m_build;							// Build version: 0.0.0.XXXXX
		uint32 m_realmId;						// Realm ID
		std::map<uint8, PacketHandler> m_packetHandlers;
		std::mutex m_packetHandlerMutex;

	private:
		BigNumber m_sessionKey;
		BigNumber m_s, m_v;
		BigNumber m_b, m_B;
		BigNumber m_unk3;
		BigNumber m_reconnectProof;
		BigNumber m_reconnectKey;
		SHA1Hash m_m2;
		bool m_authenticated;

		/// Number of bytes used to store m_s.
		static constexpr int ByteCountS = 32;
		/// Number of bytes used by a sha1 hash. Taken from OpenSSL.
		static constexpr int ShaDigestLength = 20;

	private:
		/// Closes the connection if still connected.
		void Destroy();
		/// @copydoc wow::auth::IConnectionListener::connectionLost()
		void connectionLost() override;
		/// @copydoc wow::auth::IConnectionListener::connectionMalformedPacket()
		void connectionMalformedPacket() override;
		/// @copydoc wow::auth::IConnectionListener::connectionPacketReceived()
		PacketParseResult connectionPacketReceived(auth::IncomingPacket &packet) override;

	private:
		/// Send auth proof result to the realm server.
		void SendAuthProof(auth::AuthResult result);
		/// Send the auth session result back to the realm server.
		void SendAuthSessionResult(uint64 requestId, auth::AuthResult result, uint64 accountId, BigNumber sessionKey);

	private:

		/// Handles an incoming packet with packet id LogonChallenge.
		/// @param packet The packet data.
		PacketParseResult HandleLogonChallenge(auth::IncomingPacket &packet);
		/// Handles an incoming packet with packet id LogonProof.
		/// @param packet The packet data.
		PacketParseResult HandleLogonProof(auth::IncomingPacket &packet);
		/// Handles incoming ClientAuthSession packets from a realm server.
		PacketParseResult OnClientAuthSession(auth::IncomingPacket &packet);
	};
}