// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_connector.h"
#include "version.h"

#include "base/constants.h"
#include "base/timer_queue.h"
#include "base/clock.h"
#include "log/default_log_levels.h"

namespace mmo
{
	LoginConnector::LoginConnector(asio::io_service & io, TimerQueue& queue)
		: auth::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
		, m_timerQueue(queue)
		, m_willTerminate(false)
	{
	}

	bool LoginConnector::connectionEstablished(bool success)
	{
		if (success)
		{
			// Register default packet handler
			RegisterPacketHandler(mmo::auth::server_packet::LogonChallenge, *this, &LoginConnector::OnLogonChallenge);

			// Send the auth packet
			sendSinglePacket([&](auth::OutgoingPacket &packet)
			{
				// Initialize packet using the op code
				packet.Start(auth::client_packet::LogonChallenge);

				// Write the actual packet content
				const size_t contentStart = packet.sink().position();
				packet
					<< io::write<uint8>(mmo::Major)	// Version
					<< io::write<uint8>(mmo::Minor)
					<< io::write<uint8>(mmo::Build)
					<< io::write<uint16>(mmo::Revisision)
					<< io::write_dynamic_range<uint8>(m_realmName);

				// Finish packet and send it
				packet.Finish();
			});
		}
		else
		{
			// Connection error!
			ELOG("Could not connect to the login server at " << m_loginAddress << ":" << m_loginPort << 
				"! Will try to reconnect in a few seconds...");
			QueueReconnect();
		}

		return true;
	}

	void LoginConnector::connectionLost()
	{
		// Notify the user
		ELOG("Connection to the login server has been lost!");

		// Reset authentication status
		Reset();

		// Queue reconnect timer
		QueueReconnect();
	}

	void LoginConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet from login server!");
		QueueTermination();
	}

	PacketParseResult LoginConnector::connectionPacketReceived(auth::IncomingPacket & packet)
	{
		return HandleIncomingPacket(packet);
	}

	void LoginConnector::DoSRP6ACalculation()
	{
		// Generate a
		m_a.setRand(19 * 8);
		assert(m_a.asUInt32() > 0);

		// Hash generator
		HashGeneratorSha1 gen;

		// Calculate x
		gen.update((const char*)m_s.asByteArray().data(), m_s.getNumBytes());
		gen.update((const char*)m_authHash.data(), m_authHash.size());
		SHA1Hash xHash = gen.finalize();
		m_x.setBinary(xHash.data(), xHash.size());

		// Calculate v
		m_v = constants::srp::g.modExp(m_x, constants::srp::N);

		// Calculate A
		m_A = constants::srp::g.modExp(m_a, constants::srp::N);

		// Calculate u
		SHA1Hash uHash = Sha1_BigNumbers({ m_A, m_B });
		m_u.setBinary(uHash.data(), uHash.size());

		// Calcualte S
		BigNumber k{ 3 };
		m_S = (m_B - k * constants::srp::g.modExp(m_x, constants::srp::N)).modExp((m_a + m_u * m_x), constants::srp::N);
		assert(m_S.asUInt32() > 0);

		// Calculate proof hashes M1 (client) and M2 (server)
		
		// Split it into 2 seperate strings, interleaved
		// Leave space for terminating 0
		char S1[16 + 1], S2[16 + 1];
		auto arrS = m_S.asByteArray(32);
		for (uint32 i = 0; i < 16; i++) 
		{
			S1[i] = arrS[i * 2];
			S2[i] = arrS[i * 2 + 1];
		}

		// Calculate the hash for each string
		gen.update((const char*)S1, 16);
		SHA1Hash S1hash = gen.finalize();
		gen.update((const char*)S2, 16);
		SHA1Hash S2hash = gen.finalize();

		// Re-combine them to form the session key
		uint8 S_hash[40];
		for (uint32 i = 0; i < 20; i++) 
		{
			S_hash[i * 2] = S1hash[i];
			S_hash[i * 2 + 1] = S2hash[i];
		}

		// Store the session key as BigNumber so that we can use it for 
		// calculations later on.
		m_sessionKey.setBinary((uint8*)S_hash, 40);
		
		// Generate hash of plain username
		gen.update((const char*)m_realmName.c_str(), m_realmName.size());
		SHA1Hash userhash2 = gen.finalize();

		// Generate N and g hashes
		SHA1Hash Nhash = Sha1_BigNumbers({ constants::srp::N });
		SHA1Hash ghash = Sha1_BigNumbers({ constants::srp::g });

		// Combine N and g hash like this: (N ^ g)
		uint8 Ng_hash[20];
		for (uint32 i = 0; i < 20; i++) Ng_hash[i] = Nhash[i] ^ ghash[i];

		// Convert hashes into bignumbers so we can calculate easier
		BigNumber t_acc{ userhash2.data(), userhash2.size() };
		BigNumber t_Ng_hash{ Ng_hash, 20 };

		// Caluclate M1 hash sent to the server
		Sha1_Add_BigNumbers(gen, { t_Ng_hash, t_acc, m_s, m_A, m_B});
		gen.update((const char*)S_hash, 40);
		M1hash = gen.finalize();

		// Calculate M2 hash to store for later comparison on server answer
		Sha1_Add_BigNumbers(gen, { m_A });
		gen.update((const char*)M1hash.data(), M1hash.size());
		gen.update((const char*)S_hash, 40);
		M2hash = gen.finalize();
	}

	PacketParseResult LoginConnector::OnLogonChallenge(auth::Protocol::IncomingPacket & packet)
	{
		// Read the response code
		uint8 result = 0;
		packet >> io::read<uint8>(result);

		// If it was successful, read additional server data
		if (result == auth::auth_result::Success)
		{
			// Read B number
			std::array<uint8, 32> B;
			packet >> io::read_range(B);
			m_B.setBinary(B.data(), B.size());

			// Read and verify g
			uint8 g = 0;
			packet >> io::read<uint8>(g);
			assert(g == constants::srp::g.asUInt32());

			// Read and verify N
			std::array<uint8, 32> N;
			packet >> io::read_range(N);
			BigNumber numN{ N.data(), N.size() };
			assert(numN == constants::srp::N);

			// Read s (salt)
			std::array<uint8, 32> s;
			packet >> io::read_range(s);
			m_s.setBinary(s.data(), s.size());

			// Do srp6a calculations
			DoSRP6ACalculation();

			RegisterPacketHandler(auth::server_packet::LogonProof, *this, &LoginConnector::OnLogonProof);

			// Send response packet
			sendSinglePacket([&](auth::OutgoingPacket &packet)
			{
				// Proof packet contains only A and M1 hash value
				packet.Start(auth::client_packet::LogonProof);
				packet << io::write_range(m_A.asByteArray());
				packet << io::write_range(M1hash);
				packet.Finish();
			});
		}
		else
		{
			OnLoginError(static_cast<auth::AuthResult>(result));
			return PacketParseResult::Disconnect;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult LoginConnector::OnLogonProof(auth::IncomingPacket & packet)
	{
		// Read the response code
		uint8 result = 0;
		packet >> io::read<uint8>(result);

		// If the response is "Success"...
		if (result == auth::auth_result::Success)
		{
			// Read server-calculated M2 hash for comparison
			SHA1Hash serverM2;
			packet >> io::read_range(serverM2);

			// Check that both match
			if (std::equal(M2hash.begin(), M2hash.end(), serverM2.begin()))
			{
				ILOG("Successfully authenticated at the login server! Players should now be ready to play on this realm!");
			}
			else
			{
				// Output error code in chat before terminating the application
				ELOG("[Login Server] Could not authenticate realm at login server, hash mismatch detected!");
				QueueTermination();

				return PacketParseResult::Disconnect;
			}
		}
		else
		{
			OnLoginError(static_cast<auth::AuthResult>(result));
			return PacketParseResult::Disconnect;
		}

		return PacketParseResult::Pass;
	}

	void LoginConnector::OnLoginError(auth::AuthResult result)
	{
		// Output error code in chat before terminating the application
		ELOG("[Login Server] Could not authenticate realm at login server. Error code 0x" << std::hex << static_cast<uint16>(result));
		QueueTermination();
	}

	void LoginConnector::Reset()
	{
		// Reset srp6a values
		m_B = 0;
		m_s = 0;
		m_unk = 0;
		m_a = 0;
		m_x = 0;
		m_v = 0;
		m_u = 0;
		m_A = 0;
		m_S = 0;

		// Reset session key
		m_sessionKey = 0;

		// Reset calculated hash values
		M1hash.fill(0);
		M2hash.fill(0);
	}

	void LoginConnector::QueueReconnect()
	{
		if (!m_willTerminate)
		{
			// Reconnect in 5 seconds from now on
			m_timerQueue.AddEvent(std::bind(&LoginConnector::OnReconnectTimer, this), m_timerQueue.GetNow() + constants::OneSecond * 5);
		}
	}

	void LoginConnector::OnReconnectTimer()
	{
		if (!m_willTerminate)
		{
			// Try to connect, everything in terms of authentication is handled in the connectionEstablished event
			connect(m_loginAddress, m_loginPort, *this, m_ioService);
		}
	}

	void LoginConnector::QueueTermination()
	{
		// Prevent double timer
		if (m_willTerminate)
		{
			return;
		}

		// Termination callback
		const auto termination = [this]() {
			// TODO: might need to change this to a global event to trigger instead in case we have more than one major io service object
			m_ioService.stop();
		};

		// Notify the user
		WLOG("Server will terminate in 5 seconds...");
		m_timerQueue.AddEvent(std::move(termination), m_timerQueue.GetNow() + constants::OneSecond * 5);
	}

	bool LoginConnector::Login(const std::string& serverAddress, uint16 port, const std::string & realmName, std::string password)
	{
		// Reset authentication status
		Reset();

		// Copy data for later use in reconnect timer
		m_loginAddress = serverAddress;
		m_loginPort = port;

		// Apply username and convert it to uppercase letters
		m_realmName = std::move(realmName);
		std::transform(m_realmName.begin(), m_realmName.end(), m_realmName.begin(), ::toupper);

		// Calculate auth hash
		bool hexParseError = false;
		m_authHash = sha1ParseHex(password, &hexParseError);

		// Check for errors
		if (hexParseError)
		{
			ELOG("Invalid realm password hash string provided! SHA1 hashes are represented by a 20-character hex string!");
			return false;
		}

		// Connect to the server at localhost
		connect(serverAddress, port, *this, m_ioService);
		return true;
	}
}
