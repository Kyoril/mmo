// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world.h"
#include "database.h"

#include "base/random.h"
#include "base/weak_ptr_function.h"
#include "log/default_log_levels.h"
#include "auth_protocol/auth_protocol.h"

#include <functional>

#include "player.h"
#include "player_manager.h"
#include "vector_sink.h"
#include "base/big_number.h"
#include "base/constants.h"
#include "base/utilities.h"
#include "game/game.h"
#include "game_protocol/game_outgoing_packet.h"
#include "game_server/game_player_s.h"
#include "proto_data/project.h"


namespace mmo
{
	World::World(
		TimerQueue& timerQueue,
		WorldManager& worldManager,
		PlayerManager& playerManager,
		AsyncDatabase& database, 
		std::shared_ptr<Client> connection, 
		const String & address,
		const proto::Project& project)
		: m_timerQueue(timerQueue)
		, m_manager(worldManager)
		, m_playerManager(playerManager)
		, m_database(database)
		, m_connection(std::move(connection))
		, m_address(address)
		, m_project(project)
	{
		m_connection->setListener(*this);

		RegisterPacketHandler(auth::world_realm_packet::LogonChallenge, *this, &World::OnLogonChallenge);
	}

	void World::Destroy()
	{
		destroyed(*this);

		m_connection->resetListener();
		m_connection.reset();

		m_manager.WorldDisconnected(*this);
	}

	void World::connectionLost()
	{
		ILOG("World node " << m_address << " disconnected");
		Destroy();
	}

	void World::connectionMalformedPacket()
	{
		ILOG("World node " << m_address << " sent malformed packet");
		Destroy();
	}

	PacketParseResult World::connectionPacketReceived(auth::IncomingPacket & packet)
	{
		const auto packetId = packet.GetId();
		bool isValid = true;

		PacketHandler handler = nullptr;

		{
			// Lock packet handler access
			std::scoped_lock lock{ m_packetHandlerMutex };

			// Check for packet handlers
			const auto handlerIt = m_packetHandlers.find(packetId);
			if (handlerIt == m_packetHandlers.end())
			{
				WLOG("Packet 0x" << std::hex << static_cast<uint32>(packetId) << " is either unhandled or simply currently not handled");
				return PacketParseResult::Disconnect;
			}

			handler = handlerIt->second;
		}

		// Execute the packet handler and return the result
		return handler(packet);
	}

	PacketParseResult World::OnLogonChallenge(auth::IncomingPacket& packet)
	{
		ClearPacketHandler(auth::world_realm_packet::LogonChallenge);

		// Read the packet data
		if (!(packet
			>> io::read<uint8>(m_version1)
			>> io::read<uint8>(m_version2)
			>> io::read<uint8>(m_version3)
			>> io::read<uint16>(m_build)
			>> io::read_container<uint8>(m_worldName)))
		{
			return PacketParseResult::Disconnect;
		}

		// Write the login attempt to the logs
		ILOG("Received logon challenge for world " << m_worldName << "...");

		// RequestHandler
		std::weak_ptr weakThis{ shared_from_this() };
		auto handler = [weakThis](std::optional<WorldAuthData> result) {
			if (auto strongThis = weakThis.lock())
			{
				// The temporary result
				auth::AuthResult authResult = auth::auth_result::FailWrongCredentials;
				if (result)
				{
					strongThis->m_s.setHexStr(result->s);
					strongThis->m_v.setHexStr(result->v);

					// Store realm infos
					strongThis->m_worldId = result->id;
					strongThis->m_worldName = result->name;

					// We are NOT banned so continue
					authResult = auth::auth_result::Success;

					strongThis->m_b.setRand(19 * 8);
					const auto gmod = constants::srp::g.modExp(strongThis->m_b, constants::srp::N);
					strongThis->m_B = ((strongThis->m_v * 3) + gmod) % constants::srp::N;

					assert(gmod.getNumBytes() <= 32);
					strongThis->m_unk3.setRand(16 * 8);

					// Allow handling the logon proof packet now
					strongThis->RegisterPacketHandler(auth::world_realm_packet::LogonProof, *strongThis.get(), &World::OnLogonProof);
				}
				else
				{
					// Invalid account name display
					WLOG("Invalid world name " << strongThis->GetWorldName());
				}

				// Send packet with result
				strongThis->m_connection->sendSinglePacket([authResult, &strongThis](auth::OutgoingPacket& outPacket) {
					outPacket.Start(auth::realm_world_packet::LogonChallenge);
					outPacket << io::write<uint8>(authResult);

					// On success, there are more data values to write
					if (authResult == auth::auth_result::Success)
					{
						// Write B with 32 byte length and g
						std::vector<uint8> B_ = strongThis->m_B.asByteArray(32);
						outPacket
							<< io::write_range(B_.begin(), B_.end())
							<< io::write<uint8>(constants::srp::g.asUInt32());

						// Write N with 32 byte length
						const std::vector<uint8> N_ = constants::srp::N.asByteArray(32);
						outPacket << io::write_range(N_.begin(), N_.end());

						// Write s
						const std::vector<uint8> s_ = strongThis->m_s.asByteArray();
						outPacket << io::write_range(s_.begin(), s_.end());
					}

					outPacket.Finish();
				});
			}
		};

		// Execute
		m_database.asyncRequest(std::move(handler), &IDatabase::GetWorldAuthData, std::cref(m_worldName));
		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnLogonProof(auth::IncomingPacket& packet)
	{
		ClearPacketHandler(auth::world_realm_packet::LogonProof);

		// Read packet data
		std::array<uint8, 32> rec_A{};
		std::array<uint8, 20> rec_M1{};
		if (!(packet
			>> io::read_range(rec_A.begin(), rec_A.end())
			>> io::read_range(rec_M1.begin(), rec_M1.end())
			))
		{
			return PacketParseResult::Disconnect;
		}

		// Continue the SRP6 calculation based on data received from the client
		BigNumber A{ rec_A.data(), rec_A.size() };

		// SRP safeguard: abort if A % N == 0
		if ((A % constants::srp::N).isZero())
		{
			ELOG("[Logon Proof] SRP safeguard failed");
			return PacketParseResult::Disconnect;
		}

		// Build hash
		SHA1Hash hash = Sha1_BigNumbers({ A, m_B });

		// Calculate u and S
		BigNumber u{ hash.data(), hash.size() };
		BigNumber S = (A * (m_v.modExp(u, constants::srp::N))).modExp(m_b, constants::srp::N);

		// Build t
		const auto t = S.asByteArray(32);
		std::array<uint8, 16> t1{};
		for (size_t i = 0; i < t1.size(); ++i)
		{
			t1[i] = t[i * 2];
		}
		hash = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());

		std::array<uint8, 40> vK{};
		for (size_t i = 0; i < 20; ++i)
		{
			vK[i * 2] = hash[i];
		}
		for (size_t i = 0; i < 16; ++i)
		{
			t1[i] = t[i * 2 + 1];
		}

		hash = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());
		for (size_t i = 0; i < 20; ++i)
		{
			vK[i * 2 + 1] = hash[i];
		}

		BigNumber K{ vK.data(), vK.size() };

		SHA1Hash h;
		h = Sha1_BigNumbers({ constants::srp::N });
		hash = Sha1_BigNumbers({ constants::srp::g });
		for (size_t i = 0; i < h.size(); ++i)
		{
			h[i] ^= hash[i];
		}

		BigNumber t3{ h.data(), h.size() };

		HashGeneratorSha1 sha;
		Sha1_Add_BigNumbers(sha, { t3 });
		const auto t4 = sha1(reinterpret_cast<const char*>(m_worldName.data()), m_worldName.size());
		sha.update(reinterpret_cast<const char*>(t4.data()), t4.size());
		Sha1_Add_BigNumbers(sha, { m_s, A, m_B, K });
		hash = sha.finalize();

		// Proof result which will be sent to the client
		auth::AuthResult proofResult = auth::auth_result::FailWrongCredentials;

		// Calculate M1 hash on server using values sent by the client and compare it against
		// the M1 hash sent by the client to see if the passwords do match.
		const BigNumber m1{ hash.data(), hash.size() };
		if (auto sArr = m1.asByteArray(20); std::equal(sArr.begin(), sArr.end(), rec_M1.begin()))
		{
			// Finish SRP6 by calculating the M2 hash value that is sent back to the client for
			// verification as well.
			m_m2 = Sha1_BigNumbers({ A, m1, K });
			m_sessionKey = 0;
			
			// Handler method
			std::weak_ptr weakThis{ shared_from_this() };
			auto handler = [weakThis, K](const bool success)
			{
				if (const auto strongThis = weakThis.lock())
				{
					if (success)
					{
						// Add log entry about successful login as the hashes do indeed mach (and thus, so
						// do the passwords)
						ILOG("World node " << strongThis->m_worldName << " successfully authenticated");

						// Store the calculated session key value internally for later use, also store it in the 
						// database maybe.
						strongThis->m_sessionKey = K;
						strongThis->RegisterPacketHandler(auth::world_realm_packet::PropagateMapList, *strongThis, &World::OnPropagateMapList);
						strongThis->RegisterPacketHandler(auth::world_realm_packet::PlayerCharacterJoined, *strongThis, &World::OnPlayerCharacterJoined);
						strongThis->RegisterPacketHandler(auth::world_realm_packet::PlayerCharacterJoinFailed, *strongThis, &World::OnPlayerCharacterJoinFailed);
						strongThis->RegisterPacketHandler(auth::world_realm_packet::PlayerCharacterLeft, *strongThis, &World::OnPlayerCharacterLeft);
						strongThis->RegisterPacketHandler(auth::world_realm_packet::InstanceCreated, *strongThis, &World::OnInstanceCreated);
						strongThis->RegisterPacketHandler(auth::world_realm_packet::InstanceDestroyed, *strongThis, &World::OnInstanceDestroyed);
						strongThis->RegisterPacketHandler(auth::world_realm_packet::ProxyPacket, *strongThis, &World::OnProxyPacket);
						strongThis->RegisterPacketHandler(auth::world_realm_packet::CharacterData, *strongThis, &World::OnCharacterData);
						strongThis->RegisterPacketHandler(auth::world_realm_packet::QuestData, *strongThis, &World::OnQuestData);
						strongThis->RegisterPacketHandler(auth::world_realm_packet::TeleportRequest, *strongThis, &World::OnTeleportRequest);

						// If the login attempt succeeded, then we will accept RealmList request packets from now
						// on to send the realm list to the client on manual request
						strongThis->SendAuthProof(auth::AuthResult::Success);
					}
					else
					{
						strongThis->SendAuthProof(auth::AuthResult::FailDbBusy);
					}
				}
			};

			// Build version string for database (cast to uint16 as ostringstream would otherwise treat uint8
			// numbers as ascii character letters!)
			std::ostringstream versionBuilder;
			versionBuilder
				<< static_cast<uint16>(m_version1)
				<< "."
				<< static_cast<uint16>(m_version2)
				<< "."
				<< static_cast<uint16>(m_version3)
				<< "."
				<< m_build;

			// Store session key in account database
			m_database.asyncRequest<void>(
				[this, sessionKey = K.asHexStr(), capture1 = versionBuilder.str()](auto&& database)
				{
					database->WorldLogin(m_worldId, sessionKey, m_address, capture1);
				},
				std::move(handler));

			// Stop here since we wait for the database callback
			return PacketParseResult::Pass;
		}
		else
		{
			// Log error
			WLOG("Invalid password for world " << m_worldName);
		}

		// Send proof result
		SendAuthProof(proofResult);
		return PacketParseResult::Pass;
	}

	void World::SendAuthProof(auth::AuthResult result)
	{
		// Send proof packet to the client
		m_connection->sendSinglePacket([result, this](auth::OutgoingPacket& packet) {
			packet.Start(auth::realm_world_packet::LogonProof);
			packet << io::write<uint8>(result);

			// If the login attempt succeeded, we also send the calculated M2 hash value to the
			// client, which will compare it against it's own calculated M2 hash just in case.
			if (result == auth::auth_result::Success)
			{
				// Send server-calculated M2 hash value so that the client can verify this as well
				packet << io::write_range(this->m_m2.begin(), this->m_m2.end());
			}

			packet.Finish();
		});
	}

	void World::ConsumeOnCharacterJoinedCallback(const uint64 characterGuid, const bool success, const InstanceId instanceId)
	{
		JoinWorldCallback callback = nullptr;

		{
			std::scoped_lock lock { m_joinCallbackMutex};

			if (const auto result = m_joinCallbacks.find(characterGuid); result != m_joinCallbacks.end())
			{
				callback = std::move(result->second);
				m_joinCallbacks.erase(result);
			}
		}
		
		callback(instanceId, success);
	}

	PacketParseResult World::OnPropagateMapList(auth::IncomingPacket& packet)
	{
		std::scoped_lock lock{ m_hostedMapIdMutex };
		
		m_hostedMapIds.clear();
		if (!(packet >> io::read_container<uint16>(m_hostedMapIds)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Received new list of hosted map ids from world node, containing " << m_hostedMapIds.size() << " map ids");
		for(const auto& mapId : m_hostedMapIds)
		{
			if (const proto::MapEntry* mapEntry = m_project.maps.getById(mapId))
			{
				DLOG("\tMap: " << mapId << " (" << mapEntry->name() << ")");
			}
			else
			{
				WLOG("World node hosts unknown map id '" << mapId << "'");
			}
			
		}
		
		return PacketParseResult::Pass;
	}

	void World::Join(CharacterData characterData, JoinWorldCallback callback)
	{
		// TODO: What if we already have a waiting callback? Right now we just discard the old one
		if (callback)
		{
			std::scoped_lock lock { m_joinCallbackMutex };
			m_joinCallbacks.emplace(characterData.characterId, std::move(callback));	
		}
		
		GetConnection().sendSinglePacket([characterData](auth::OutgoingPacket& outPacket)
		{
			outPacket.Start(auth::realm_world_packet::PlayerCharacterJoin);
			outPacket << characterData;
			outPacket.Finish();
		});
	}

	void World::Leave(ObjectGuid characterGuid, auth::WorldLeftReason reason)
	{
		GetConnection().sendSinglePacket([characterGuid, reason](auth::OutgoingPacket& outPacket)
		{
			outPacket.Start(auth::realm_world_packet::PlayerCharacterLeave);
			outPacket << io::write<uint64>(characterGuid) << io::write<uint8>(reason);
			outPacket.Finish();
		});
	}

	void World::RegisterPacketHandler(uint16 opCode, PacketHandler && handler)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		if (const auto it = m_packetHandlers.find(opCode); it == m_packetHandlers.end())
		{
			m_packetHandlers.emplace(std::make_pair(opCode, std::forward<PacketHandler>(handler)));
		}
		else
		{
			it->second = std::forward<PacketHandler>(handler);
		}
	}

	void World::ClearPacketHandler(uint16 opCode)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		const auto it = m_packetHandlers.find(opCode);
		if (it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}

	bool World::IsHostingMapId(const MapId mapId)
	{
		std::scoped_lock lock{ m_hostedMapIdMutex };
		return std::find(m_hostedMapIds.begin(), m_hostedMapIds.end(), mapId) != m_hostedMapIds.end();
	}

	auto World::IsHostingInstanceId(const InstanceId instanceId) -> bool
	{
		std::scoped_lock lock{ m_hostedInstanceIdMutex };
		return std::find(m_hostedInstanceIds.begin(), m_hostedInstanceIds.end(), instanceId) != m_hostedInstanceIds.end();
	}

	void World::RequestMapInstanceCreation(MapId mapId)
	{
		// TODO: Implement this method. This should notify the connected world node that it should
		// create an instance for a given map id for us and notify us as soon as this has happened.
		// Instance creation might fail, in which case we also want to be notified about failure and
		// the reason of failure!

	}

	void World::LocalChatMessage(const uint64 playerGuid, const ChatType chatType, const std::string& message) const
	{
		GetConnection().sendSinglePacket([playerGuid, chatType, &message](auth::OutgoingPacket& outPacket)
			{
				outPacket.Start(auth::realm_world_packet::LocalChatMessage);
				outPacket
					<< io::write_packed_guid(playerGuid)
					<< io::write<uint8>(chatType)
					<< io::write_dynamic_range<uint16>(message);
				outPacket.Finish();
			});
	}

	PacketParseResult World::OnPlayerCharacterJoined(auth::IncomingPacket& packet)
	{
		uint64 characterGuid = 0;
		InstanceId instanceId;
		if (!(packet >> io::read_packed_guid(characterGuid) >> instanceId))
		{
			return PacketParseResult::Disconnect;
		}
		
		DLOG("Player character " << log_hex_digit(characterGuid) << " successfully joined world instance!");
		ConsumeOnCharacterJoinedCallback(characterGuid, true, instanceId);
		
		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnPlayerCharacterJoinFailed(auth::IncomingPacket& packet)
	{
		uint64 characterGuid = 0;
		if (!(packet >> io::read_packed_guid(characterGuid)))
		{
			return PacketParseResult::Disconnect;
		}
		
		DLOG("Player character " << log_hex_digit(characterGuid) << " failed to join world instance!");
		ConsumeOnCharacterJoinedCallback(characterGuid, false, InstanceId());
		
		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnInstanceCreated(auth::IncomingPacket& packet)
	{
		InstanceId instanceId;
		if (!(packet >> instanceId))
		{
			return PacketParseResult::Disconnect;
		}
		
		ILOG("New world instance hosted: " << instanceId.to_string());

		std::scoped_lock lock { m_hostedInstanceIdMutex };
		m_hostedInstanceIds.emplace_back(std::move(instanceId));

		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnInstanceDestroyed(auth::IncomingPacket& packet)
	{
		InstanceId instanceId;
		if (!(packet >> instanceId))
		{
			return PacketParseResult::Disconnect;
		}

		ILOG("World instance host terminated: " << instanceId.to_string());

		std::scoped_lock lock { m_hostedInstanceIdMutex };
		m_hostedInstanceIds.emplace_back(std::move(instanceId));

		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnProxyPacket(auth::IncomingPacket& packet)
	{
		uint64 characterGuid;
		uint16 packetId;
		uint32 packetSize;
		std::vector<uint8> packetContent;
		if (!(packet 
			>> io::read<uint64>(characterGuid)
			>> io::read<uint16>(packetId)
			>> io::read<uint32>(packetSize)
			>> io::read_container<uint32>(packetContent)
			))
		{
			return PacketParseResult::Disconnect;
		}
		
		auto* player = m_playerManager.GetPlayerByCharacterGuid(characterGuid);
		if (!player)
		{
			WLOG("Could not find player to redirect proxy packet");
			return PacketParseResult::Pass;
		}

		std::vector<char> outBuffer;
		io::VectorSink sink { outBuffer };

		game::OutgoingPacket proxyPacket(sink, true);
		proxyPacket
			<< io::write_range(packetContent);
		player->SendProxyPacket(packetId, outBuffer);
		
		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnCharacterData(auth::IncomingPacket& packet)
	{
		uint64 characterGuid = 0;
		uint32 mapId = 0;
		InstanceId instanceId;

		GamePlayerS player(m_project, m_timerQueue);
		player.Initialize();

		if (!(packet
			>> io::read<uint64>(characterGuid)
			>> io::read<uint32>(mapId)
			>> instanceId
			>> player
			))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Received character data for character " << log_hex_digit(characterGuid) << ", persisting character data...");

		std::array<uint32, 5> attributePoints;
		for (uint32 i = 0; i < attributePoints.size(); ++i)
		{
			attributePoints[i] = player.GetAttributePointsByAttribute(i);
		}

		std::vector<uint32> spellIds;
		spellIds.reserve(player.GetSpells().size());
		for (const auto& spell : player.GetSpells())
		{
			if (!spell) continue;
			spellIds.push_back(spell->id());
		}

		// RequestHandler
		auto handler = [](bool result) { };
		m_database.asyncRequest(std::move(handler), &IDatabase::UpdateCharacter, characterGuid, mapId, player.GetMovementInfo().position,
			player.GetMovementInfo().facing, player.Get<uint32>(object_fields::Level),
			player.Get<uint32>(object_fields::Xp), 
			player.Get<uint32>(object_fields::Health), 
			player.Get<uint32>(object_fields::Mana), 
			player.Get<uint32>(object_fields::Rage), 
			player.Get<uint32>(object_fields::Energy),
			player.Get<uint32>(object_fields::Money),
			player.GetInventory().GetItemData(),
			player.GetBindMap(),
			player.GetBindPosition(),
			player.GetBindFacing(),
			attributePoints,
			spellIds
			);

		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnQuestData(auth::IncomingPacket& packet)
	{
		uint64 characterGuid = 0;
		uint32 questId = 0;
		QuestStatusData questData;

		if (!(packet
			>> io::read<uint64>(characterGuid)
			>> io::read<uint32>(questId)
			>> questData
			))
		{
			return PacketParseResult::Disconnect;
		}

		// RequestHandler
		auto handler = [characterGuid](bool result)
		{
			if (!result)
			{
				WLOG("Failed to persist quest data for character " << log_hex_digit(characterGuid));
			}
		};
		m_database.asyncRequest(std::move(handler), &IDatabase::SetQuestData, characterGuid, questId, questData);

		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnTeleportRequest(auth::IncomingPacket& packet)
	{
		uint64 characterGuid = 0;
		uint32 mapId;
		Vector3 position;
		float facingRadianVal;
		if (!(packet >> io::read<uint64>(characterGuid)
			>> io::read<uint32>(mapId)
			>> io::read<float>(position.x)
			>> io::read<float>(position.y)
			>> io::read<float>(position.z)
			>> io::read<float>(facingRadianVal)
			))
		{
			return PacketParseResult::Disconnect;
		}

		// Find the player using this character
		auto player = m_playerManager.GetPlayerByCharacterGuid(characterGuid);
		if (!player)
		{
			ELOG("Can't find player by character id - transfer failed");
			return PacketParseResult::Pass;
		}

		ILOG("Initializing transfer of player " << log_hex_digit(characterGuid) << " to: " << mapId << " - " << position);
		player->InitializeTransfer(mapId, position, facingRadianVal);

		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnPlayerCharacterLeft(auth::IncomingPacket& packet)
	{
		auth::WorldLeftReason reason;
		uint64 playerGuid = 0;
		if (!(packet >> io::read<uint64>(playerGuid) >> io::read<uint8>(reason)))
		{
			return PacketParseResult::Disconnect;
		}
		
		DLOG("Player character " << log_hex_digit(playerGuid) << " left world instance!");

		// Notify player about this
		const auto player = m_playerManager.GetPlayerByCharacterGuid(playerGuid);
		if (!player)
		{
			WLOG("Could not find player with character id " << log_hex_digit(playerGuid));
			return PacketParseResult::Pass;
		}

		// Send world instance data
		const auto strong = shared_from_this();
		player->OnWorldLeft(strong, reason);

		return PacketParseResult::Pass;
	}
}
