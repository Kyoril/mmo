// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_connector.h"
#include "version.h"

#include "base/constants.h"
#include "log/default_log_levels.h"

namespace mmo
{
	LoginConnector::LoginConnector(asio::io_service & io)
		: auth::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
	{
	}

	bool LoginConnector::connectionEstablished(bool success)
	{
		if (success)
		{
			// Send the auth packet
			sendSinglePacket([&](auth::OutgoingPacket &packet)
			{
				// Initialize packet using the op code
				packet.Start(auth::client_packet::LogonChallenge);

				// Placeholder for packet size value
				const size_t contentSizePos = packet.sink().position();
				packet
					<< io::write<uint16>(0);

				// Write the actual packet content
				const size_t contentStart = packet.sink().position();
				packet
					<< io::write<uint8>(mmo::Major)	// Version
					<< io::write<uint8>(mmo::Minor)
					<< io::write<uint8>(mmo::Build)
					<< io::write<uint16>(mmo::Revisision)
					<< io::write<uint32>(0x00783836)	// Platform: x86
					<< io::write<uint32>(0x0057696e)	// System: Win
					<< io::write<uint32>(0x64654445)	// Locale: deDE
					<< io::write<uint32>(0)	// Timezone?
					<< io::write<uint32>(0)	// Ip
					<< io::write_dynamic_range<uint8>(m_username);

				// Calculate the actual packet size and write it at the beginning
				const size_t contentEnd = packet.sink().position();
				packet.writePOD(contentSizePos, static_cast<uint16>(contentEnd - contentStart));

				// Finish packet and send it
				packet.Finish();
			});
		}
		else
		{
			// Connection error!
			ELOG("Could not connect");
		}
		return true;
	}

	void LoginConnector::connectionLost()
	{
		ELOG("Disconnected");
	}

	void LoginConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
	}

	PacketParseResult LoginConnector::connectionPacketReceived(auth::IncomingPacket & packet)
	{
		switch (packet.GetId())
		{
		case auth::server_packet::LogonChallenge:
			OnLogonChallenge(packet);
			break;
		case auth::server_packet::LogonProof:
			OnLogonProof(packet);
			break;
		case auth::server_packet::RealmList:
			// TODO: Handle realm list packet
			break;
		default:
			WLOG("Received unhandled packet " << packet.GetId());
			break;
		}

		return PacketParseResult::Pass;
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
		gen.update((const char*)m_username.c_str(), m_username.size());
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

	void LoginConnector::OnLogonChallenge(auth::Protocol::IncomingPacket & packet)
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
			// Output error code
			ELOG("AUTH ERROR: " << static_cast<uint16>(result));
		}
	}

	void LoginConnector::OnLogonProof(auth::IncomingPacket & packet)
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
				// Success!
				ILOG("Success!");
			}
		}
		else
		{
			// Output error code
			ELOG("AUTH ERROR: " << static_cast<uint16>(result));
		}
	}

	void LoginConnector::Connect(const std::string & username, const std::string & password)
	{
		// Apply username and convert it to uppercase letters
		m_username = std::move(username);
		std::transform(m_username.begin(), m_username.end(), m_username.begin(), ::toupper);

		// Apply password in uppercase letters
		std::string upperPassword;
		std::transform(password.begin(), password.end(), std::back_inserter(upperPassword), ::toupper);

		// Calculate auth hash
		const std::string authHash = m_username + ":" + upperPassword;
		m_authHash = sha1(authHash.c_str(), authHash.size());

		// Connect to the server at localhost
		connect("mmo-dev.net", constants::DefaultLoginPlayerPort, *this, m_ioService);
	}
}
