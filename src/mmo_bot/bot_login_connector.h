// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "mmo_client/realm_data.h"

#include "auth_protocol/auth_connector.h"
#include "base/big_number.h"
#include "base/sha1.h"
#include "base/signal.h"
#include "base/constants.h"
#include "game_protocol/game_protocol.h"

#include "asio/io_service.hpp"

namespace mmo
{
	/// Lightweight login connector variant for the bot application.
	class BotLoginConnector final
		: public auth::Connector
		, public auth::IConnectorListener
	{
	public:
		/// Fired when authentication finished.
		signal<void(auth::AuthResult)> AuthenticationResult;
		/// Fired when the realm list was received.
		signal<void()> RealmListUpdated;

	private:
		asio::io_service& m_ioService;
		std::string m_host;
		uint16 m_port;

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
		BotLoginConnector(asio::io_service& io, std::string host, uint16 port = constants::DefaultLoginPlayerPort);
		~BotLoginConnector() override = default;

		/// Gets realm data.
		const std::vector<RealmData>& GetRealms() const { return m_realms; }

		/// Gets the session key.
		const BigNumber& GetSessionKey() const { return m_sessionKey; }

		/// Gets the account name.
		const std::string& GetAccountName() const { return m_accountName; }

		/// Updates the realmlist host/port.
		void SetRealmlist(std::string host, uint16 port);

		/// Tries to connect to the login server and authenticate using the given credentials.
		void Connect(const std::string& username, const std::string& password);

		/// Sends a realm list request to the login server.
		void SendRealmListRequest();

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(auth::IncomingPacket& packet) override;
		// ~ End IConnectorListener

	private:
		// Perform client-side srp6-a calculations after we received server values
		void DoSRP6ACalculation();

		// Handles the LogonChallenge packet from the server.
		PacketParseResult OnLogonChallenge(auth::Protocol::IncomingPacket& packet);

		// Handles the LogonProof packet from the server.
		PacketParseResult OnLogonProof(auth::IncomingPacket& packet);

		/// Handles the RealmList packet from the login server.
		PacketParseResult OnRealmList(auth::IncomingPacket& packet);
	};
}
