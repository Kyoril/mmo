// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "realm_connector.h"

#include "player.h"
#include "player_manager.h"
#include "game_server/world_instance_manager.h"
#include "game_server/world_instance.h"
#include "version.h"

#include "base/utilities.h"
#include "base/clock.h"
#include "base/constants.h"
#include "base/timer_queue.h"
#include "game_server/character_data.h"
#include "game/chat_type.h"
#include "game_server/game_player_s.h"
#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"


namespace mmo
{
	RealmConnector::RealmConnector(asio::io_service& io, TimerQueue& queue, const std::set<uint64>& defaultHostedMapIds, PlayerManager& playerManager, WorldInstanceManager& worldInstanceManager,
		const proto::Project& project)
		: auth::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
		, m_timerQueue(queue)
		, m_playerManager(playerManager)
		, m_worldInstanceManager(worldInstanceManager)
		, m_willReconnect(false)
		, m_project(project)
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

		// Clear all packet handlers just to be sure
		m_packetHandlers.clear();
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
		QueueReconnect();
	}

	void RealmConnector::QueueReconnect()
	{
		// Prevent double timer
		if (m_willReconnect)
		{
			return;
		}

		Reset();
		close();

		m_willReconnect = true;


		// Termination callback
		const auto reconnect = [this]() {
			m_willReconnect = false;
			connect(m_realmAddress, m_realmPort, *this, m_ioService);
		};

		// Notify the user
		WLOG("Reconnect in 5 seconds...");
		m_timerQueue.AddEvent(reconnect, m_timerQueue.GetNow() + constants::OneSecond * 5);
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

	void RealmConnector::SendProxyPacket(uint64 characterGuid, uint16 packetId, uint32 packetSize, const std::vector<char>& packetContent, bool flush)
	{
		sendSinglePacket([characterGuid, packetId, packetSize, &packetContent](auth::OutgoingPacket& outPacket)
		{
			outPacket.Start(auth::world_realm_packet::ProxyPacket);
			outPacket
				<< io::write<uint64>(characterGuid)
				<< io::write<uint16>(packetId)
				<< io::write<uint32>(packetSize)
				<< io::write_dynamic_range<uint32>(packetContent);
			outPacket.Finish();
		}, flush);
	}

	void RealmConnector::SendCharacterData(uint32 mapId, const InstanceId& instanceId, const GamePlayerS& character)
	{
		sendSinglePacket([&character, mapId, &instanceId](auth::OutgoingPacket & outPacket)
		{
			outPacket.Start(auth::world_realm_packet::CharacterData);
			outPacket
				<< io::write<uint64>(character.GetGuid())
				<< io::write<uint32>(mapId)
				<< instanceId
				<< character;
			outPacket.Finish();
		});
	}

	void RealmConnector::SendQuestData(uint64 characterGuid, uint32 questId, const QuestStatusData& questData)
	{
		sendSinglePacket([characterGuid, questId, &questData](auth::OutgoingPacket& outPacket)
			{
				outPacket.Start(auth::world_realm_packet::QuestData);
				outPacket
					<< io::write<uint64>(characterGuid)
					<< io::write<uint32>(questId)
					<< questData;
				outPacket.Finish();
			});
	}

	void RealmConnector::SendTeleportRequest(uint64 characterGuid, uint32 mapId, const Vector3& position, const Radian& facing)
	{
		sendSinglePacket([characterGuid, mapId, &position, &facing](auth::OutgoingPacket& outPacket)
			{
				outPacket.Start(auth::world_realm_packet::TeleportRequest);
				outPacket
					<< io::write<uint64>(characterGuid)
					<< io::write<uint32>(mapId)
					<< io::write<float>(position.x)
					<< io::write<float>(position.y)
					<< io::write<float>(position.z)
					<< io::write<float>(facing.GetValueRadians());
				outPacket.Finish();
			});
	}

	void RealmConnector::NotifyWorldInstanceLeft(uint64 characterGuid, auth::WorldLeftReason reason)
	{
		sendSinglePacket([characterGuid, reason](auth::OutgoingPacket& outPacket)
			{
				outPacket.Start(auth::world_realm_packet::PlayerCharacterLeft);
				outPacket
					<< io::write<uint64>(characterGuid)
					<< io::write<uint8>(static_cast<uint8>(reason));
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
				RegisterPacketHandler(auth::realm_world_packet::ProxyPacket, *this, &RealmConnector::OnProxyPacket);
				RegisterPacketHandler(auth::realm_world_packet::LocalChatMessage, *this, &RealmConnector::OnLocalChatMessage);
				
				PropagateHostedMapIds();
			}
			else
			{
				// Output error code in chat before terminating the application
				ELOG("[Login Server] Could not authenticate world at realm server, hash mismatch detected!");
				QueueReconnect();

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
			ELOG("Failed to read PLAYER_CHARACTER_JOIN packet");
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
			if (!instance)
			{
				ELOG("Failed to create world instance for map " << characterData.mapId);
				sendSinglePacket([&characterData](auth::OutgoingPacket& outPacket)
					{
						outPacket.Start(auth::world_realm_packet::PlayerCharacterJoinFailed);
						outPacket << io::write_packed_guid(characterData.characterId);
						outPacket.Finish();
					});
				return PacketParseResult::Pass;
			}
		}

		const auto* classEntry = m_project.classes.getById(characterData.classId);
		if (!classEntry)
		{
			ELOG("Character data contains unknown class id " << characterData.classId << " - ensure data project is up to date with the realm!");
			sendSinglePacket([&characterData](auth::OutgoingPacket& outPacket)
				{
					outPacket.Start(auth::world_realm_packet::PlayerCharacterJoinFailed);
					outPacket << io::write_packed_guid(characterData.characterId);
					outPacket.Finish();
				});
			return PacketParseResult::Pass;
		}

		const auto* raceEntry = m_project.races.getById(characterData.raceId);
		if (!raceEntry)
		{
			ELOG("Character data contains unknown race id " << characterData.raceId << " - ensure data project is up to date with the realm!");
			sendSinglePacket([&characterData](auth::OutgoingPacket& outPacket)
				{
					outPacket.Start(auth::world_realm_packet::PlayerCharacterJoinFailed);
					outPacket << io::write_packed_guid(characterData.characterId);
					outPacket.Finish();
				});
			return PacketParseResult::Pass;
		}

		// Apply instance id before sending
		characterData.instanceId = instance->GetId();

		// Create the character object
		auto characterObject = std::make_shared<GamePlayerS>(m_project, m_timerQueue);
		characterObject->Initialize();
		characterObject->Set(object_fields::Guid, characterData.characterId);

		if (characterData.position.y < 0.0f)
		{
			WLOG("Player position height was too low, safeguard set it to 10");
			characterData.position.y = 10.0f;
		}

		characterObject->Relocate(characterData.position, characterData.facing);

		// Make character fall on login
		MovementInfo info = characterObject->GetMovementInfo();
		info.movementFlags |= movement_flags::Falling;
		characterObject->ApplyMovementInfo(info);

		characterObject->SetClass(*classEntry);
		characterObject->SetRace(*raceEntry);
		characterObject->SetGender(characterData.gender);
		characterObject->SetLevel(characterData.level);
		characterObject->Set<uint32>(object_fields::Xp, characterData.xp);
		characterObject->Set<uint32>(object_fields::Health, Clamp(characterData.hp, 0u, characterObject->Get<uint32>(object_fields::MaxHealth)));
		characterObject->Set<uint32>(object_fields::Mana, Clamp(characterData.mana, 0u, characterObject->Get<uint32>(object_fields::MaxMana)));
		characterObject->Set<uint32>(object_fields::Rage, characterData.rage);
		characterObject->Set<uint32>(object_fields::Energy, characterData.energy);
		characterObject->Set<uint32>(object_fields::Money, characterData.money);

		// Mark rewarded quests
		for (const uint32& questId : characterData.rewardedQuestIds)
		{
			characterObject->NotifyQuestRewarded(questId);
		}

		// Set quest status data
		for (const auto& questData : characterData.questStatus)
		{
			characterObject->SetQuestData(questData.first, questData.second);
		}

		characterObject->SetBinding(characterData.bindMap, characterData.bindPosition, characterData.bindFacing);

		bool pointsReset = false;
		for (uint32 i = 0; i < 5; ++i)
		{
			for (uint32 j = 0; j < characterData.attributePointsSpent[i]; ++j)
			{
				if (!characterObject->AddAttributePoint(i))
				{
					WLOG("Points have been reset due to inconsistencies with points spent vs points available!");
					pointsReset = true;
					break;
				}
			}

			if (pointsReset)
			{
				break;
			}
		}

		// Construct inventory data
		for (const auto& itemData : characterData.items)
		{
			characterObject->GetInventory().AddRealmData(itemData);
		}

		characterObject->ClearFieldChanges();

		// Create a new player object
		auto player = std::make_shared<Player>(m_playerManager, *this, characterObject, characterData, m_project);
		m_playerManager.AddPlayer(player);

		// Enter the world using the character object
		instance->AddGameObject(*characterObject);

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
		ObjectGuid characterGuid;
		auth::WorldLeftReason reason;
		if (!(packet >> io::read<uint64>(characterGuid) >> io::read<uint8>(reason)))
		{
			ELOG("Failed to read PLAYER_CHARACTER_LEAVE packet");
			return PacketParseResult::Disconnect;
		}

		DLOG("Received PLAYER_CHARACTER_LEAVE packet for character " << log_hex_digit(characterGuid) << " from realm due to reason " << reason);

		const auto player = m_playerManager.GetPlayerByCharacterGuid(characterGuid);
		if (!player)
		{
			return PacketParseResult::Pass;
		}

		m_playerManager.RemovePlayer(player);

		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnProxyPacket(auth::IncomingPacket& packet)
	{
		ObjectId characterId;
		if (!(packet >> io::read<uint64>(characterId)))
		{
			ELOG("Failed to read PROXY_PACKET packet");
			return PacketParseResult::Disconnect;
		}

		const auto player = m_playerManager.GetPlayerByCharacterGuid(characterId);
		if (!player)
		{
			WLOG("Received proxy packet for unknown player character");
			return PacketParseResult::Pass;
		}

		uint16 opCode;
		uint32 packetSize;
		if (!(packet >> io::read<uint16>(opCode) >> io::read<uint32>(packetSize)))
		{
			ELOG("Failed to read PROXY_PACKET packet");
			return PacketParseResult::Disconnect;
		}
		
		std::vector<uint8> buffer;

		if (packetSize > 0)
		{
			buffer.resize(packetSize);
			packet.getSource()->read(reinterpret_cast<char*>(&buffer[0]), buffer.size());
			if (!packet)
			{
				ELOG("Failed to read PROXY_PACKET packet");
				return PacketParseResult::Disconnect;
			}
		}

		player->HandleProxyPacket(static_cast<game::client_realm_packet::Type>(opCode), buffer);
		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnLocalChatMessage(auth::IncomingPacket& packet)
	{
		ObjectId playerGuid;
		ChatType chatType;
		String message;

		if (!(packet 
			>> io::read_packed_guid(playerGuid) 
			>> io::read<uint8>(chatType)
			>> io::read_container<uint16>(message)))
		{
			ELOG("Failed to read LOCAL_CHAT packet")
			return PacketParseResult::Disconnect;
		}

		const auto player = m_playerManager.GetPlayerByCharacterGuid(playerGuid);
		if (!player)
		{
			WLOG("Received local chat message packet for unknown player character " << log_hex_digit(playerGuid));
			return PacketParseResult::Pass;
		}

		DLOG("Received local chat message from player " << log_hex_digit(playerGuid));

		switch(chatType)
		{
		case ChatType::Say:
		case ChatType::Yell:
		case ChatType::Emote:
			player->LocalChatMessage(chatType, message);
			break;
		default:
			ELOG("Unsupported chat type received: " << log_hex_digit(static_cast<uint16>(chatType)));
			return PacketParseResult::Pass;
		}

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

			// Clear packet handlers
			ClearPacketHandlers();
			QueueReconnect();
		}
		
		return true;
	}

	void RealmConnector::connectionLost()
	{
		ELOG("Lost connection to the realm server");

		// Clear packet handlers
		ClearPacketHandlers();
		QueueReconnect();
	}

	void RealmConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
		QueueReconnect();
	}
}
