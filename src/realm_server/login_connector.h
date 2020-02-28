// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "auth_protocol/auth_connector.h"
#include "base/big_number.h"
#include "base/sha1.h"
#include "base/id_generator.h"

#include "asio/io_service.hpp"

#include <functional>


namespace mmo
{
	class TimerQueue;
	

	/// A simple test connector which will try to log in to the login server
	/// hosted at localhost.
	class LoginConnector
		: public auth::Connector
		, public auth::IConnectorListener
	{
	public:
		/// Callback for ClientAuthSession results.
		typedef std::function<void(bool success, const BigNumber& sessionKey)> ClientAuthSessionCallback;

	private:
		/// Contains data passed by a ClientAuthSession packet.
		struct ClientAuthSessionRequest final
		{
			/// Requested account name.
			std::string accountName;
			/// Requested client seed.
			uint32 clientSeed;
			/// Requested server seed.
			uint32 serverSeed;
			/// Requested client hash for verification.
			SHA1Hash clientHash;
			/// Callback on success.
			ClientAuthSessionCallback callback;
		};

	private:
		// Internal io service
		asio::io_service& m_ioService;
		TimerQueue& m_timerQueue;

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
		std::string m_realmName;
		/// A hash that is built by the salted password provided to the Connect method.
		SHA1Hash m_authHash;

		/// Ip address of the login server. Stored for automatic reconnection attempts.
		std::string m_loginAddress;
		/// Port of the login server. Stored for automatic reconnection attempts.
		uint16 m_loginPort;
		/// Whether the login connector will request application termination due to wrong login requests at the
		/// login server (termination is logical since we have to guess for wrong credentials, which can only be
		/// fixed after a server restart anyway).
		bool m_willTerminate;

		/// Generator for client auth session request ids.
		IdGenerator<uint64> m_clientAuthSessionReqIdGen;
		/// Pending client auth session requests by id.
		std::map<uint64, ClientAuthSessionRequest> m_pendingClientAuthSessionReqs;
		/// Mutex for accessing m_pendingClientAuthSessionReqs
		std::mutex m_authSessionReqMutex;

	public:
		/// Initializes a new instance of the TestConnector class.
		/// @param io The io service to be used in order to create the internal socket.
		explicit LoginConnector(asio::io_service &io, TimerQueue& queue);

	public:
		// ~ Begin IConnectorListener
		virtual bool connectionEstablished(bool success) override;
		virtual void connectionLost() override;
		virtual void connectionMalformedPacket() override;
		virtual PacketParseResult connectionPacketReceived(auth::IncomingPacket &packet) override;
		// ~ End IConnectorListener

	public:
		/// Queues a client auth session request for the login connector and waits for response from a login server.
		/// @returns false if the request couldn't be queued.
		bool QueueClientAuthSession(const std::string& accountName, uint32 clientSeed, uint32 serverSeed, const SHA1Hash& clientHash, ClientAuthSessionCallback callback);

	private:
		// Perform client-side srp6-a calculations after we received server values
		void DoSRP6ACalculation();
		/// Called if a login error happened.
		void OnLoginError(auth::AuthResult result);
		/// Resets the authentication status to the initial state.
		void Reset();
		/// Queues reconnection event to the timer queue.
		void QueueReconnect();
		/// Executed when the reconnect timer triggers.
		void OnReconnectTimer();
		/// Queues app termination in case of errors.
		void QueueTermination();

	private:
		// Handles the LogonChallenge packet from the server.
		PacketParseResult OnLogonChallenge(auth::Protocol::IncomingPacket &packet);
		// Handles the LogonProof packet from the server.
		PacketParseResult OnLogonProof(auth::IncomingPacket &packet);
		// Handles the ClientAuthSessionResponse packet from the login server.
		PacketParseResult OnClientAuthSessionResponse(auth::IncomingPacket &packet);

	public:
		/// Tries to connect to the default login server. After a connection has been established,
		/// the login process is started using the given credentials.
		/// @param username The username to login with.
		/// @param password The password to login with.
		/// @param ioService The io service used to connect.
		bool Login(const std::string& serverAddress, uint16 port, const std::string& username, std::string password);
	};
}

