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

#include "player_group.h"
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
		const proto::Project& project,
		IdGenerator<uint64>& groupIdGenerator)
		: m_timerQueue(timerQueue)
		, m_manager(playerManager)
		, m_worldManager(worldManager)
		, m_loginConnector(loginConnector)
		, m_database(database)
		, m_project(project)
		, m_groupIdGenerator(groupIdGenerator)
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

		// Decline group invite if any
		if (m_inviterGuid != 0)
		{
			DeclineGroupInvite();
		}

		if (const auto strongWorld = m_world.lock())
		{
			if (HasCharacterGuid())
			{
				strongWorld->Leave(GetCharacterGuid(), auth::world_left_reason::Disconnect);
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

	void Player::OnGroupLoaded(PlayerGroup& group)
	{
		m_onGroupLoaded.disconnect();

		ASSERT(group.IsLoaded());
		group.SendUpdate();
	}

	void Player::connectionLost()
	{
		ILOG("Client " << m_address << " disconnected");
		Destroy();
	}

	void Player::connectionMalformedPacket()
	{
		ILOG("Client " << m_address << " sent malformed packet");

		// Remove player character from the world node
		if (auto strongWorld = m_world.lock(); strongWorld && m_characterData)
		{
			strongWorld->Leave(m_characterData->characterId, auth::world_left_reason::Disconnect);
		}

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
			if (const auto strongThis = weakThis.lock())
			{
				strongThis->OnCharacterData(characterData);
			}
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::CharacterEnterWorld, guid, m_accountId);
		
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

		std::map<uint8, ActionButton> actionButtons;

		std::vector<uint32> spellIds;
		spellIds.reserve(classInstance->spells().size());
		for(const auto& spell : classInstance->spells())
		{
			const proto::SpellEntry* spellEntry = m_project.spells.getById(spell.spell());
			if (spellEntry && actionButtons.size() < 12)
			{
				// Not a passive, not hidden and an ability? Put it in the action bar!
				if ((spellEntry->attributes(0) & spell_attributes::Passive) == 0 &&
					(spellEntry->attributes(0) & spell_attributes::HiddenClientSide) == 0 &&
					(spellEntry->attributes(0) & spell_attributes::Ability) != 0)
				{
					actionButtons[actionButtons.size()] = ActionButton{ static_cast<uint16>(spell.spell()), action_button_type::Spell };
				}
			}

			if (spell.level() <= level)
			{
				spellIds.push_back(spell.spell());
			}
		}

		// Each spell which isn't a passive should (for now) be placed on the action bar
		DLOG("Creating new character named '" << characterName << "' for account 0x" << std::hex << m_accountId << " (Race: " << raceEntry->id() << "; Class: " << classInstance->id() << "; Gender: " << (uint16)gender << ")...");
		m_database.asyncRequest(std::move(handler), &IDatabase::CreateCharacter, characterName, this->m_accountId, map, level, hp, gender, race, characterClass, position, rotation,
			spellIds, mana, rage, energy, actionButtons);
		
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
			WLOG("Received proxy packet from character without a world!");
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
			if (!m_group || !m_group->IsMember(m_characterData->characterId))
			{
				WLOG("Player tried to send group chat message without being in a group!");
				break;
			}
			m_group->BroadcastPacket([this, &message, chatType](game::OutgoingPacket& outPacket)
				{
					outPacket.Start(game::realm_client_packet::ChatMessage);
					outPacket
						<< io::write_packed_guid(m_characterData->characterId)
						<< io::write<uint8>(chatType)
						<< io::write_range(message)
						<< io::write<uint8>(0)
						<< io::write<uint8>(0);
					outPacket.Finish();
				});
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
			// We have to look up the player name in the database
			std::weak_ptr weakThis = shared_from_this();
			auto handler = [weakThis, guid](std::optional<String>& playerName) {
				if (const auto strongThis = weakThis.lock())
				{
					strongThis->m_connection->sendSinglePacket([guid, &playerName](game::OutgoingPacket& packet)
						{
							packet.Start(game::realm_client_packet::NameQueryResult);
							packet
								<< io::write_packed_guid(guid)
								<< io::write<uint8>(playerName ? true : false);
								if (playerName)
								{
									packet
										<< io::write_range(*playerName) << io::write<uint8>(0);
								}
							packet.Finish();
						});
				}
			};
			m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterNameById, guid);
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

	PacketParseResult Player::OnMoveWorldPortAck(game::IncomingPacket& packet)
	{
		DLOG("Received MoveWorldPortAck from player " << log_hex_digit(m_characterData->characterId));

		ASSERT(!m_newWorldAckHandler.IsEmpty());
		m_newWorldAckHandler.Clear();

		m_characterData->mapId = m_transferMap;
		m_characterData->position = m_transferPosition;
		m_characterData->facing = m_transferFacing;

		// Find a new world node
		std::shared_ptr<World> world = m_worldManager.GetIdealWorldNode(m_transferMap, InstanceId());
		if (!world)
		{
			// World does not exist
			WLOG("Player login failed: Could not find world server for map " << m_transferMap);
			OnWorldTransferAborted(game::transfer_abort_reason::NotFound);
			return PacketParseResult::Pass;
		}

		std::weak_ptr weakThis = shared_from_this();
		std::weak_ptr weakWorld = world;
		world->Join(*m_characterData, [weakThis, weakWorld](const InstanceId instanceId, const bool success)
			{
				const auto strongThis = weakThis.lock();
				if (!strongThis)
				{
					return;
				}

				// World server still connected?
				auto strongWorld = weakWorld.lock();
				if (!strongWorld)
				{
					strongThis->OnWorldTransferAborted(game::transfer_abort_reason::NotFound);
					return;
				}

				if (success)
				{
					strongThis->OnWorldChanged(strongWorld, instanceId);
				}
				else
				{
					// TODO: Get exact reason
					strongThis->OnWorldTransferAborted(game::transfer_abort_reason::NotFound);
				}
			});

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGroupInvite(game::IncomingPacket& packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		// Try to find that player
		auto player = m_manager.GetPlayerByCharacterName(playerName);
		if (!player)
		{
			WLOG("Unable to invite player: Player " << playerName << " not found (may be offline)");
			SendPartyOperationResult(party_operation::Invite, party_result::CantFindTarget, playerName);
			return PacketParseResult::Pass;
		}

		if (player == this)
		{
			WLOG("Player wants to invite himself");
			SendPartyOperationResult(party_operation::Invite, party_result::CantInviteYourself, playerName);
			return PacketParseResult::Pass;
		}

		if (player->GetGroup() != nullptr)
		{
			WLOG("Unable to invite player: Player " << playerName << " is already a member of a group");
			SendPartyOperationResult(party_operation::Invite, party_result::AlreadyInGroup, playerName);
			return PacketParseResult::Pass;
		}

		// Are we already in a group?
		if (!m_group)
		{
			// Not yet in a group - create a new one!
			m_group = std::make_shared<PlayerGroup>(m_groupIdGenerator.GenerateId(), m_manager, m_database);
			m_group->Create(m_characterData->characterId, m_characterData->name);
			GetWorld()->NotifyPlayerGroupChanged(m_characterData->characterId, m_group->GetId());
		}
		else if (!m_group->IsLeaderOrAssistant(m_characterData->characterId))
		{
			WLOG("Unable to invite player: Player has no right to invite other players");
			SendPartyOperationResult(party_operation::Invite, party_result::YouNotLeader, playerName);
			return PacketParseResult::Pass;
		}

		const auto result = m_group->AddInvite(player->GetCharacterGuid());
		if (result != party_result::Ok)
		{
			SendPartyOperationResult(party_operation::Invite, result, playerName);
			return PacketParseResult::Pass;
		}

		player->SetGroup(m_group);
		player->SetInviterGuid(m_characterData->characterId);
		player->SendPartyInvite(m_characterData->name);

		SendPartyOperationResult(party_operation::Invite, party_result::Ok, playerName);
		m_group->SendUpdate();

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGroupUninvite(game::IncomingPacket& packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!m_group)
		{
			WLOG("Player tried to uninvite player from group without being in a group");
			SendPartyOperationResult(party_operation::Leave, party_result::YouNotInGroup, playerName);
			return PacketParseResult::Pass;
		}

		ASSERT(m_characterData);
		if (!m_group->IsLeaderOrAssistant(m_characterData->characterId))
		{
			WLOG("Player tried to uninvite player from group without being the leader or assistant");
			SendPartyOperationResult(party_operation::Leave, party_result::YouNotLeader, playerName);
			return PacketParseResult::Pass;
		}

		const uint64 guid = m_group->GetMemberGuid(playerName);
		if (guid == 0)
		{
			WLOG("Player tried to uninvite player " << playerName << " from group who is not in the group");
			SendPartyOperationResult(party_operation::Leave, party_result::NotInYourParty, playerName);
			return PacketParseResult::Pass;
		}

		// Assistants may not kick the leader
		if (m_group->GetLeader() != m_characterData->characterId &&
			m_group->GetLeader() == guid)
		{
			WLOG("Player tried to uninvite player " << playerName << " from group who is the leader");
			SendPartyOperationResult(party_operation::Leave, party_result::YouNotLeader, playerName);
			return PacketParseResult::Pass;
		}

		DLOG("Player wants to kick player " << playerName << " from group");
		m_group->RemoveMember(guid);

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGroupAccept(game::IncomingPacket& packet)
	{
		if (!m_group || m_inviterGuid == 0)
		{
			ELOG("Player tried to accept group invite with no pending group invite");
			return PacketParseResult::Disconnect;
		}

		DLOG("Player accepted group invite");

		auto result = m_group->AddMember(m_characterData->characterId, m_characterData->name);
		if (result != party_result::Ok)
		{
			ELOG("Failed to add player to group: " << result);
			DeclineGroupInvite();
			return PacketParseResult::Pass;
		}

		m_inviterGuid = 0;

		const auto world = GetWorld();
		ASSERT(world);
		world->NotifyPlayerGroupChanged(m_characterData->characterId, m_group->GetId());

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGroupDecline(game::IncomingPacket& packet)
	{
		DeclineGroupInvite();

		return PacketParseResult::Pass;
	}

#ifdef MMO_WITH_DEV_COMMANDS
	PacketParseResult Player::OnCheatTeleportToPlayer(game::IncomingPacket& packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		// Resolve player by name
		Player* player = m_manager.GetPlayerByCharacterName(playerName);
		if (!player)
		{
			// We could not find a player currently online with the given name, so we ask the database and teleport to the location the character logged out
			std::weak_ptr weakThis{ shared_from_this() };
			auto handler = [weakThis, playerName](const std::optional<CharacterLocationData>& result) {
				if (auto strongThis = weakThis.lock())
				{
					if (!result)
					{
						WLOG("Failed to teleport to player because no character named '" << playerName << "' exists!");
						return;
					}

					strongThis->SendTeleportRequest(result->map, result->position, result->facing);
				}
			};

			// Execute
			m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterLocationDataByName, playerName);
			return PacketParseResult::Pass;
		}

		// Player is currently connected, so we need to get the players location data first by asking the world node the player is currently connected to
		std::weak_ptr weakThis{ shared_from_this() };
		player->FetchCharacterLocationAsync([weakThis](bool succeeded, uint32 mapId, Vector3 position, Radian facing)
			{
				auto strongThis = weakThis.lock();
				if (!strongThis)
				{
					return;
				}

				if (!succeeded)
				{
					ELOG("Failed to fetch player position from world node, world node reported an error");
					return;
				}

				// Teleport player to the location
				DLOG("Teleporting player to map " << mapId << ", location " << position << "...");
				strongThis->SendTeleportRequest(mapId, position, facing);
			});

		return PacketParseResult::Pass;
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	PacketParseResult Player::OnCheatSummon(game::IncomingPacket& packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		std::weak_ptr weakThis{ shared_from_this() };
		FetchCharacterLocationAsync([weakThis, playerName](bool succeeded, uint32 mapId, Vector3 position, Radian facing)
			{
				std::shared_ptr<Player> strongThis = weakThis.lock();
				if (!strongThis)
				{
					return;
				}

				// Resolve player by name
				Player* player = strongThis->m_manager.GetPlayerByCharacterName(playerName);
				if (!player)
				{
					// We could not find a player currently online with the given name, so we just teleport him in the database so that when he logs back in, he will be at the new location
					auto handler = [weakThis, playerName](bool succeeded) {
						if (auto strongThis = weakThis.lock())
						{
							if (!succeeded)
							{
								WLOG("Failed to summon player '" << playerName << "' in database because no such character exists");
							}
						}
						};

					// Execute
					strongThis->m_database.asyncRequest(std::move(handler), &IDatabase::TeleportCharacterByName, playerName, mapId, position, facing);
					return;
				}

				// Send teleport request
				player->SendTeleportRequest(mapId, position, facing);
			});

		return PacketParseResult::Pass;
	}
#endif

	bool Player::InitializeTransfer(uint32 map, const Vector3& location, const float o, bool shouldLeaveNode)
	{
		const std::shared_ptr<World> world = m_worldManager.GetWorldByInstanceId(m_instanceId);
		if (shouldLeaveNode && !world)
		{
			return false;
		}

		m_transferMap = map;
		m_transferPosition = location;
		m_transferFacing = o;

		if (shouldLeaveNode)
		{
			// Send transfer pending state. This will show up the loading screen at the client side
			m_connection->sendSinglePacket([map](game::OutgoingPacket& packet) {
				packet.Start(game::realm_client_packet::TransferPending);
				packet << io::write<uint32>(map);
				packet.Finish();
				});
			world->Leave(m_characterData->characterId, auth::world_left_reason::Teleport);
		}

		return true;
	}

	void Player::CommitTransfer()
	{
		ILOG("Committing pending world transfer...");

		if (!m_newWorldAckHandler.IsEmpty())
		{
			ELOG("Failed to commit transfer because a pending transfer seems to exist!");
			return;
		}

		m_connection->sendSinglePacket([this](game::OutgoingPacket& packet) {
			packet.Start(game::realm_client_packet::NewWorld);
			packet
				<< io::write<uint32>(m_transferMap)
				<< io::write<float>(m_transferPosition.x)
				<< io::write<float>(m_transferPosition.y)
				<< io::write<float>(m_transferPosition.z)
				<< io::write<float>(m_transferFacing);
			packet.Finish();
			});

		m_newWorldAckHandler += RegisterAutoPacketHandler(game::client_realm_packet::MoveWorldPortAck, *this, &Player::OnMoveWorldPortAck);
	}

	void Player::SetInviterGuid(uint64 inviter)
	{
		m_inviterGuid = inviter;
	}

	void Player::SendPartyInvite(const String& inviterName)
	{
		m_connection->sendSinglePacket([&inviterName](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::GroupInvite);
				packet << io::write_dynamic_range<uint8>(inviterName);
				packet.Finish();
			});
	}

	void Player::DeclineGroupInvite()
	{
		if (m_inviterGuid == 0)
		{
			return;
		}

		ASSERT(m_characterData);
		ASSERT(m_group);

		DLOG("Pending group invite from " << log_hex_digit(m_inviterGuid) << " declined by player " << log_hex_digit(m_characterData->characterId));

		// Find the group leader
		if (!m_group->RemoveInvite(m_characterData->characterId))
		{
			return;
		}

		// We are no longer a member of this group
		m_group.reset();

		// Notify the inviter if possible
		const auto player = m_manager.GetPlayerByCharacterGuid(m_inviterGuid);
		m_inviterGuid = 0;
		if (player)
		{
			const String& inviterName = GetCharacterName();
			player->SendPacket(
				[&inviterName](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::GroupDecline);
					packet << io::write_dynamic_range<uint8>(inviterName);
					packet.Finish();
				}
			);
		}
	}

	void Player::BuildPartyMemberStatsPacket(game::OutgoingPacket& packet, const uint32 groupUpdateFlags) const
	{
		packet.Start(game::realm_client_packet::PartyMemberStats);
		packet
			<< io::write_packed_guid(m_characterData->characterId)
			<< io::write<uint32>(groupUpdateFlags);

		if (groupUpdateFlags & group_update_flags::Status)
		{
			uint16 flags = 0;
			flags = group_member_status::Online;
			packet << io::write<uint16>(flags);
		}

		if (groupUpdateFlags & group_update_flags::CurrentHP)
		{
			packet << io::write<uint16>(m_characterData->hp);
		}

		if (groupUpdateFlags & group_update_flags::MaxHP)
		{
			packet << io::write<uint16>(m_characterData->maxHp);
		}

		const uint32 powerType = m_characterData->powerType;
		if (groupUpdateFlags & group_update_flags::PowerType)
		{
			packet << io::write<uint8>(powerType);
		}

		if (groupUpdateFlags & group_update_flags::CurrentPower)
		{
			switch (powerType)
			{
			case power_type::Mana: packet << io::write<uint16>(m_characterData->mana); break;
			case power_type::Rage: packet << io::write<uint16>(m_characterData->rage); break;
			case power_type::Energy: packet << io::write<uint16>(m_characterData->energy); break;
			default: packet << io::write<uint16>(0); break;
			}
		}

		if (groupUpdateFlags & group_update_flags::MaxPower)
		{
			switch (powerType)
			{
			case power_type::Mana: packet << io::write<uint16>(m_characterData->maxMana); break;
			case power_type::Rage: packet << io::write<uint16>(m_characterData->maxRage); break;
			case power_type::Energy: packet << io::write<uint16>(m_characterData->maxEnergy); break;
			default: packet << io::write<uint16>(0); break;
			}
		}

		if (groupUpdateFlags & group_update_flags::Level)
		{
			packet << io::write<uint16>(m_characterData->level);
		}

		if (groupUpdateFlags & group_update_flags::Zone)
		{
			packet << io::write<uint16>(0);
		}

		if (groupUpdateFlags & group_update_flags::Position)
		{
			const Vector3 location(m_characterData->position);
			packet << io::write<float>(location.x) << io::write<float>(location.y) << io::write<float>(location.z);
		}

		packet.Finish();
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
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveEnded, *this, &Player::OnProxyPacket);

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
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::RandomRoll, *this, &Player::OnProxyPacket);
			
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
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatWorldPort, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::CheatSpeed, *this, &Player::OnProxyPacket);
#endif

			RegisterPacketHandler(game::client_realm_packet::ChatMessage, *this, &Player::OnChatMessage);
			RegisterPacketHandler(game::client_realm_packet::NameQuery, *this, &Player::OnNameQuery);
			RegisterPacketHandler(game::client_realm_packet::CreatureQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::ItemQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::QuestQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::SetActionBarButton, *this, &Player::OnSetActionBarButton);
			RegisterPacketHandler(game::client_realm_packet::GroupInvite, *this, &Player::OnGroupInvite);
			RegisterPacketHandler(game::client_realm_packet::GroupUninvite, *this, &Player::OnGroupUninvite);
			RegisterPacketHandler(game::client_realm_packet::GroupAccept, *this, &Player::OnGroupAccept);
			RegisterPacketHandler(game::client_realm_packet::GroupDecline, *this, &Player::OnGroupDecline);

#if MMO_WITH_DEV_COMMANDS
			RegisterPacketHandler(game::client_realm_packet::CheatTeleportToPlayer, *this, &Player::OnCheatTeleportToPlayer);
			RegisterPacketHandler(game::client_realm_packet::CheatSummon, *this, &Player::OnCheatSummon);
#endif
		}
		else
		{
			ClearPacketHandler(game::client_realm_packet::ChatMessage);
			ClearPacketHandler(game::client_realm_packet::NameQuery);
			ClearPacketHandler(game::client_realm_packet::CreatureQuery);
			ClearPacketHandler(game::client_realm_packet::ItemQuery);
			ClearPacketHandler(game::client_realm_packet::QuestQuery);
			ClearPacketHandler(game::client_realm_packet::SetActionBarButton);
			ClearPacketHandler(game::client_realm_packet::GroupInvite);
			ClearPacketHandler(game::client_realm_packet::GroupUninvite);
			ClearPacketHandler(game::client_realm_packet::GroupAccept);
			ClearPacketHandler(game::client_realm_packet::GroupDecline);

#if MMO_WITH_DEV_COMMANDS
			ClearPacketHandler(game::client_realm_packet::CheatTeleportToPlayer);
			ClearPacketHandler(game::client_realm_packet::CheatSummon);
#endif
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

	void Player::OnWorldJoined(const std::shared_ptr<World>& world, const InstanceId instanceId)
	{
		DLOG("World join succeeded on instance id " << instanceId);

		m_world = world;
		m_instanceId = instanceId;
		NotifyWorldNodeChanged(world.get());

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

		// If we have a group, either it is already loaded (in which case we just send the group data to all members) or we wait for it to load
		if (m_group)
		{
			if (m_group->IsLoaded())
			{
				m_group->SendUpdate();
			}
			else
			{
				m_onGroupLoaded = m_group->loaded.connect(this, &Player::OnGroupLoaded);
			}
		}

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

	void Player::OnWorldChanged(const std::shared_ptr<World>& world, const InstanceId instanceId)
	{
		DLOG("World change succeeded on instance id " << instanceId);

		m_world = world;
		m_instanceId = instanceId;
		NotifyWorldNodeChanged(world.get());

		ASSERT(m_characterData);
		m_characterData->instanceId = instanceId;
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

	void Player::OnWorldTransferAborted(const game::TransferAbortReason reason)
	{
		ELOG("World transfer aborted: " << reason);

		m_connection->sendSinglePacket([this, reason](game::OutgoingPacket& outPacket)
			{
				outPacket.Start(game::realm_client_packet::TransferAborted);
				outPacket
					<< io::write<uint32>(m_transferMap)
					<< io::write<uint8>(reason);
				outPacket.Finish();
			});
	}

	void Player::OnWorldLeft(const std::shared_ptr<World>& world, auth::WorldLeftReason reason)
	{
		// We no longer care about the world node
		m_worldDestroyed.disconnect();
		m_world.reset();

		// Save action bar
		if (m_characterData && m_pendingButtons)
		{
			m_database.asyncRequest([](bool) {}, &IDatabase::SetCharacterActionButtons, m_characterData->characterId, m_actionButtons);
			m_pendingButtons = false;
		}

		switch (reason)
		{
		case auth::world_left_reason::Logout:
			DLOG("TODO!");
			break;

		case auth::world_left_reason::Teleport:
			// We were removed from the old world node - now we can move on to the new one
			CommitTransfer();
			break;

		case auth::world_left_reason::Disconnect:
			// Finally destroy this instance
			ILOG("Left world instance because of a disconnect");
			Destroy();
			break;

		default:
			// Unknown reason?
			WLOG("Player left world instance for unknown reason...");
			break;
		}
	}

	void Player::OnCharacterData(const std::optional<CharacterData> characterData)
	{
		if (!characterData)
		{
			OnWorldJoinFailed(game::player_login_response::NoCharacter);
			return;
		}

		m_characterData = characterData;

		if (m_characterData->groupId != 0)
		{
			auto groupIt = PlayerGroup::ms_groupsById.find(m_characterData->groupId);
			if (groupIt != PlayerGroup::ms_groupsById.end())
			{
				m_group = groupIt->second;
				m_group->CreateFromDatabase();
			}
			else
			{
				ELOG("Failed to restore character group: Group does not seem to exist!");
				m_characterData->groupId = 0;
			}
		}

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
		auto world = m_worldManager.GetIdealWorldNode(m_characterData->mapId, m_characterData->instanceId);
		if (!world)
		{
			WLOG("No world node available which is able to host map " << m_characterData->mapId << " and/or instance id " << m_characterData->instanceId);

			OnWorldJoinFailed(game::player_login_response::NoWorldServer);

			EnableEnterWorldPacket(true);
			return;
		}

		// Send join request
		std::weak_ptr weakThis = shared_from_this();
		std::weak_ptr weakWorld = world;
		world->Join(*m_characterData, [weakThis, weakWorld] (const InstanceId instanceId, const bool success)
		{
			const auto strongThis = weakThis.lock();
			if (!strongThis)
			{
				return;
			}

			// World server still connected?
			auto strongWorld = weakWorld.lock();
			if (!strongWorld)
			{
				strongThis->OnWorldJoinFailed(game::player_login_response::NoWorldServer);
				return;
			}

			if (success)
			{
				strongThis->OnWorldJoined(strongWorld, instanceId);
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
			return;
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
			return;
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
		info.attackTime = itemEntry->delay();

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

	void Player::FetchCharacterLocationAsync(CharacterLocationAsyncCallback&& callback)
	{
		// Check if we are currently connected to a world node
		std::shared_ptr<World> world = m_world.lock();
		if (!world)
		{
			callback(false, 0, Vector3::Zero, Radian());
			return;
		}

		// Send a request to the world node to get the character's location data
		const uint64 ackId = m_callbackIdGenerator.GenerateId();
		ASSERT(!m_characterLocationCallbacks.contains(ackId));
		m_characterLocationCallbacks.emplace(ackId, std::move(callback));

		world->SendFetchLocationRequestAsync(m_characterData->characterId, ackId);
	}

	void Player::CharacterLocationResponseNotification(const bool succeeded, const uint64 ackId, const uint32 mapId, const Vector3& position, const Radian& facing)
	{
		// Check if we have a callback
		const auto it = m_characterLocationCallbacks.find(ackId);
		if (it == m_characterLocationCallbacks.end())
		{
			ELOG("Received character location response for unknown ack id " << ackId);
			return;
		}

		// Execute callback
		it->second(succeeded, mapId, position, facing);
		m_characterLocationCallbacks.erase(it);
	}

	PacketParseResult Player::OnGroupUpdate(auth::IncomingPacket& packet)
	{
		std::vector<uint64> nearbyMembers;
		uint32 health, maxHealth, power, maxPower, mapId, zoneId;
		uint8 powerType, level;
		Vector3 location;

		if (!(packet 
			>> io::read_container<uint8>(nearbyMembers)
			>> io::read<uint32>(health)
			>> io::read<uint32>(maxHealth)
			>> io::read<uint8>(powerType)
			>> io::read<uint32>(power)
			>> io::read<uint32>(maxPower)
			>> io::read<uint8>(level)
			>> io::read<uint32>(mapId)
			>> io::read<uint32>(zoneId)
			>> io::read<float>(location.x)
			>> io::read<float>(location.y)
			>> io::read<float>(location.z)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!m_group)
		{
			WLOG("Received group update packet without being in a group!");
			return PacketParseResult::Pass;
		}

		m_characterData->hp = health;
		m_characterData->maxHp = maxHealth;
		m_characterData->powerType = powerType;

		switch (powerType)
		{
		case power_type::Mana:		m_characterData->mana = power; m_characterData->maxMana = maxPower;		break;
		case power_type::Rage:		m_characterData->rage = power; m_characterData->maxRage = maxPower;		break;
		case power_type::Energy:	m_characterData->energy = power; m_characterData->maxEnergy= maxPower;	break;
		}

		m_characterData->level = level;
		m_characterData->position = location;
		m_characterData->mapId = mapId;

		nearbyMembers.push_back(m_characterData->characterId);

		// Broadcast to far members
		m_group->BroadcastPacket([&](game::OutgoingPacket& packet)
		{
			BuildPartyMemberStatsPacket(packet, group_update_flags::Full);
		}, &nearbyMembers, m_characterData->characterId);

		return PacketParseResult::Pass;
	}

	void Player::SendTeleportRequest(uint32 mapId, const Vector3& position, const Radian& facing) const
	{
		// Check if we are currently connected to a world node
		std::shared_ptr<World> world = m_world.lock();
		if (!world)
		{
			return;
		}

		// Send a request to the world node to teleport the character
		world->SendTeleportRequest(m_characterData->characterId, mapId, position, facing);
	}

	void Player::SendPartyOperationResult(PartyOperation operation, PartyResult result, const String& playerName)
	{
		m_connection->sendSinglePacket([operation, result, playerName](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::PartyCommandResult);
				packet
					<< io::write<uint8>(operation)
					<< io::write_dynamic_range<uint8>(playerName)
					<< io::write<uint8>(result);
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
