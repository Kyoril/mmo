// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "player_manager.h"

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
	class RealmManager;


	/// This class represents a player connction on the login server.
	class Player final
		: public NonCopyable
		, public auth::IConnectionListener
		, public std::enable_shared_from_this<Player>
	{
	public:
		typedef AbstractConnection<auth::Protocol> Client;
		typedef std::function<PacketParseResult(auth::IncomingPacket &)> PacketHandler;

	public:
		explicit Player(
			PlayerManager &manager,
			RealmManager &realmManager,
			AsyncDatabase &database,
			std::shared_ptr<Client> connection,
			const std::string &address);

		/// Gets the player connection class used to send packets to the client.
		inline Client &getConnection() { assert(m_connection); return *m_connection; }
		/// Gets the player manager which manages all connected players.
		inline PlayerManager &getManager() const { return m_manager; }
		/// Determines whether the player is authentificated.
		/// @returns true if the player is authentificated.
		inline bool isAuthentificated() const { return false;/* (getSession() != nullptr);*/ }
		/// Gets the account name the player is logged in with.
		inline const std::string &getAccountName() const { return m_userName; }
		/// 
		inline uint32 getAccountId() const { return m_accountId; }
		/// Returns the client locale.
		inline const auth::AuthLocale &getLocale() const { return m_locale; }

	public:
		/// Registers a packet handler.
		void registerPacketHandler(uint8 opCode, PacketHandler &&handler);
		/// Clears a packet handler so that the opcode is no longer handled.
		void clearPacketHandler(uint8 opCode);

	private:
		PlayerManager &m_manager;
		RealmManager &m_realmManager;
		AsyncDatabase &m_database;
		std::shared_ptr<Client> m_connection;
		std::string m_address;						// IP address in string format
		std::string m_userName;						// Account name in uppercase letters
		auth::AuthPlatform m_platform;			// Client platform (32 Bit / 64 Bit)
		auth::AuthSystem m_system;				// User system (Windows, Mac)
		auth::AuthLocale m_locale;				// Client language
		uint8 m_version1;						// Major version: X.0.0.00000
		uint8 m_version2;						// Minor version: 0.X.0.00000
		uint8 m_version3;						// Patch version: 0.0.X.00000
		uint16 m_build;							// Build version: 0.0.0.XXXXX
		uint32 m_accountId;						// Account ID
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

		/// Number of bytes used to store m_s.
		static constexpr int ByteCountS = 32;
		/// Number of bytes used by a sha1 hash. Taken from OpenSSL.
		static constexpr int ShaDigestLength = 20;

	private:
		/// Closes the connection if still connected.
		void destroy();
		/// @copydoc wow::auth::IConnectionListener::connectionLost()
		void connectionLost() override;
		/// @copydoc wow::auth::IConnectionListener::connectionMalformedPacket()
		void connectionMalformedPacket() override;
		/// @copydoc wow::auth::IConnectionListener::connectionPacketReceived()
		PacketParseResult connectionPacketReceived(auth::IncomingPacket &packet) override;

	private:
		void SendAuthProof(auth::AuthResult result);
		void SendRealmList();

	private:

		/// Handles an incoming packet with packet id LogonChallenge.
		/// @param packet The packet data.
		PacketParseResult handleLogonChallenge(auth::IncomingPacket &packet);
		/// Handles an incoming packet with packet id LogonProof.
		/// @param packet The packet data.
		PacketParseResult handleLogonProof(auth::IncomingPacket &packet);
		/// Handles an incoming packet with packet id LogonChallenge.
		/// @param packet The packet data.
		PacketParseResult handleReconnectChallenge(auth::IncomingPacket &packet);
		/// Handles an incoming packet with packet id LogonProof.
		/// @param packet The packet data.
		PacketParseResult handleReconnectProof(auth::IncomingPacket &packet);
		/// Handles an incoming packet with packet id RealmList.
		/// @param packet The packet data.
		PacketParseResult handleRealmList(auth::IncomingPacket &packet);
	};
}