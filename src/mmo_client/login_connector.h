// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "auth_protocol/auth_connector.h"
#include "base/big_number.h"
#include "base/sha1.h"
#include "base/signal.h"

#include "asio/io_service.hpp"

#include <map>
#include <functional>
#include <mutex>


namespace mmo
{
	/// Contains realm informations.
	struct RealmData final
	{
		/// Unique id of a realm.
		uint32 id;
		/// Realm display name.
		std::string name;
		/// Realm address (ip).
		std::string address;
	};

	/// A simple test connector which will try to log in to the login server
	/// hosted at localhost.
	class LoginConnector final
		: public auth::Connector
		, public auth::IConnectorListener
	{
	public:
		typedef std::function<PacketParseResult(auth::IncomingPacket &)> PacketHandler;

	public:
		/// Signal that is fired when the client successfully authenticated at the realm list.
		signal<void(auth::AuthResult)> AuthenticationResult;
		/// Signal that is fired when the client received a realm list packet.
		signal<void()> RealmListUpdated;

	private:
		// Internal io service
		asio::io_service& m_ioService;

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

		/// Username provided to the Connect method.
		std::string m_username;
		/// A hash that is built by the salted password provided to the Connect method.
		SHA1Hash m_authHash;

		/// Active packet handler instances.
		std::map<uint8, PacketHandler> m_packetHandlers;
		std::mutex m_packetHandlerMutex;

		/// Realm list infos.
		std::vector<RealmData> m_realms;

	public:
		/// Initializes a new instance of the TestConnector class.
		/// @param io The io service to be used in order to create the internal socket.
		explicit LoginConnector(asio::io_service &io);
		~LoginConnector();

	public:
		/// Registers a packet handler for a given op code.
		void RegisterPacketHandler(auth::server_packet::Type opCode, PacketHandler handler);
		/// Removes a registered packet handler for a given op code.
		void ClearPacketHandler(auth::server_packet::Type opCode);
		/// Removes all registered packet handlers.
		void ClearPacketHandlers();

	public:
		/// Get realm data.
		inline const std::vector<RealmData>& GetRealms() const { return m_realms; }
		/// Get the session key.
		inline const BigNumber& GetSessionKey() const { return m_sessionKey; }

	public:
		// ~ Begin IConnectorListener
		virtual bool connectionEstablished(bool success) override;
		virtual void connectionLost() override;
		virtual void connectionMalformedPacket() override;
		virtual PacketParseResult connectionPacketReceived(auth::IncomingPacket &packet) override;
		// ~ End IConnectorListener

	private:
		// Perform client-side srp6-a calculations after we received server values
		void DoSRP6ACalculation();
		// Handles the LogonChallenge packet from the server.
		PacketParseResult OnLogonChallenge(auth::Protocol::IncomingPacket &packet);
		// Handles the LogonProof packet from the server.
		PacketParseResult OnLogonProof(auth::IncomingPacket &packet);
		/// Handles the RealmList packet from the login server.
		PacketParseResult OnRealmList(auth::IncomingPacket &packet);

	public:
		/// Tries to connect to the default login server. After a connection has been established,
		/// the login process is started using the given credentials.
		/// @param username The username to login with.
		/// @param password The password to login with.
		/// @param ioService The io service used to connect.
		void Connect(const std::string& username, const std::string& password);
	};
}

