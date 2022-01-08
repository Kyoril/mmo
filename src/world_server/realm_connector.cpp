// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "realm_connector.h"
#include "game/world_instance_manager.h"
#include "game/world_instance.h"
#include "version.h"

#include "base/utilities.h"
#include "base/clock.h"
#include "base/constants.h"
#include "base/timer_queue.h"
#include "game/character_data.h"
#include "log/default_log_levels.h"


namespace mmo
{
	RealmConnector::RealmConnector(asio::io_service& io, TimerQueue& queue, const std::set<uint64>& defaultHostedMapIds, WorldInstanceManager& worldInstanceManager)
		: auth::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
		, m_timerQueue(queue)
		, m_worldInstanceManager(worldInstanceManager)
		, m_willTerminate(false)
	{
		UpdateHostedMapList(defaultHostedMapIds);

		m_worldInstanceManager.instanceCreated += [&](const InstanceId id)
		{
			NotifyInstanceCreated(id);
		};
		m_worldInstanceManager.instanceDestroyed += [&](const InstanceId id)
		{
			NotifyInstanceDestroyed(id);
		};
	}

	RealmConnector::~RealmConnector() = default;

	void RealmConnector::Reset()
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
	
	void RealmConnector::DoSRP6ACalculation()
	{
		// Generate a
		m_a.setRand(19 * 8);
		assert(m_a.asUInt32() > 0);

		// Hash generator
		HashGeneratorSha1 gen;

		// Calculate x
		gen.update(reinterpret_cast<const char*>(m_s.asByteArray().data()), m_s.getNumBytes());
		gen.update(reinterpret_cast<const char*>(m_authHash.data()), m_authHash.size());
		SHA1Hash xHash = gen.finalize();
		m_x.setBinary(xHash.data(), xHash.size());

		// Calculate v
		m_v = constants::srp::g.modExp(m_x, constants::srp::N);

		// Calculate A
		m_A = constants::srp::g.modExp(m_a, constants::srp::N);

		// Calculate u
		SHA1Hash uHash = Sha1_BigNumbers({ m_A, m_B });
		m_u.setBinary(uHash.data(), uHash.size());

		// Calculate S
		BigNumber k{ 3 };
		m_S = (m_B - k * constants::srp::g.modExp(m_x, constants::srp::N)).modExp((m_a + m_u * m_x), constants::srp::N);
		assert(m_S.asUInt32() > 0);

		// Calculate proof hashes M1 (client) and M2 (server)

		// Split it into 2 separate strings, interleaved
		// Leave space for terminating 0
		char S1[16 + 1], S2[16 + 1];
		auto arrS = m_S.asByteArray(32);
		for (uint32 i = 0; i < 16; i++)
		{
			S1[i] = arrS[i * 2];
			S2[i] = arrS[i * 2 + 1];
		}

		// Calculate the hash for each string
		gen.update(static_cast<const char*>(S1), 16);
		SHA1Hash S1hash = gen.finalize();
		gen.update(static_cast<const char*>(S2), 16);
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
		m_sessionKey.setBinary(static_cast<uint8*>(S_hash), 40);

		// Generate hash of plain username
		gen.update(static_cast<const char*>(m_authName.c_str()), m_authName.size());
		const auto userhash2 = gen.finalize();

		// Generate N and g hashes
		const auto Nhash = Sha1_BigNumbers({ constants::srp::N });
		const auto ghash = Sha1_BigNumbers({ constants::srp::g });

		// Combine N and g hash like this: (N ^ g)
		uint8 Ng_hash[20];
		for (uint32 i = 0; i < 20; i++) Ng_hash[i] = Nhash[i] ^ ghash[i];

		// Convert hashes into big numbers so we can calculate easier
		BigNumber t_acc{ userhash2.data(), userhash2.size() };
		BigNumber t_Ng_hash{ Ng_hash, 20 };

		// Caluclate M1 hash sent to the server
		Sha1_Add_BigNumbers(gen, { t_Ng_hash, t_acc, m_s, m_A, m_B });
		gen.update(reinterpret_cast<const char*>(S_hash), 40);
		M1hash = gen.finalize();

		// Calculate M2 hash to store for later comparison on server answer
		Sha1_Add_BigNumbers(gen, { m_A });
		gen.update(reinterpret_cast<const char*>(M1hash.data()), M1hash.size());
		gen.update(reinterpret_cast<const char*>(S_hash), 40);
		M2hash = gen.finalize();
	}

	bool RealmConnector::Login(const std::string& serverAddress, uint16 port, const std::string& worldName, std::string password)
	{
		// Reset authentication status
		Reset();

		// Copy data for later use in reconnect timer
		m_realmAddress = serverAddress;
		m_realmPort = port;

		// Apply username and convert it to uppercase letters
		m_authName = worldName;
		std::transform(m_authName.begin(), m_authName.end(), m_authName.begin(), ::toupper);

		// Calculate auth hash
		bool hexParseError = false;
		m_authHash = sha1ParseHex(password, &hexParseError);

		// Check for errors
		if (hexParseError)
		{
			ELOG("Invalid world password hash string provided! SHA1 hashes are represented by a 20-character hex string!");
			return false;
		}

		// Connect to the server at localhost
		connect(serverAddress, port, *this, m_ioService);
		return true;
	}
	
	PacketParseResult RealmConnector::OnLogonChallenge(auth::Protocol::IncomingPacket& packet)
	{
		ClearPacketHandler(auth::realm_world_packet::LogonChallenge);

		// Read the response code
		uint8 result = 0;
		packet >> io::read<uint8>(result);

		// If it was successful, read additional server data
		if (result == auth::auth_result::Success)
		{
			// Read B number
			std::array<uint8, 32> B{};
			packet >> io::read_range(B);
			m_B.setBinary(B.data(), B.size());

			// Read and verify g
			uint8 g = 0;
			packet >> io::read<uint8>(g);
			ASSERT(g == constants::srp::g.asUInt32());

			// Read and verify N
			std::array<uint8, 32> N{};
			packet >> io::read_range(N);
			BigNumber numN{ N.data(), N.size() };
			ASSERT(numN == constants::srp::N);

			// Read s (salt)
			std::array<uint8, 32> s{};
			packet >> io::read_range(s);
			m_s.setBinary(s.data(), s.size());

			// Do srp6a calculations
			DoSRP6ACalculation();

			// Accept LogonProof packets from the login server from here on
			RegisterPacketHandler(auth::realm_world_packet::LogonProof, *this, &RealmConnector::OnLogonProof);

			// Send response packet
			sendSinglePacket([&](auth::OutgoingPacket& outPacket)
			{
				// Proof packet contains only A and M1 hash value
				outPacket.Start(auth::world_realm_packet::LogonProof);
				outPacket << io::write_range(m_A.asByteArray());
				outPacket << io::write_range(M1hash);
				outPacket.Finish();
			});
		}
		else
		{
			OnLoginError(static_cast<auth::AuthResult>(result));
			return PacketParseResult::Disconnect;
		}

		return PacketParseResult::Pass;
	}

	void RealmConnector::OnLoginError(auth::AuthResult result)
	{
		// Output error code in chat before terminating the application
		ELOG("[Realm Server] Could not authenticate world at realm server. Error code 0x" << std::hex << static_cast<uint16>(result));
		QueueTermination();
	}

	void RealmConnector::QueueTermination()
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
		m_timerQueue.AddEvent(termination, m_timerQueue.GetNow() + constants::OneSecond * 5);
	}

	void RealmConnector::PropagateHostedMapIds()
	{
		sendSinglePacket([&](auth::OutgoingPacket& outPacket)
		{
			// Proof packet contains only A and M1 hash value
			outPacket.Start(auth::world_realm_packet::PropagateMapList);
			outPacket << io::write_dynamic_range<uint16>(m_hostedMapIds);
			outPacket.Finish();
		});
	}

	void RealmConnector::UpdateHostedMapList(const std::set<uint64>& mapIds)
	{
		m_hostedMapIds.clear();
		std::copy(mapIds.begin(), mapIds.end(), std::back_inserter(m_hostedMapIds));

		if (!m_sessionKey.isZero())
		{
			PropagateHostedMapIds();
		}
	}

	void RealmConnector::NotifyInstanceCreated(InstanceId instanceId)
	{
		sendSinglePacket([instanceId](auth::OutgoingPacket& outPacket)
		{
			outPacket.Start(auth::world_realm_packet::InstanceCreated);
			outPacket << instanceId;
			outPacket.Finish();
		});
	}

	void RealmConnector::NotifyInstanceDestroyed(InstanceId instanceId)
	{
		sendSinglePacket([instanceId](auth::OutgoingPacket& outPacket)
		{
			outPacket.Start(auth::world_realm_packet::InstanceDestroyed);
			outPacket << instanceId;
			outPacket.Finish();
		});
	}

	PacketParseResult RealmConnector::OnLogonProof(auth::IncomingPacket& packet)
	{
		ClearPacketHandler(auth::realm_world_packet::LogonProof);

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
				ILOG("Successfully authenticated at the realm server! Players should now be ready to play on this world node!");
				RegisterPacketHandler(auth::realm_world_packet::PlayerCharacterJoin, *this, &RealmConnector::OnPlayerCharacterJoin);
				RegisterPacketHandler(auth::realm_world_packet::PlayerCharacterLeave, *this, &RealmConnector::OnPlayerCharacterLeave);
				
				PropagateHostedMapIds();
			}
			else
			{
				// Output error code in chat before terminating the application
				ELOG("[Login Server] Could not authenticate world at realm server, hash mismatch detected!");
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
	
	PacketParseResult RealmConnector::OnPlayerCharacterJoin(auth::IncomingPacket& packet)
	{
		// Read packet data
		CharacterData characterData;
		if (!(packet >> characterData))
		{
			return PacketParseResult::Disconnect;
		}
		
		DLOG("Player character " << log_hex_digit(characterData.characterId) << " wants to join world...");
		
		WorldInstance* instance = nullptr;
		if (characterData.instanceId.is_nil())
		{
			instance = m_worldInstanceManager.GetInstanceByMap(characterData.mapId);
		}
		else
		{
			instance = m_worldInstanceManager.GetInstanceById(characterData.instanceId);
			if (!instance)
			{
				// TODO: Try to load instance id from instance storage
				WLOG("Unable to find world instance by id " << characterData.instanceId);
				instance = m_worldInstanceManager.GetInstanceByMap(characterData.mapId);
			}
		}

		if (!instance)
		{
			DLOG("Unable to find any world instance for map id " << log_hex_digit(characterData.mapId) << ": Creating new one");
			instance = &m_worldInstanceManager.CreateInstance(characterData.mapId);
			ASSERT(instance);
		}

		// Apply instance id before sending
		characterData.instanceId = instance->GetId();

		// TODO: Create game object from character data and spawn in world

		// For now just tell the realm server that we joined
		sendSinglePacket([&characterData](auth::OutgoingPacket& outPacket)
		{
			outPacket.Start(auth::world_realm_packet::PlayerCharacterJoined);
			outPacket << io::write_packed_guid(characterData.characterId) << characterData.instanceId;
			outPacket.Finish();
		});
		
		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnPlayerCharacterLeave(auth::IncomingPacket& packet)
	{
		DLOG("Player character should leave world...");
		
		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::connectionPacketReceived(auth::IncomingPacket& packet)
	{
		return HandleIncomingPacket(packet);
	}

	bool RealmConnector::connectionEstablished(const bool success)
	{
		if (success)
		{
			// Register for default packet handlers
			RegisterPacketHandler(auth::world_realm_packet::LogonChallenge, *this, &RealmConnector::OnLogonChallenge);

			// Send the auth packet
			sendSinglePacket([&](auth::OutgoingPacket& packet)
				{
					// Initialize packet using the op code
					packet.Start(auth::world_realm_packet::LogonChallenge);

					// Write the actual packet content
					const size_t contentStart = packet.Sink().Position();
					packet
						<< io::write<uint8>(mmo::Major)	// Version
						<< io::write<uint8>(mmo::Minor)
						<< io::write<uint8>(mmo::Build)
						<< io::write<uint16>(mmo::Revision)
						<< io::write_dynamic_range<uint8>(m_authName);

					// Finish packet and send it
					packet.Finish();
				});
			
			ILOG("Handshaking...");
		}
		else
		{
			// Connection error!
			ELOG("Could not connect to the realm server.");
		}
		
		return true;
	}

	void RealmConnector::connectionLost()
	{
		ELOG("Lost connection to the realm server");

		// Clear packet handlers
		ClearPacketHandlers();
	}

	void RealmConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
	}
}
