// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "player_manager.h"
#include "database.h"

#include "base/non_copyable.h"
#include "auth_protocol/auth_protocol.h"
#include "auth_protocol/auth_connection.h"
#include "base/signal.h"
#include "base/big_number.h"
#include "auth_protocol/srp_server.h"

#include <atomic>
#include <memory>
#include <optional>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include <cassert>


namespace mmo
{
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

		/// Disconnects the player if still connected.
		///
		/// May only be called on the connection's strand. Callers on any other thread must use
		/// PostKick instead.
		///
		/// @param reason Sent to the client before the connection closes so it can tell the player
		///	       why. Omit for teardowns that need no explanation (shutdown), where the client
		///	       showing a plain connection error is the honest outcome.
		void Kick(std::optional<auth::SessionKickReason> reason = std::nullopt);

		/// Requests a kick from any thread. The kick itself runs on the connection's strand.
		///
		/// Kick() tears down state that the connection's own handlers also touch, so calling it
		/// directly is only safe from the strand. Callers that are not on it -- the REST ban
		/// handler and the duplicate-login displacement, above all -- must come through here.
		void PostKick(std::optional<auth::SessionKickReason> reason = std::nullopt);

		/// Gets the player connection class used to send packets to the client.
		inline Client &GetConnection() { assert(m_connection); return *m_connection; }
		/// Gets the player manager which manages all connected players.
		inline PlayerManager &GetManager() const { return m_manager; }
		/// Determines whether the player is authentificated.
		/// @returns true if the player is authentificated.
		/// Determines whether the player has completed the SRP6 exchange.
		/// @returns true if a session key has been negotiated.
		///
		/// Deliberately not derived from m_sessionKey. This is read from other io threads while
		/// scanning the session list for an account, and BigNumber wraps an OpenSSL BIGNUM whose
		/// internals are being written on this session's own strand at the very moment a login
		/// completes -- reading it across threads is a race on the library's allocation, not on a
		/// scalar. A flag published once, after the key is in place, is safe to read from anywhere.
		inline bool IsAuthenticated() const { return m_authenticated.load(std::memory_order_acquire); }
		/// Gets the account name the player is logged in with.
		inline const std::string &GetAccountName() const { return m_accountName; }
		/// Gets the account id the player is logged in with. Read from other io threads, hence
		/// atomic: see IsAuthenticated.
		inline uint64 GetAccountId() const { return m_accountId.load(std::memory_order_relaxed); }
		/// Returns the client locale.
		inline const auth::AuthLocale &getLocale() const { return m_locale; }

	public:
		/// Marks this session as belonging to an account, without running the SRP exchange.
		///
		/// Exists for tests only. Everything that selects sessions by account -- displacement, the
		/// ban kick -- keys off IsAuthenticated() and GetAccountId(), and both are otherwise only
		/// reachable by driving a full logon challenge and proof against a database. Without this
		/// the tests could assert that nothing happens and never that the right thing does.
		void SetAuthenticatedForTest(const uint64 accountId)
		{
			m_accountId.store(accountId, std::memory_order_relaxed);
			m_authenticated.store(true, std::memory_order_release);
		}

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
		PlayerManager &m_manager;
		RealmManager &m_realmManager;
		std::shared_ptr<Client> m_connection;
		/// This session's view of the database. Held by value rather than by reference because it
		/// is not the server's shared instance: its results are bound to this connection's strand.
		/// See MakeStrandBoundDatabase. Declared after m_connection, which it is built from.
		AsyncDatabase m_database;
		std::string m_address;					// IP address in string format
		std::string m_accountName;				// Account name in uppercase letters
		auth::AuthLocale m_locale;				// Client language
		// Initialized because a session can be logged, kicked or torn down before the logon
		// challenge ever fills these in -- Kick() reads m_accountId unconditionally, which was an
		// uninitialized read that surfaced as a garbage account id in the shutdown logs.
		uint8 m_version1 = 0;					// Major version: X.0.0.00000
		uint8 m_version2 = 0;					// Minor version: 0.X.0.00000
		uint8 m_version3 = 0;					// Patch version: 0.0.X.00000
		uint16 m_build = 0;						// Build version: 0.0.0.XXXXX
		std::atomic<uint64> m_accountId{ 0 };	// Account ID (read cross-thread, see GetAccountId)
		// Published once the session key is in place. See IsAuthenticated for why this exists
		// rather than a check on m_sessionKey.
		std::atomic<bool> m_authenticated{ false };
		std::set<uint32> m_accountFeatureIds;	// Active account feature ids (loaded after login; used for realm visibility)
		std::map<uint8, PacketHandler> m_packetHandlers;
		std::mutex m_packetHandlerMutex;

		uint32 m_authProtocol = 0;
		uint32 m_gameProtocol = 0;

		/// Whether this session has already been torn down. Only ever touched on the connection's
		/// strand. See destroy() for why a second teardown is reachable.
		bool m_destroyed = false;

	private:
		BigNumber m_sessionKey;
		std::optional<SrpServer> m_srp;
		BigNumber m_reconnectProof;
		BigNumber m_reconnectKey;
		SHA1Hash m_m2;

		/// Number of bytes used to store the SRP salt (s).
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
		/// Tells the client why its session is about to be terminated. Must be sent before the
		/// connection is closed; the connection defers the actual shutdown until the write
		/// completes, so the client receives this ahead of the disconnect.
		void SendKickNotice(auth::SessionKickReason reason);
		void SendRealmList();
		/// Sends the active account feature keys (entitlements) to the client.
		void SendAccountFeatures(const std::vector<std::string>& featureKeys);

	private:

		/// Handles an incoming packet with packet id LogonChallenge.
		/// @param packet The packet data.
		PacketParseResult HandleLogonChallenge(auth::IncomingPacket &packet);
		/// Handles an incoming packet with packet id LogonProof.
		/// @param packet The packet data.
		PacketParseResult HandleLogonProof(auth::IncomingPacket &packet);
		/// Handles an incoming packet with packet id LogonChallenge.
		/// @param packet The packet data.
		PacketParseResult HandleReconnectChallenge(auth::IncomingPacket &packet);
		/// Handles an incoming packet with packet id LogonProof.
		/// @param packet The packet data.
		PacketParseResult HandleReconnectProof(auth::IncomingPacket &packet);
		/// Handles an incoming packet with packet id RealmList.
		/// @param packet The packet data.
		PacketParseResult OnRealmList(auth::IncomingPacket &packet);
	};
}
