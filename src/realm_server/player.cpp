// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player.h"
#include "player_manager.h"
#include "world_manager.h"
#include "world.h"
#include "login_connector.h"
#include "database.h"
#include "version.h"

#include "base/random.h"
#include "base/sha1.h"
#include "log/default_log_levels.h"
#include "math/vector3.h"
#include "math/degree.h"
#include "proto_data/project.h"

#include <functional>

#include "base/utilities.h"
#include "game/chat_type.h"
#include "game/quest_info.h"
#include "game_server/game_player_s.h"


namespace mmo
{
	Player::Player(
		TimerQueue& timerQueue,
		PlayerManager& playerManager,
		WorldManager& worldManager,
		LoginConnector &loginConnector,
		AsyncDatabase& database, 
		std::shared_ptr<Client> connection,
		String address,
		const proto::Project& project)
		: m_timerQueue(timerQueue)
		, m_manager(playerManager)
		, m_worldManager(worldManager)
		, m_loginConnector(loginConnector)
		, m_database(database)
		, m_project(project)
		, m_connection(std::move(connection))
		, m_address(std::move(address))
		, m_accountId(0)
	{
		// Generate random seed for packet header encryption & decryption
		std::uniform_int_distribution<uint32> dist;
		m_seed = dist(RandomGenerator);

		m_connection->setListener(*this);
	}

	void Player::Kick()
	{
		DLOG("Kicking player with account " << GetAccountName() << " (" << GetAccountId() << ")");
		Destroy();
	}

	void Player::Destroy()
	{
		// Save action bar
		if (m_pendingButtons)
		{
			m_database.asyncRequest([](bool) {}, &IDatabase::SetCharacterActionButtons, m_characterData->characterId, m_actionButtons);
			m_pendingButtons = false;
		}

		if (const auto strongWorld = m_world.lock())
		{
			if (HasCharacterGuid())
			{
				strongWorld->Leave(GetCharacterGuid());
			}
		}

		m_connection->resetListener();
		m_connection->close();
		m_connection.reset();

		m_manager.PlayerDisconnected(*this);
	}

	void Player::DoCharEnum()
	{
		// RequestHandler
		std::weak_ptr weakThis{ shared_from_this() };
		auto handler = [weakThis](std::optional<std::vector<CharacterView>> result) {
			if (auto strongThis = weakThis.lock())
			{
				// Lock around m_characterViews
				{
					std::scoped_lock lock{ strongThis->m_charViewMutex };

					// Copy character view data
					strongThis->m_characterViews.clear();
					for (auto& charView : result.value())
					{
						// Resolve display id from race/class (TODO: Maybe do this somewhere else or differently? This seems like a wrong place to do this, but it works :/)
						if (const auto* raceEntry = strongThis->m_project.races.getById(charView.GetRaceId()))
						{
							charView.SetDisplayId(charView.GetGender() == Male ? raceEntry->malemodel() : raceEntry->femalemodel());
						}

						strongThis->m_characterViews[charView.GetGuid()] = charView;
					}
				}

				// Send result data to requesting client
				strongThis->GetConnection().sendSinglePacket([&result](game::OutgoingPacket& outPacket)
					{
						outPacket.Start(game::realm_client_packet::CharEnum);
						outPacket << io::write_dynamic_range<uint8>(*result);
						outPacket.Finish();
					});
			}
			else
			{
				WLOG("Could not send char list (client no longer available!)");
			}
		};

		// Testing...
		DLOG("Requesting char list for account " << m_accountId << "...");

		// Execute
		ASSERT(m_accountId != 0);
		m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterViewsByAccountId, m_accountId);
	}

	void Player::connectionLost()
	{
		ILOG("Client " << m_address << " disconnected");
		Destroy();
	}

	void Player::connectionMalformedPacket()
	{
		ILOG("Client " << m_address << " sent malformed packet");
		Destroy();
	}

	PacketParseResult Player::connectionPacketReceived(game::IncomingPacket & packet)
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

	PacketParseResult Player::OnAuthSession(game::IncomingPacket & packet)
	{
		ClearPacketHandler(game::client_realm_packet::AuthSession);

		// Read packet data
		if (!(packet
			>> io::read<uint32>(m_build)
			>> io::read_container<uint8>(m_accountName)
			>> io::read<uint32>(m_clientSeed)
			>> io::read_range(m_clientHash)))
		{
			ELOG("Could not read LogonChallenge packet from a game client");
			return PacketParseResult::Disconnect;
		}

		// Verify the client build immediately for validity
		if (m_build != mmo::Revision)
		{
#ifdef _DEBUG
			WLOG("Client is using a different build than the realm server (C " << m_build << ", S " << Revision << "). There might be incompatibilities");
#elif 0 // Replace with else to disable incompatible client builds on this realm in release builds
			ELOG("Client is using unsupported client build " << m_build);
			return PacketParseResult::Disconnect;
#endif
		}

		// Setup a weak callback handler
		std::weak_ptr weakThis{ shared_from_this() };
		auto callbackHandler = [weakThis](const bool succeeded, const uint64 accountId, const BigNumber& sessionKey) {
			// Obtain strong reference to see if the client connection is still valid
			if (const auto strongThis = weakThis.lock())
			{
				// Handle success cases
				if (succeeded)
				{
					// Store session key
					strongThis->m_accountId = accountId;
					strongThis->InitializeSession(sessionKey);
				}
				else
				{
					// TODO: Send response to the game client
					DLOG("CLIENT_AUTH_SESSION: Error");
				}
			}
		};

		// Since we can't verify the client hash, as we don't have the client's session key just yet,
		// we need to ask the login server for verification.
		if (!m_loginConnector.QueueClientAuthSession(m_accountName, m_clientSeed, m_seed, m_clientHash, callbackHandler))
		{
			// Could not queue session, there is probably something wrong with the login server 
			// connection, so we close the client connection at this point
			return PacketParseResult::Disconnect;
		}

		// Everything has been done on our end, the client will be notified once the login server
		// has processed the request we just made
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnCharEnum(game::IncomingPacket & packet)
	{
		DoCharEnum();

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnEnterWorld(game::IncomingPacket & packet)
	{
		EnableEnterWorldPacket(false);

		uint64 guid = 0;
		if (!(packet >> io::read<uint64>(guid)))
		{
			return PacketParseResult::Disconnect;
		}

		ILOG("Client wants to enter the world with character 0x" << std::hex << guid << "...");
		
		std::weak_ptr weakThis = shared_from_this();
		auto handler = [weakThis](const std::optional<CharacterData>& characterData)
		{
			const auto strongThis = weakThis.lock();
			if (strongThis)
			{
				strongThis->OnCharacterData(characterData);
			}
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::CharacterEnterWorld,
			guid, m_accountId);
		
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnCreateChar(game::IncomingPacket& packet)
	{
		uint8 race = 0, characterClass = 0, gender = 0;
		std::string characterName;
		if (!(packet
			>> io::read_container<uint8>(characterName)
			>> io::read<uint8>(race)
			>> io::read<uint8>(characterClass)
			>> io::read<uint8>(gender)
			))
		{
			return PacketParseResult::Disconnect;
		}

		// Check input parameters for invalid values
		if (characterName.empty() || characterName.size() > 12 || gender > 1)
		{
			GetConnection().sendSinglePacket([](game::OutgoingPacket& outPacket)
				{
					outPacket.Start(game::realm_client_packet::CharCreateResponse);
					outPacket << io::write<uint8>(game::char_create_result::Error);
					outPacket.Finish();
				});
			return PacketParseResult::Pass;
		}

		// Check if given character class exists
		const auto* classInstance = m_project.classes.getById(characterClass);
		if (classInstance == nullptr)
		{
			ELOG("Unable to find character class " << log_hex_digit(characterClass));
			GetConnection().sendSinglePacket([](game::OutgoingPacket& outPacket)
				{
					outPacket.Start(game::realm_client_packet::CharCreateResponse);
					outPacket << io::write<uint8>(game::char_create_result::Error);
					outPacket.Finish();
				});
			return PacketParseResult::Pass;
		}

		const auto* raceEntry = m_project.races.getById(race);
		if (raceEntry == nullptr)
		{
			ELOG("Unable to find character race " << log_hex_digit(race));
			GetConnection().sendSinglePacket([](game::OutgoingPacket& outPacket)
				{
					outPacket.Start(game::realm_client_packet::CharCreateResponse);
					outPacket << io::write<uint8>(game::char_create_result::Error);
					outPacket.Finish();
				});
			return PacketParseResult::Pass;
		}

		std::weak_ptr weakThis{ shared_from_this() };
		auto handler = [weakThis](const std::optional<CharCreateResult>& result) {
			if (const auto strongThis = weakThis.lock())
			{
				if (!result)
				{
					ELOG("Character creation failed due to an exception!");
					strongThis->GetConnection().sendSinglePacket([](game::OutgoingPacket& outPacket)
						{
							// TODO: This is not correct because we don't know if NameInUse is really the exact error
							outPacket.Start(game::realm_client_packet::CharCreateResponse);
							outPacket << io::write<uint8>(game::char_create_result::NameInUse);
							outPacket.Finish();
						});
					return;
				}

				if (result.value() == CharCreateResult::Success)
				{
					strongThis->DoCharEnum();
					return;
				}

				strongThis->GetConnection().sendSinglePacket([&result](game::OutgoingPacket& outPacket)
					{
						// TODO: This is not correct because we don't know if NameInUse is really the exact error
						outPacket.Start(game::realm_client_packet::CharCreateResponse);
						outPacket << io::write<uint8>(
							result.value() == CharCreateResult::NameAlreadyInUse ? game::char_create_result::NameInUse : game::char_create_result::Error);
						outPacket.Finish();
					});
			}
		};

		// TODO: Load real start values from static game data instead of hard code it here. However,
		// the infrastructure isn't read yet.
		const uint32 level = 1;
		const uint32 map = raceEntry->startmap();
		const Vector3 position(raceEntry->startposx(), raceEntry->startposy(), raceEntry->startposz());
		const Angle rotation(raceEntry->startrotation());

		// Setup a temporary player object
		GamePlayerS player(m_project, m_timerQueue);
		player.Initialize();
		player.SetClass(*classInstance);
		player.SetRace(*raceEntry);
		player.SetGender(gender);
		player.SetLevel(1);

		const uint32 hp = player.GetMaxHealth();
		const uint32 mana = player.Get<uint32>(object_fields::MaxMana);
		const uint32 rage = 0;
		const uint32 energy = player.Get<uint32>(object_fields::MaxEnergy);

		std::vector<uint32> spellIds;
		spellIds.reserve(classInstance->spells().size());
		for(const auto& spell : classInstance->spells())
		{
			if (spell.level() <= level)
			{
				spellIds.push_back(spell.spell());
			}
		}

		DLOG("Creating new character named '" << characterName << "' for account 0x" << std::hex << m_accountId << " (Race: " << raceEntry->id() << "; Class: " << classInstance->id() << "; Gender: " << (uint16)gender << ")...");
		m_database.asyncRequest(std::move(handler), &IDatabase::CreateCharacter, characterName, this->m_accountId, map, level, hp, gender, race, characterClass, position, rotation,
			spellIds, mana, rage, energy);
		
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnDeleteChar(game::IncomingPacket& packet)
	{
		ASSERT(IsAuthenticated());
		
		uint64 charGuid = 0;
		if (!(packet >> io::read<uint64>(charGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		// Lock around m_characterViews
		std::scoped_lock<std::mutex> lock{ m_charViewMutex };
		
		// Check if such a character exists
		const auto charIt = m_characterViews.find(charGuid);
		if (charIt == m_characterViews.end())
		{
			WLOG("Tried to delete character 0x" << std::hex << charGuid << " which doesn't exist or belong to the players account!");
			return PacketParseResult::Disconnect;
		}

		// Database callback handler
		std::weak_ptr weakThis{ shared_from_this() };
		auto handler = [weakThis](bool success) {
			if (auto strongThis = weakThis.lock())
			{
				if (success)
				{
					strongThis->DoCharEnum();
				}
				else
				{
					ELOG("Failed to delete character!");
				}
			}
		};
		
		DLOG("Deleting character 0x" << std::hex << charGuid << " from account 0x" << std::hex << m_accountId << "...");
		m_database.asyncRequest<void>([charGuid](auto&& database) { database->DeleteCharacter(charGuid); }, std::move(handler));

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnProxyPacket(game::IncomingPacket& packet)
	{
		const auto strongWorld = m_world.lock();
		if (!strongWorld)
		{
			return PacketParseResult::Disconnect;
		}

		const auto characterId = m_characterData->characterId;
		strongWorld->GetConnection().sendSinglePacket([characterId, &packet](auth::OutgoingPacket& outPacket)
		{
			outPacket.Start(auth::realm_world_packet::ProxyPacket);
			outPacket
				<< io::write<uint64>(characterId)
				<< io::write<uint16>(packet.GetId())
				<< io::write<uint32>(packet.GetSize());

			if (packet.GetSize() > 0)
			{
				std::vector<uint8> buffer;
				buffer.resize(packet.GetSize());
				packet.getSource()->read(reinterpret_cast<char*>(&buffer[0]), buffer.size());
				outPacket << io::write_range(buffer);
			}
			
			outPacket.Finish();
		});

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnChatMessage(game::IncomingPacket& packet)
	{
		ASSERT(HasCharacterGuid());

		uint8 chatType;
		std::string message;
		if (!(packet
			>> io::read<uint8>(chatType)
			>> io::read_limited_string<512>(message)))
		{
			return PacketParseResult::Disconnect;
		}

		// Switch chat type
		switch (static_cast<ChatType>(chatType))
		{
		case ChatType::Say:
		case ChatType::Yell:
		case ChatType::Emote:
			// Forward chat command to world node
			if (const auto strongWorld = m_world.lock())
			{
				strongWorld->LocalChatMessage(GetCharacterGuid(), static_cast<ChatType>(chatType), message);
			}
			break;

		case ChatType::Group:
			WLOG("Group chat is not implemented yet!");
			break;

		case ChatType::Guild:
			WLOG("Guild chat is not implemented yet!")
				break;

		case ChatType::Raid:
			WLOG("Raid chat is not implemented yet!");
			break;

		case ChatType::Channel:
			WLOG("Chat channels are not implemented yet!");
			break;

		default:
			ELOG("Unsupported chat type received from player: " << log_hex_digit(chatType));
			return PacketParseResult::Disconnect;
		}

		// Store in database
		m_database.asyncRequest([](bool) {}, &IDatabase::ChatMessage, m_characterData->characterId, static_cast<uint16>(chatType), message);

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnNameQuery(game::IncomingPacket& packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Received CMSG_NAME_QUERY for unit " << log_hex_digit(guid) << "...");

		const Player* player = m_manager.GetPlayerByCharacterGuid(guid);
		if (player != nullptr)
		{
			String name = player->GetCharacterName();
			m_connection->sendSinglePacket([guid, &name](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::NameQueryResult);
					packet
						<< io::write_packed_guid(guid)
						<< io::write<uint8>(true)
						<< io::write_range(name) << io::write<uint8>(0);
					packet.Finish();
				});
		}
		else
		{
			m_connection->sendSinglePacket([guid](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::NameQueryResult);
					packet
						<< io::write_packed_guid(guid)
						<< io::write<uint8>(false);
					packet.Finish();
				});
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnDbQuery(game::IncomingPacket& packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			return PacketParseResult::Disconnect;
		}

		switch(packet.GetId())
		{
		case game::client_realm_packet::CreatureQuery:
			OnQueryCreature(guid);
			break;

		case game::client_realm_packet::ItemQuery:
			OnQueryItem(guid);
			break;

		case game::client_realm_packet::QuestQuery:
			OnQueryQuest(guid);
			break;

		default:
			ELOG("Player tried to query unsupported item, packet handler might be subscribed for wrong op code (" << packet.GetId() << ")");
			break;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnSetActionBarButton(game::IncomingPacket& packet)
	{
		uint8 slot;
		if (!(packet >> io::read<uint8>(slot)))
		{
			return PacketParseResult::Disconnect;
		}

		if (slot >= MaxActionButtons)
		{
			ELOG("Wrong action bar button slot");
			return PacketParseResult::Disconnect;
		}

		if (!(packet >> m_actionButtons[slot]))
		{
			return PacketParseResult::Disconnect;
		}

		// State is now changed
		m_actionButtons[slot].state = action_button_update_state::Changed;
		m_pendingButtons = true;

		return PacketParseResult::Pass;
	}

	void Player::SendAuthChallenge()
	{
		// We will start accepting LogonChallenge packets from the client
		RegisterPacketHandler(game::client_realm_packet::AuthSession, *this, &Player::OnAuthSession);

		// Send the LogonChallenge packet to the client including our generated seed
		m_connection->sendSinglePacket([this](game::OutgoingPacket& packet) {
			packet.Start(game::realm_client_packet::AuthChallenge);
			packet << io::write<uint32>(m_seed);
			packet.Finish();
		});
	}

	void Player::InitializeSession(const BigNumber & sessionKey)
	{
		m_sessionKey = sessionKey;

		// Notify about success
		DLOG("CLIENT_AUTH_SESSION: Success!");

		// Initialize encryption
		HMACHash hash;
		m_connection->GetCrypt().GenerateKey(hash, m_sessionKey);
		m_connection->GetCrypt().SetKey(hash.data(), hash.size());
		m_connection->GetCrypt().Init();

		// Send the response to the client
		m_connection->sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::realm_client_packet::AuthSessionResponse);
			packet << io::write<uint8>(game::auth_result::Success);	// TODO: Write real packet content
			packet.Finish();
		});

		// Enable CharEnum packets
		RegisterPacketHandler(game::client_realm_packet::CharEnum, *this, &Player::OnCharEnum);
		RegisterPacketHandler(game::client_realm_packet::CreateChar, *this, &Player::OnCreateChar);
		RegisterPacketHandler(game::client_realm_packet::DeleteChar, *this, &Player::OnDeleteChar);
		RegisterPacketHandler(game::client_realm_packet::EnterWorld, *this, &Player::OnEnterWorld);
	}

	void Player::SendProxyPacket(uint16 packetId, const std::vector<char>& buffer)
	{
		const auto bufferSize = buffer.size();
		if (bufferSize == 0)
		{
			return;	
		}

		// Write native packet
		mmo::Buffer &sendBuffer = m_connection->getSendBuffer();
		io::StringSink sink(sendBuffer);

		// Get the end of the buffer (needed for encryption)
		const size_t bufferPos = sendBuffer.size();

		game::Protocol::OutgoingPacket packet(sink, true);
		packet.Start(packetId);
		packet
			<< io::write_range(buffer);
		packet.Finish();
		
		m_connection->GetCrypt().EncryptSend(reinterpret_cast<uint8*>(&sendBuffer[bufferPos]), game::Crypt::CryptedSendLength);
		m_connection->flush();
	}

	void Player::EnableEnterWorldPacket(const bool enable)
	{
		if (enable)
		{
			RegisterPacketHandler(game::client_realm_packet::EnterWorld, *this, &Player::OnEnterWorld);
		}
		else
		{
			ClearPacketHandler(game::client_realm_packet::EnterWorld);
		}
	}

	void Player::EnableProxyPackets(const bool enable)
	{
		ASSERT(!enable || m_proxyHandlers.IsEmpty());

		// First clear potential existing proxy handlers (always)
		m_proxyHandlers.Clear();

		if (enable)
		{
			// Register proxy handlers
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveStartForward, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveStartBackward, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveStop, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveStartStrafeLeft, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveStartStrafeRight, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveStopStrafe, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveStartTurnLeft, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveStartTurnRight, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveStopTurn, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveHeartBeat, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveSetFacing, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveJump, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveFallLand, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::SetSelection, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CastSpell, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CancelCast, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CancelAura, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ChannelStart, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ChannelUpdate, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CancelChanneling, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AttackSwing, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AttackStop, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::UseItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AttackSwing, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AttackStop, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ReviveRequest, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ForceMoveSetWalkSpeedAck, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ForceMoveSetRunSpeedAck, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ForceMoveSetRunBackSpeedAck, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ForceMoveSetSwimSpeedAck, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ForceMoveSetSwimBackSpeedAck, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ForceMoveSetTurnRateAck, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ForceSetFlightSpeedAck, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ForceSetFlightBackSpeedAck, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveTeleportAck, *this, &Player::OnProxyPacket);

			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AutoStoreLootItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AutoEquipItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AutoStoreBagItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::SwapItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::SwapInvItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::SplitItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AutoEquipItemSlot, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::DestroyItem, *this, &Player::OnProxyPacket);

			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::Loot, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::LootMoney, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::LootRelease, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::GossipHello, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::SellItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::BuyItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AttributePoint, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::TrainerBuySpell, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::QuestGiverStatusQuery, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::TrainerMenu, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::ListInventory, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::QuestGiverHello, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AcceptQuest, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::QuestGiverQueryQuest, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AbandonQuest, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::QuestGiverCompleteQuest, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::QuestGiverChooseQuestReward, *this, &Player::OnProxyPacket);

#if MMO_WITH_DEV_COMMANDS
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatLearnSpell, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatRecharge, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatFaceMe, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatFollowMe, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatCreateMonster, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatDestroyMonster, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatLevelUp, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatGiveMoney, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatAddItem, *this, &Player::OnProxyPacket);
#endif

			RegisterPacketHandler(game::client_realm_packet::ChatMessage, *this, &Player::OnChatMessage);
			RegisterPacketHandler(game::client_realm_packet::NameQuery, *this, &Player::OnNameQuery);
			RegisterPacketHandler(game::client_realm_packet::CreatureQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::ItemQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::QuestQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::SetActionBarButton, *this, &Player::OnSetActionBarButton);
		}
		else
		{
			ClearPacketHandler(game::client_realm_packet::ChatMessage);
			ClearPacketHandler(game::client_realm_packet::NameQuery);
			ClearPacketHandler(game::client_realm_packet::CreatureQuery);
			ClearPacketHandler(game::client_realm_packet::ItemQuery);
			ClearPacketHandler(game::client_realm_packet::QuestQuery);
			ClearPacketHandler(game::client_realm_packet::SetActionBarButton);
		}
	}

	void Player::JoinWorld() const
	{
		if (const auto strongWorld = m_world.lock())
		{
			strongWorld->GetConnection().sendSinglePacket([](auth::OutgoingPacket& outPacket)
			{
				outPacket.Start(auth::realm_world_packet::PlayerCharacterJoin);
				outPacket << io::write<uint32>(0);
				outPacket.Finish();
			});	
		}
	}

	void Player::OnWorldJoined(const InstanceId instanceId)
	{
		DLOG("World join succeeded on instance id " << instanceId);

		EnableProxyPackets(true);

		ASSERT(m_characterData);
		m_characterData->instanceId = instanceId;

		m_connection->sendSinglePacket([&](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::LoginVerifyWorld);
			outPacket
				<< io::write<uint64>(m_characterData->mapId)	
				<< io::write<float>(m_characterData->position.x)
				<< io::write<float>(m_characterData->position.y)
				<< io::write<float>(m_characterData->position.z)
				<< io::write<float>(m_characterData->facing.GetValueRadians())
			;
			outPacket.Finish();
		});

		std::weak_ptr weakThis = shared_from_this();
		auto handler = [weakThis](const std::optional<ActionButtons>& actionButtons)
			{
				if (const auto strongThis = weakThis.lock())
				{
					strongThis->OnActionButtons(*actionButtons);
				}
			};

		m_database.asyncRequest(std::move(handler), &IDatabase::GetActionButtons, m_characterData->characterId);
	}

	void Player::OnWorldJoinFailed(const game::player_login_response::Type response)
	{
		ELOG("World join failed");
		m_characterData.reset();

		m_connection->sendSinglePacket([response](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::EnterWorldFailed);
			outPacket << io::write<uint8>(response);
			outPacket.Finish();
		});
	}

	void Player::OnCharacterData(const std::optional<CharacterData> characterData)
	{
		if (!characterData)
		{
			OnWorldJoinFailed(game::player_login_response::NoCharacter);
			return;
		}

		m_characterData = characterData;

		// Add potentially missing default spells from class data
		std::set<uint32> spellsAdded;
		if (const proto::ClassEntry* classEntry = m_project.classes.getById(m_characterData->classId))
		{
			for (const auto& classSpellEntry : classEntry->spells())
			{
				if (m_characterData->level < classSpellEntry.level())
				{
					continue;
				}

				if (std::find_if(m_characterData->spellIds.begin(), m_characterData->spellIds.end(), [&classSpellEntry](const uint32 spellId)
					{
						return spellId == classSpellEntry.spell();
					}) == m_characterData->spellIds.end())
				{
					m_characterData->spellIds.push_back(classSpellEntry.spell());
					spellsAdded.insert(classSpellEntry.spell());

					DLOG("Added new spell " << classSpellEntry.spell() << " to character because he did not yet know this spell but should have known base on class!");
				}
			}
		}

		// TODO: Persist added spells back in database

		// Find a world node for the character's map id and instance id
		m_world = m_worldManager.GetIdealWorldNode(m_characterData->mapId, m_characterData->instanceId);
		
		// Check if world instance exists
		const auto strongWorld = m_world.lock();
		NotifyWorldNodeChanged(strongWorld.get());

		if (!strongWorld)
		{
			WLOG("No world node available which is able to host map " << m_characterData->mapId << " and/or instance id " << m_characterData->instanceId);

			OnWorldJoinFailed(game::player_login_response::NoWorldServer);

			EnableEnterWorldPacket(true);
			return;
		}

		// Send join request
		std::weak_ptr weakThis = shared_from_this();
		strongWorld->Join(*m_characterData, [weakThis] (const InstanceId instanceId, const bool success)
		{
			const auto strongThis = weakThis.lock();
			if (!strongThis)
			{
				return;
			}
			
			if (success)
			{
				strongThis->OnWorldJoined(instanceId);
			}
			else
			{
				strongThis->OnWorldJoinFailed(game::player_login_response::NoWorldServer);
			}
		});
	
	}

	void Player::OnWorldDestroyed(World& world)
	{
		EnableProxyPackets(false);

		m_world.reset();
		NotifyWorldNodeChanged(nullptr);

		connectionLost();
	}

	void Player::NotifyWorldNodeChanged(World* worldNode)
	{
		m_worldDestroyed.disconnect();

		if (worldNode)
		{
			m_worldDestroyed = worldNode->destroyed.connect(this, &Player::OnWorldDestroyed);
		}
	}

	void Player::OnQueryCreature(uint64 entry)
	{
		DLOG("Querying for creature " << log_hex_digit(entry) << "...");

		const proto::UnitEntry* unit = m_project.units.getById(entry);
		if (unit == nullptr)
		{
			WLOG("Could not find creature entry " << log_hex_digit(entry));
			m_connection->sendSinglePacket([entry](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::CreatureQueryResult);
					packet
						<< io::write_packed_guid(entry)
						<< io::write<uint8>(false);
					packet.Finish();
				});
			return;
		}

		m_connection->sendSinglePacket([unit](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::CreatureQueryResult);
				packet
					<< io::write_packed_guid(unit->id())
					<< io::write<uint8>(true)
					<< io::write_range(unit->name()) << io::write<uint8>(0)
					<< io::write_range(unit->subname()) << io::write<uint8>(0);
				packet.Finish();
			});
	}

	void Player::OnQueryQuest(uint64 entry)
	{
		DLOG("Querying for quest " << log_hex_digit(entry) << "...");

		// Check for existing quest entry
		const proto::QuestEntry* questEntry = m_project.quests.getById(entry);
		if (!questEntry)
		{
			m_connection->sendSinglePacket([entry](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::QuestQueryResult);
					packet
						<< io::write_packed_guid(entry)
						<< io::write<uint8>(false);
					packet.Finish();
				});
		}

		// Map quest info
		QuestInfo quest;
		quest.id = questEntry->id();
		quest.title = questEntry->name();
		quest.description = questEntry->detailstext();
		quest.summary = questEntry->objectivestext();

		quest.questLevel = questEntry->questlevel();
		quest.rewardMoney = questEntry->rewardmoney();
		quest.rewardXp = questEntry->rewardxp();

		for (const auto& requirement : questEntry->requirements())
		{
			if (requirement.itemid() != 0)
			{
				quest.requiredItems.emplace_back(requirement.itemid(), requirement.itemcount());
			}
			else if(requirement.creatureid() != 0)
			{
				quest.requiredCreatures.emplace_back(requirement.creatureid(), requirement.creaturecount());
			}
		}

		for (const auto& reward : questEntry->rewarditems())
		{
			quest.rewardItems.emplace_back(reward.itemid(), reward.count());
		}

		for (const auto& reward : questEntry->rewarditemschoice())
		{
			quest.optionalItems.emplace_back(reward.itemid(), reward.count());
		}

		m_connection->sendSinglePacket([entry, &quest](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::QuestQueryResult);
				packet
					<< io::write_packed_guid(entry)
					<< io::write<uint8>(true)
					<< quest;
				packet.Finish();
			});
	}

	void Player::OnQueryItem(uint64 entry)
	{
		DLOG("Querying for item " << log_hex_digit(entry) << "...");

		const proto::ItemEntry* itemEntry = m_project.items.getById(entry);
		if (!itemEntry)
		{
			WLOG("Item with entry " << entry << " could not be found!");
			m_connection->sendSinglePacket([entry](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::ItemQueryResult);
					packet
						<< io::write_packed_guid(entry)
						<< io::write<uint8>(false);
					packet.Finish();
				});
		}

		// Map item entry
		ItemInfo info;
		info.name = itemEntry->name();
		info.description = itemEntry->description();
		info.id = entry;
		info.itemClass = itemEntry->itemclass();
		info.itemSubclass = itemEntry->subclass();
		info.displayId = itemEntry->displayid();
		info.quality = itemEntry->quality();
		info.flags = itemEntry->flags();
		info.buyCount = itemEntry->buycount();
		info.buyPrice = itemEntry->buyprice();
		info.sellPrice = itemEntry->sellprice();
		info.inventoryType = itemEntry->inventorytype();
		info.allowedClasses = itemEntry->allowedclasses();
		info.allowedRaces = itemEntry->allowedraces();
		info.itemlevel = itemEntry->itemlevel();
		info.requiredlevel = itemEntry->requiredlevel();
		info.requiredskill = itemEntry->requiredskill();
		info.requiredskillrank = itemEntry->requiredskillrank();
		info.requiredspell = itemEntry->requiredspell();
		info.requiredrep = itemEntry->requiredrep();
		info.requiredreprank = itemEntry->requiredreprank();
		info.requiredcityrank = itemEntry->requiredcityrank();
		info.requiredrep = itemEntry->requiredrep();
		info.requiredreprank = itemEntry->requiredreprank();
		info.maxcount = itemEntry->maxcount();
		info.maxstack = itemEntry->maxstack();
		info.containerslots = itemEntry->containerslots();

		for (int i = 0; i < 10; ++i)
		{
			if (i >= itemEntry->stats_size())
			{
				info.stats[i].type = -1;
				info.stats[i].value = 0;
			}
			else
			{
				const auto& stat = itemEntry->stats(i);
				info.stats[i].type = stat.type();
				info.stats[i].value = stat.value();
			}
		}

		info.damage.type = itemEntry->damage().type();
		info.damage.min = itemEntry->damage().mindmg();
		info.damage.max = itemEntry->damage().maxdmg();

		for (int i = 0; i < 5; ++i)
		{
			if (i >= itemEntry->spells_size())
			{
				info.spells[i].spellId = -1;
				info.spells[i].triggertype = 0;
			}
			else
			{
				const auto& spell = itemEntry->spells(i);
				info.spells[i].spellId = spell.spell();
				info.spells[i].triggertype = spell.trigger();
			}
		}

		info.armor = itemEntry->armor();
		info.resistance[0] = itemEntry->holyres();
		info.resistance[1] = itemEntry->fireres();
		info.resistance[2] = itemEntry->natureres();
		info.resistance[3] = itemEntry->frostres();
		info.resistance[4] = itemEntry->shadowres();
		info.resistance[5] = itemEntry->arcaneres();
		info.ammotype = itemEntry->ammotype();

		info.bonding = itemEntry->bonding();
		info.lockid = itemEntry->lockid();
		info.sheath = itemEntry->sheath();
		info.randomproperty = itemEntry->randomproperty();
		info.randomsuffix = itemEntry->randomsuffix();
		info.block = itemEntry->block();
		info.itemset = itemEntry->itemset();
		info.material = itemEntry->material();
		info.maxdurability = itemEntry->durability();
		info.area = itemEntry->area();
		info.extraflags = itemEntry->extraflags();
		info.startquestid = itemEntry->questentry();
		info.skill = itemEntry->skill();
		info.icon = itemEntry->icon();

		m_connection->sendSinglePacket([entry, &info](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::ItemQueryResult);
				packet
					<< io::write_packed_guid(entry)
					<< io::write<uint8>(true)
					<< info;
				packet.Finish();
			});
	}

	void Player::OnActionButtons(const ActionButtons& actionButtons)
	{
		m_actionButtons = actionButtons;

		m_connection->sendSinglePacket([&actionButtons](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::ActionButtons);
				packet << io::write_range(actionButtons);
				packet.Finish();
			});
	}

	void Player::RegisterPacketHandler(uint16 opCode, PacketHandler && handler)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		const auto it = m_packetHandlers.find(opCode);
		if (it == m_packetHandlers.end())
		{
			m_packetHandlers.emplace(std::make_pair(opCode, std::forward<PacketHandler>(handler)));
		}
		else
		{
			it->second = std::forward<PacketHandler>(handler);
		}
	}

	void Player::ClearPacketHandler(const uint16 opCode)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		if (auto it = m_packetHandlers.find(opCode); it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}
}
