// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "realm_data.h"

#include "auth_protocol/auth_connector.h"
#include "base/big_number.h"
#include "base/sha1.h"
#include "base/signal.h"

#include "asio/io_service.hpp"

#include <mutex>


namespace mmo
{
	/// A simple test connector which will try to log in to the login server
	/// hosted at localhost.
	class LoginConnector final
		: public auth::Connector
		, public auth::IConnectorListener
	{
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
		std::string m_accountName;
		/// A hash that is built by the salted password provided to the Connect method.
		SHA1Hash m_authHash;

		/// Realm list infos.
		std::vector<RealmData> m_realms;

	public:
		/// Initializes a new instance of the TestConnector class.
		/// @param io The io service to be used in order to create the internal socket.
		explicit LoginConnector(asio::io_service &io);
		~LoginConnector() override;

	public:
		/// Gets realm data.
		const std::vector<RealmData>& GetRealms() const { return m_realms; }

		/// Gets the session key.
		const BigNumber& GetSessionKey() const { return m_sessionKey; }

		/// Gets the account name.
		const std::string& GetAccountName() const { return m_accountName; }

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(auth::IncomingPacket &packet) override;
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

		/// Sends a realm list request to the login server.
		void SendRealmListRequest();
	};
}

