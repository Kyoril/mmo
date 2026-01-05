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

#include "guild_mgr.h"
#include "friend_mgr.h"
#include "player_group.h"
#include "base/utilities.h"
#include "game/chat_type.h"
#include "game/guild_info.h"
#include "game/object_info.h"
#include "game/quest_info.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "game_server/objects/game_player_s.h"

namespace mmo
{
	Player::Player(
		TimerQueue &timerQueue,
		PlayerManager &playerManager,
		WorldManager &worldManager,
		LoginConnector &loginConnector,
		AsyncDatabase &database,
		std::shared_ptr<Client> connection,
		String address,
		const proto::Project &project,
		IdGenerator<uint64> &groupIdGenerator,
		GuildMgr &guildMgr,
		FriendMgr &friendMgr)
		: m_timerQueue(timerQueue), m_manager(playerManager), m_worldManager(worldManager), m_loginConnector(loginConnector), m_database(database), m_project(project), m_groupIdGenerator(groupIdGenerator), m_connection(std::move(connection)), m_address(std::move(address)), m_accountId(0), m_guildMgr(guildMgr), m_friendMgr(friendMgr)
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

		// Notify the group that the player is leaving
		if (m_group && m_characterData)
		{
			m_group->NotifyMemberDisconnected(m_characterData->characterId);
		}

		if (m_characterData && m_characterData->guildId != 0)
		{
			if (Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId))
			{
				guild->BroadcastEvent(guild_event::LoggedOut, m_characterData->characterId, m_characterData->name.c_str());
			}
			else
			{
				ELOG("Player " << log_hex_digit(m_characterData->characterId) << " is in guild " << log_hex_digit(m_characterData->guildId) << " which could not be found (not loaded from database?)");
			}
		}

		// Notify friends that this player is going offline
		if (m_characterData)
		{
			m_friendMgr.NotifyFriendStatusChange(m_characterData->characterId, false);
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
		std::weak_ptr weakThis{shared_from_this()};
		auto handler = [weakThis](std::optional<std::vector<CharacterView>> result)
		{
			if (auto strongThis = weakThis.lock())
			{
				// Lock around m_characterViews
				{
					std::scoped_lock lock{strongThis->m_charViewMutex};

					// Copy character view data
					strongThis->m_characterViews.clear();
					for (auto &charView : result.value())
					{
						// Resolve display id from race/class (TODO: Maybe do this somewhere else or differently? This seems like a wrong place to do this, but it works :/)
						if (const auto *raceEntry = strongThis->m_project.races.getById(charView.GetRaceId()))
						{
							charView.SetDisplayId(charView.GetGender() == Male ? raceEntry->malemodel() : raceEntry->femalemodel());
						}

						strongThis->m_characterViews[charView.GetGuid()] = charView;
					}
				}

				// Send result data to requesting client
				strongThis->GetConnection().sendSinglePacket([&result](game::OutgoingPacket &outPacket)
															 {
						outPacket.Start(game::realm_client_packet::CharEnum);
						outPacket << io::write_dynamic_range<uint8>(*result);
						outPacket.Finish(); });
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

	void Player::OnGroupLoaded(PlayerGroup &group)
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

	PacketParseResult Player::connectionPacketReceived(game::IncomingPacket &packet)
	{
		const auto packetId = packet.GetId();
		bool isValid = true;

		PacketHandler handler = nullptr;

		{
			// Lock packet handler access
			std::scoped_lock lock{m_packetHandlerMutex};

			// Check for packet handlers
			const auto handlerIt = m_packetHandlers.find(packetId);
			if (handlerIt == m_packetHandlers.end())
			{
				WLOG("Packet 0x" << std::hex << static_cast<uint32>(packetId) << " is either unhandled or simply currently not handled");
				return PacketParseResult::Pass;
			}

			handler = handlerIt->second;
		}

		// Execute the packet handler and return the result
		return handler(packet);
	}

	PacketParseResult Player::OnAuthSession(game::IncomingPacket &packet)
	{
		ClearPacketHandler(game::client_realm_packet::AuthSession);

		// Read packet data
		if (!(packet >> io::read<uint32>(m_build) >> io::read_container<uint8>(m_accountName) >> io::read<uint32>(m_clientSeed) >> io::read_range(m_clientHash)))
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
		std::weak_ptr weakThis{shared_from_this()};
		auto callbackHandler = [weakThis](const bool succeeded, const uint64 accountId, const uint8 gmLevel, const BigNumber &sessionKey)
		{
			// Obtain strong reference to see if the client connection is still valid
			if (const auto strongThis = weakThis.lock())
			{
				// Handle success cases
				if (succeeded)
				{
					// Store session key and GM level
					strongThis->m_accountId = accountId;
					strongThis->m_gmLevel = gmLevel;
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

	PacketParseResult Player::OnCharEnum(game::IncomingPacket &packet)
	{
		DoCharEnum();

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnEnterWorld(game::IncomingPacket &packet)
	{
		EnableEnterWorldPacket(false);

		uint64 guid = 0;
		if (!(packet >> io::read<uint64>(guid)))
		{
			return PacketParseResult::Disconnect;
		}

		ILOG("Client wants to enter the world with character 0x" << std::hex << guid << "...");

		std::weak_ptr weakThis = shared_from_this();
		auto handler = [weakThis](const std::optional<CharacterData> &characterData)
		{
			if (const auto strongThis = weakThis.lock())
			{
				strongThis->OnCharacterData(characterData);
			}
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::CharacterEnterWorld, guid, m_accountId);

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnCreateChar(game::IncomingPacket &packet)
	{
		uint8 race = 0, characterClass = 0, gender = 0;
		std::string characterName;
		AvatarConfiguration configuration;
		if (!(packet >> io::read_container<uint8>(characterName) >> io::read<uint8>(race) >> io::read<uint8>(characterClass) >> io::read<uint8>(gender) >> configuration))
		{
			return PacketParseResult::Disconnect;
		}

		// Check input parameters for invalid values
		if (characterName.empty() || characterName.size() > 12 || gender > 1)
		{
			GetConnection().sendSinglePacket([](game::OutgoingPacket &outPacket)
											 {
					outPacket.Start(game::realm_client_packet::CharCreateResponse);
					outPacket << io::write<uint8>(game::char_create_result::Error);
					outPacket.Finish(); });
			return PacketParseResult::Pass;
		}

		// Check if given character class exists
		const auto *classInstance = m_project.classes.getById(characterClass);
		if (classInstance == nullptr)
		{
			ELOG("Unable to find character class " << log_hex_digit(characterClass));
			GetConnection().sendSinglePacket([](game::OutgoingPacket &outPacket)
											 {
					outPacket.Start(game::realm_client_packet::CharCreateResponse);
					outPacket << io::write<uint8>(game::char_create_result::Error);
					outPacket.Finish(); });
			return PacketParseResult::Pass;
		}

		const auto *raceEntry = m_project.races.getById(race);
		if (raceEntry == nullptr)
		{
			ELOG("Unable to find character race " << log_hex_digit(race));
			GetConnection().sendSinglePacket([](game::OutgoingPacket &outPacket)
											 {
					outPacket.Start(game::realm_client_packet::CharCreateResponse);
					outPacket << io::write<uint8>(game::char_create_result::Error);
					outPacket.Finish(); });
			return PacketParseResult::Pass;
		}

		// Get model
		const uint32 modelId = gender == 0 ? raceEntry->malemodel() : raceEntry->femalemodel();
		const proto::ModelDataEntry *model = m_project.models.getById(modelId);
		if (!model)
		{
			ELOG("Unable to find model data for model " << log_hex_digit(modelId));
			GetConnection().sendSinglePacket([](game::OutgoingPacket &outPacket)
											 {
					outPacket.Start(game::realm_client_packet::CharCreateResponse);
					outPacket << io::write<uint8>(game::char_create_result::Error);
					outPacket.Finish(); });
			return PacketParseResult::Pass;
		}

		// TODO: Load avatar definition and validate chosen property values

		std::weak_ptr weakThis{shared_from_this()};
		auto handler = [weakThis](const std::optional<CharCreateResult> &result)
		{
			if (const auto strongThis = weakThis.lock())
			{
				if (!result)
				{
					ELOG("Character creation failed due to an exception!");
					strongThis->GetConnection().sendSinglePacket([](game::OutgoingPacket &outPacket)
																 {
							// TODO: This is not correct because we don't know if NameInUse is really the exact error
							outPacket.Start(game::realm_client_packet::CharCreateResponse);
							outPacket << io::write<uint8>(game::char_create_result::NameInUse);
							outPacket.Finish(); });
					return;
				}

				if (result.value() == CharCreateResult::Success)
				{
					strongThis->DoCharEnum();
					return;
				}

				strongThis->GetConnection().sendSinglePacket([&result](game::OutgoingPacket &outPacket)
															 {
						// TODO: This is not correct because we don't know if NameInUse is really the exact error
						outPacket.Start(game::realm_client_packet::CharCreateResponse);
						outPacket << io::write<uint8>(
							result.value() == CharCreateResult::NameAlreadyInUse ? game::char_create_result::NameInUse : game::char_create_result::Error);
						outPacket.Finish(); });
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

		// Item data
		uint8 bagSlot = 0;
		std::vector<ItemData> items;
		std::set<uint16> usedSlots;

		// Add initial items
		auto it1 = raceEntry->initialitems().find(classInstance->id());
		if (it1 != raceEntry->initialitems().end())
		{
			for (int i = 0; i < it1->second.items_size(); ++i)
			{
				const auto *item = m_project.items.getById(it1->second.items(i));
				if (!item)
				{
					continue;
				}

				// Get slot for this item
				uint16 slot = 0xffff;
				switch (item->inventorytype())
				{
				case inventory_type::Head:
					slot = player_equipment_slots::Head;
					break;
				case inventory_type::Neck:
					slot = player_equipment_slots::Neck;
					break;
				case inventory_type::Shoulders:
					slot = player_equipment_slots::Shoulders;
					break;
				case inventory_type::Body:
					slot = player_equipment_slots::Body;
					break;
				case inventory_type::Chest:
				case inventory_type::Robe:
					slot = player_equipment_slots::Chest;
					break;
				case inventory_type::Waist:
					slot = player_equipment_slots::Waist;
					break;
				case inventory_type::Legs:
					slot = player_equipment_slots::Legs;
					break;
				case inventory_type::Feet:
					slot = player_equipment_slots::Feet;
					break;
				case inventory_type::Wrists:
					slot = player_equipment_slots::Wrists;
					break;
				case inventory_type::Hands:
					slot = player_equipment_slots::Hands;
					break;
				case inventory_type::Finger:
					slot = player_equipment_slots::Finger1;
					if (usedSlots.contains(slot))
					{
						slot = player_equipment_slots::Finger2;
					}
					break;
				case inventory_type::Trinket:
					slot = player_equipment_slots::Trinket1;
					if (usedSlots.contains(slot))
					{
						slot = player_equipment_slots::Trinket2;
					}
					break;
				case inventory_type::Weapon:
				case inventory_type::TwoHandedWeapon:
				case inventory_type::MainHandWeapon:
					slot = player_equipment_slots::Mainhand;
					break;
				case inventory_type::Shield:
				case inventory_type::OffHandWeapon:
				case inventory_type::Holdable:
					slot = player_equipment_slots::Offhand;
					break;
				case inventory_type::Ranged:
				case inventory_type::Thrown:
					slot = player_equipment_slots::Ranged;
					break;
				case inventory_type::Cloak:
					slot = player_equipment_slots::Back;
					break;
				case inventory_type::Tabard:
					slot = player_equipment_slots::Tabard;
					break;

				default:
				{
					if (bagSlot < player_inventory_pack_slots::Count_)
					{
						slot = player_inventory_pack_slots::Start + (bagSlot++);
					}
					break;
				}
				}

				if (slot != 0xffff)
				{
					if (usedSlots.contains(slot))
					{
						if (bagSlot < player_inventory_pack_slots::Count_)
						{
							slot = player_inventory_pack_slots::Start + (bagSlot++);
						}
						else
						{
							WLOG("Skipped creating item " << item->id() << " because there is not enough space to create this item!");
							continue;
						}
					}

					// Mark slot as used
					usedSlots.emplace(slot);

					ItemData itemData;
					itemData.entry = item->id();
					itemData.durability = item->durability();
					itemData.slot = slot | 0xFF00;
					itemData.stackCount = 1;
					items.emplace_back(itemData);
				}
			}
		}

		std::map<uint8, ActionButton> actionButtons;

		std::vector<uint32> spellIds;
		spellIds.reserve(classInstance->spells().size());
		for (const auto &spell : classInstance->spells())
		{
			const proto::SpellEntry *spellEntry = m_project.spells.getById(spell.spell());
			if (spellEntry && actionButtons.size() < 12)
			{
				// Not a passive, not hidden and an ability? Put it in the action bar!
				if ((spellEntry->attributes(0) & spell_attributes::Passive) == 0 &&
					(spellEntry->attributes(0) & spell_attributes::HiddenClientSide) == 0 &&
					(spellEntry->attributes(0) & spell_attributes::Ability) != 0)
				{
					actionButtons[actionButtons.size()] = ActionButton{static_cast<uint16>(spell.spell()), action_button_type::Spell};
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
								spellIds, mana, rage, energy, actionButtons, configuration, items);

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnDeleteChar(game::IncomingPacket &packet)
	{
		ASSERT(IsAuthenticated());

		uint64 charGuid = 0;
		if (!(packet >> io::read<uint64>(charGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		// Lock around m_characterViews
		std::scoped_lock lock{m_charViewMutex};

		// Check if such a character exists
		const auto charIt = m_characterViews.find(charGuid);
		if (charIt == m_characterViews.end())
		{
			WLOG("Tried to delete character 0x" << std::hex << charGuid << " which doesn't exist or belong to the players account!");
			return PacketParseResult::Disconnect;
		}

		// Handle guild membership before deletion
		HandleCharacterGuildOnDelete(charGuid);

		// Handle group membership before deletion
		HandleCharacterGroupOnDelete(charGuid);

		// Handle friend relationships before deletion
		HandleCharacterFriendsOnDelete(charGuid);

		// Database callback handler
		std::weak_ptr weakThis{shared_from_this()};
		auto handler = [weakThis](const bool success)
		{
			if (const auto strongThis = weakThis.lock())
			{
				if (!success)
				{
					ELOG("Failed to delete character!");
				}

				strongThis->DoCharEnum();
			}
		};

		DLOG("Deleting character 0x" << std::hex << charGuid << " from account 0x" << std::hex << m_accountId << "...");
		m_database.asyncRequest<void>([charGuid](auto &&database)
									  { database->DeleteCharacter(charGuid); }, std::move(handler));

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnProxyPacket(game::IncomingPacket &packet)
	{
		const auto strongWorld = m_world.lock();
		if (!strongWorld)
		{
			WLOG("Received proxy packet " << log_hex_digit(packet.GetId()) << " from character without a world!");
			return PacketParseResult::Pass;
		}

		// Check for GM commands that require specific GM levels
#ifdef MMO_WITH_DEV_COMMANDS
		// This is where we will restrict certain commands based on GM level
		switch (packet.GetId())
		{
		case game::client_realm_packet::CheatFaceMe:
		case game::client_realm_packet::CheatFollowMe:
		case game::client_realm_packet::CheatSpeed:
		case game::client_realm_packet::CheatWorldPort:
		case game::client_realm_packet::CheatTeleportToPlayer:
			// Require GM level 1 (basic GM)
			if (!HasGMLevel(1))
			{
				WLOG("Player " << m_characterData->name << " attempted to use a GM command without sufficient privileges");
				return PacketParseResult::Pass;
			}
			break;
		case game::client_realm_packet::CheatLearnSpell:
		case game::client_realm_packet::CheatRecharge:
		case game::client_realm_packet::CheatCreateMonster:
		case game::client_realm_packet::CheatDestroyMonster:
		case game::client_realm_packet::CheatLevelUp:
		case game::client_realm_packet::CheatGiveMoney:
		case game::client_realm_packet::CheatAddItem:
			// Require GM level 2
			if (!HasGMLevel(2))
			{
				WLOG("Player " << m_characterData->name << " attempted to use a teleport command without sufficient privileges");
				return PacketParseResult::Pass;
			}
			break;
		}
#endif

		const auto characterId = m_characterData->characterId;
		strongWorld->GetConnection().sendSinglePacket([characterId, &packet](auth::OutgoingPacket &outPacket)
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
			
			outPacket.Finish(); });

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnChatMessage(game::IncomingPacket &packet)
	{
		ASSERT(HasCharacterGuid());

		uint8 chatType;
		std::string message;
		if (!(packet >> io::read<uint8>(chatType) >> io::read_limited_string<512>(message)))
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
			m_group->BroadcastPacket([this, &message, chatType](game::OutgoingPacket &outPacket)
									 {
					outPacket.Start(game::realm_client_packet::ChatMessage);
					outPacket
						<< io::write_packed_guid(m_characterData->characterId)
						<< io::write<uint8>(chatType)
						<< io::write_range(message)
						<< io::write<uint8>(0)
						<< io::write<uint8>(0);
					outPacket.Finish(); });
			break;

		case ChatType::Guild:
		case ChatType::GuildOfficer:
		{
			if (!m_characterData)
			{
				WLOG("Player tried to send guild chat message without having character data!");
				break;
			}

			if (m_characterData->guildId == 0)
			{
				WLOG("Player tried to send guild chat message without being in a guild!");
				break;
			}

			Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
			if (!guild)
			{
				WLOG("Player tried to send guild chat message to a guild that doesn't exist!");
				break;
			}

			// Check if the player has permission to write to guild chat
			const uint32 requiredWritePermissions = (static_cast<ChatType>(chatType) == ChatType::GuildOfficer) ? guild_rank_permissions::WriteOfficerChat : guild_rank_permissions::WriteGuildChat;
			if (!guild->HasPermission(m_characterData->characterId, requiredWritePermissions))
			{
				WLOG("Player tried to send guild chat message without having permission to write to guild chat!");
				break;
			}

			const uint32 requiredReadPermissions = (static_cast<ChatType>(chatType) == ChatType::GuildOfficer) ? guild_rank_permissions::ReadOfficerChat : guild_rank_permissions::ReadGuildChat;

			// Send guild chat packet to all members with permission to read guild chat
			guild->BroadcastPacketWithPermission([this, &message, chatType](game::OutgoingPacket &outPacket)
												 {
						outPacket.Start(game::realm_client_packet::ChatMessage);
						outPacket
							<< io::write_packed_guid(m_characterData->characterId)
							<< io::write<uint8>(chatType)
							<< io::write_range(message)
							<< io::write<uint8>(0)  // Chat flags
							<< io::write<uint8>(0); // Chat tag
						outPacket.Finish(); }, requiredReadPermissions);
		}
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

	PacketParseResult Player::OnNameQuery(game::IncomingPacket &packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Received CMSG_NAME_QUERY for unit " << log_hex_digit(guid) << "...");

		const Player *player = m_manager.GetPlayerByCharacterGuid(guid);
		if (player != nullptr)
		{
			String name = player->GetCharacterName();
			m_connection->sendSinglePacket([guid, &name](game::OutgoingPacket &packet)
										   {
					packet.Start(game::realm_client_packet::NameQueryResult);
					packet
						<< io::write_packed_guid(guid)
						<< io::write<uint8>(true)
						<< io::write_range(name) << io::write<uint8>(0);
					packet.Finish(); });
		}
		else
		{
			// We have to look up the player name in the database
			std::weak_ptr weakThis = shared_from_this();
			auto handler = [weakThis, guid](std::optional<String> &playerName)
			{
				if (const auto strongThis = weakThis.lock())
				{
					strongThis->m_connection->sendSinglePacket([guid, &playerName](game::OutgoingPacket &packet)
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
							packet.Finish(); });
				}
			};
			m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterNameById, guid);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildQuery(game::IncomingPacket &packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Received CMSG_GUILD_QUERY for unit " << log_hex_digit(guid) << "...");

		Guild *guild = m_guildMgr.GetGuild(guid);
		if (!guild)
		{
			WLOG("Queried guild " << log_hex_digit(guid) << " does not exist!");

			m_connection->sendSinglePacket([guid](game::OutgoingPacket &packet)
										   {
					packet.Start(game::realm_client_packet::GuildQueryResponse);
					packet
						<< io::write_packed_guid(guid)
						<< io::write<uint8>(false);
					packet.Finish(); });
			return PacketParseResult::Pass;
		}

		GuildInfo info;
		info.id = guild->GetId();
		info.name = guild->GetName();

		m_connection->sendSinglePacket([&info](game::OutgoingPacket &packet)
									   {
				packet.Start(game::realm_client_packet::GuildQueryResponse);
				packet
					<< io::write_packed_guid(info.id)
					<< io::write<uint8>(true)
					<< info;
				packet.Finish(); });

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnDbQuery(game::IncomingPacket &packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			return PacketParseResult::Disconnect;
		}

		switch (packet.GetId())
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

		case game::client_realm_packet::ObjectQuery:
			OnQueryObject(guid);
			break;

		default:
			ELOG("Player tried to query unsupported item, packet handler might be subscribed for wrong op code (" << packet.GetId() << ")");
			break;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnSetActionBarButton(game::IncomingPacket &packet)
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

	PacketParseResult Player::OnMoveWorldPortAck(game::IncomingPacket &packet)
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
				} });

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGroupInvite(game::IncomingPacket &packet)
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
			m_group = std::make_shared<PlayerGroup>(m_groupIdGenerator.GenerateId(), m_manager, m_database, m_timerQueue);
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

	PacketParseResult Player::OnGroupUninvite(game::IncomingPacket &packet)
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

	PacketParseResult Player::OnGroupAccept(game::IncomingPacket &packet)
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

	PacketParseResult Player::OnGroupDecline(game::IncomingPacket &packet)
	{
		DeclineGroupInvite();

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnLogoutRequest(game::IncomingPacket &packet)
	{
		if (!m_characterData)
		{
			ELOG("Player tried to logout without having character data");
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to log out");

		// TODO: In the future we should check if the player can logout (is in combat etc.) and implement a logout timer
		// but for simplicity we enable instant logout everywhere for now
		if (const auto world = GetWorld())
		{
			world->Leave(m_characterData->characterId, auth::world_left_reason::Logout);
		}
		else
		{
			Destroy();
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildAccept(game::IncomingPacket &packet)
	{
		if (m_pendingGuildInvite == 0)
		{
			ELOG("Player tried to accept guild invite without having a pending invite");
			return PacketParseResult::Disconnect;
		}

		// Get the guild
		Guild *guild = m_guildMgr.GetGuild(m_pendingGuildInvite);
		if (!guild)
		{
			ELOG("Player tried to accept guild invite to a guild that doesn't exist");
			return PacketParseResult::Pass;
		}

		if (!guild->AddMember(m_characterData->characterId, guild->GetLowestRank()))
		{
			ELOG("Failed to add player to guild");
			return PacketParseResult::Pass;
		}

		GuildChange(guild->GetId());
		guild->BroadcastEvent(guild_event::Joined, 0, GetCharacterName().c_str());

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildDecline(game::IncomingPacket &packet)
	{
		if (m_pendingGuildInvite == 0)
		{
			ELOG("Player tried to decline guild invite without having a pending invite");
			return PacketParseResult::Disconnect;
		}

		ASSERT(m_characterData);
		DLOG("Pending guild invite from " << log_hex_digit(m_guildInviter) << " to guild " << log_hex_digit(m_pendingGuildInvite) << " declined by player " << log_hex_digit(m_characterData->characterId));

		// Notify the inviter if possible
		const auto player = m_manager.GetPlayerByCharacterGuid(m_guildInviter);
		m_guildInviter = 0;
		m_pendingGuildInvite = 0;
		if (player)
		{
			const String &inviterName = GetCharacterName();
			player->SendPacket(
				[&inviterName](game::OutgoingPacket &packet)
				{
					packet.Start(game::realm_client_packet::GuildDecline);
					packet << io::write_dynamic_range<uint8>(inviterName);
					packet.Finish();
				});
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildRoster(game::IncomingPacket &packet)
	{
		if (!m_characterData || m_characterData->guildId == 0)
		{
			WLOG("Player requested guild roster without being in a guild!");
			return PacketParseResult::Pass;
		}

		Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
		if (!guild)
		{
			WLOG("Player requested guild roster for a guild that doesn't exist!");
			return PacketParseResult::Pass;
		}

		SendPacket([guild](game::OutgoingPacket &packet)
				   {
				packet.Start(game::realm_client_packet::GuildRoster);
				guild->WriteRoster(packet);
				packet.Finish(); });

		return PacketParseResult::Pass;
	}

	// Friend system implementations

	void Player::LoadFriendList()
	{
		if (!m_characterData)
		{
			return;
		}

		m_friendMgr.LoadCharacterFriends(m_characterData->characterId, [this](const std::vector<FriendData> &friends)
										 {
				m_friendCache = friends;
				SendFriendListUpdate(); });
	}

	void Player::SendFriendInvite(const String &targetName)
	{
		if (!m_characterData)
		{
			return;
		}

		const uint64 myGuid = m_characterData->characterId;

		// First check if target is online
		Player *targetPlayer = m_manager.GetPlayerByCharacterName(targetName);

		if (targetPlayer)
		{
			// Online player path - directly add friendship without requiring acceptance
			const uint64 targetGuid = targetPlayer->GetCharacterGuid();

			// Check if trying to add self
			if (targetGuid == myGuid)
			{
				SendPacket([targetName](game::OutgoingPacket &packet)
						   {
					packet.Start(game::realm_client_packet::FriendCommandResult);
					packet << io::write<uint8>(game::friend_command_result::CannotAddSelf);
					packet << io::write_dynamic_range<uint8>(targetName);
					packet.Finish(); });
				return;
			}

			// Check if already friends
			if (m_friendMgr.AreFriends(myGuid, targetGuid))
			{
				SendPacket([targetName](game::OutgoingPacket &packet)
						   {
					packet.Start(game::realm_client_packet::FriendCommandResult);
					packet << io::write<uint8>(game::friend_command_result::AlreadyFriends);
					packet << io::write_dynamic_range<uint8>(targetName);
					packet.Finish(); });
				return;
			}

			// Check if sender's friend list is full
			if (!m_friendMgr.CanAddFriend(myGuid))
			{
				SendPacket([targetName](game::OutgoingPacket &packet)
						   {
					packet.Start(game::realm_client_packet::FriendCommandResult);
					packet << io::write<uint8>(game::friend_command_result::FriendListFull);
					packet << io::write_dynamic_range<uint8>(targetName);
					packet.Finish(); });
				return;
			}

			// One-sided friendship: directly add the target to sender's list
			auto addFriendHandler = [this, targetPlayer, targetGuid, targetName, myGuid](bool success)
			{
				if (success)
				{
					// Reload my friend list from database to update in-memory cache
					m_friendMgr.LoadCharacterFriends(myGuid, [this](const std::vector<FriendData> &friends)
													 {
						m_friendCache = friends;
						SendFriendListUpdate(); });

					// Send success result to sender
					SendPacket([targetName](game::OutgoingPacket &packet)
							   {
						packet.Start(game::realm_client_packet::FriendCommandResult);
						packet << io::write<uint8>(game::friend_command_result::Success);
						packet << io::write_dynamic_range<uint8>(targetName);
						packet.Finish(); });

					ILOG("Friendship established (one-sided) with online player " << targetGuid);
				}
				else
				{
					ELOG("Failed to add online friend to database");
				}
			};

			m_database.asyncRequest(std::move(addFriendHandler), &IDatabase::AddFriend, myGuid, targetGuid);
		}
		else
		{
			// Offline player path - lookup in database
			auto handler = [this, targetName, myGuid](std::optional<DatabaseId> targetGuidOpt)
			{
				if (!targetGuidOpt.has_value())
				{
					// Character doesn't exist
					SendPacket([targetName](game::OutgoingPacket &packet)
							   {
						packet.Start(game::realm_client_packet::FriendCommandResult);
						packet << io::write<uint8>(game::friend_command_result::PlayerNotFound);
						packet << io::write_dynamic_range<uint8>(targetName);
						packet.Finish(); });
					return;
				}

				const uint64 targetGuid = targetGuidOpt.value();

				// Check if trying to add self
				if (targetGuid == myGuid)
				{
					SendPacket([targetName](game::OutgoingPacket &packet)
							   {
						packet.Start(game::realm_client_packet::FriendCommandResult);
						packet << io::write<uint8>(game::friend_command_result::CannotAddSelf);
						packet << io::write_dynamic_range<uint8>(targetName);
						packet.Finish(); });
					return;
				}

				// Check if already friends
				if (m_friendMgr.AreFriends(myGuid, targetGuid))
				{
					SendPacket([targetName](game::OutgoingPacket &packet)
							   {
						packet.Start(game::realm_client_packet::FriendCommandResult);
						packet << io::write<uint8>(game::friend_command_result::AlreadyFriends);
						packet << io::write_dynamic_range<uint8>(targetName);
						packet.Finish(); });
					return;
				}

				// Check if sender's friend list is full
				if (!m_friendMgr.CanAddFriend(myGuid))
				{
					SendPacket([targetName](game::OutgoingPacket &packet)
							   {
						packet.Start(game::realm_client_packet::FriendCommandResult);
						packet << io::write<uint8>(game::friend_command_result::FriendListFull);
						packet << io::write_dynamic_range<uint8>(targetName);
						packet.Finish(); });
					return;
				}

				// One-sided friendship: directly add the target to sender's list
				auto addFriendHandler = [this, targetGuid, targetName, myGuid](bool success)
				{
					if (success)
					{
						// Reload my friend list from database to update in-memory cache
						m_friendMgr.LoadCharacterFriends(myGuid, [this](const std::vector<FriendData> &friends)
														 {
							m_friendCache = friends;
							SendFriendListUpdate(); });

						// Send success result
						SendPacket([targetName](game::OutgoingPacket &packet)
								   {
							packet.Start(game::realm_client_packet::FriendCommandResult);
							packet << io::write<uint8>(game::friend_command_result::Success);
							packet << io::write_dynamic_range<uint8>(targetName);
							packet.Finish(); });

						ILOG("Friendship established (one-sided) with offline player " << targetGuid);
					}
					else
					{
						ELOG("Failed to add offline friend to database");
					}
				};

				m_database.asyncRequest(std::move(addFriendHandler), &IDatabase::AddFriend, myGuid, targetGuid);
			};

			m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterIdByName, targetName);
		}
	}

	void Player::AcceptFriendInvite()
	{
		if (!m_characterData)
		{
			return;
		}

		if (m_pendingFriendInvite == 0)
		{
			WLOG("Player " << m_characterData->name << " tried to accept friend invite but has none pending");
			return;
		}

		const uint64 inviterGuid = m_pendingFriendInvite;
		const uint64 myGuid = m_characterData->characterId;

		// Add friendship to database
		auto handler = [this, inviterGuid, myGuid](bool success)
		{
			if (!success)
			{
				ELOG("Failed to add friendship between " << myGuid << " and " << inviterGuid);
				return;
			}

			// Reload both players' friend lists
			m_friendMgr.LoadCharacterFriends(myGuid, [this](const std::vector<FriendData> &friends)
											 {
						m_friendCache = friends;
						SendFriendListUpdate(); });

			Player *inviterPlayer = m_manager.GetPlayerByCharacterGuid(inviterGuid);
			if (inviterPlayer)
			{
				inviterPlayer->m_friendMgr.LoadCharacterFriends(inviterGuid, [inviterPlayer](const std::vector<FriendData> &friends)
																{
							inviterPlayer->m_friendCache = friends;
							inviterPlayer->SendFriendListUpdate(); });

				// Send success result to inviter
				const String myName = m_characterData->name;
				inviterPlayer->SendPacket([myName](game::OutgoingPacket &packet)
										  {
							packet.Start(game::realm_client_packet::FriendCommandResult);
							packet << io::write<uint8>(game::friend_command_result::Success);
							packet << io::write_dynamic_range<uint8>(myName);
							packet.Finish(); });
			}

			ILOG("Friendship established between " << myGuid << " and " << inviterGuid);
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::AddFriend, myGuid, inviterGuid);

		// Clear pending invite
		m_pendingFriendInvite = 0;
	}

	void Player::DeclineFriendInvite()
	{
		if (!m_characterData)
		{
			return;
		}

		if (m_pendingFriendInvite == 0)
		{
			return;
		}

		// Notify inviter of decline
		Player *inviterPlayer = m_manager.GetPlayerByCharacterGuid(m_pendingFriendInvite);
		if (inviterPlayer)
		{
			const String myName = m_characterData->name;
			inviterPlayer->SendPacket([myName](game::OutgoingPacket &packet)
									  {
					packet.Start(game::realm_client_packet::FriendCommandResult);
					packet << io::write<uint8>(game::friend_command_result::Success);  // Use Success for decline notification
					packet << io::write_dynamic_range<uint8>(myName);
					packet.Finish(); });
		}

		m_pendingFriendInvite = 0;
		DLOG("Player " << m_characterData->name << " declined friend invite");
	}

	void Player::RemoveFriend(const String &friendName)
	{
		if (!m_characterData)
		{
			return;
		}

		// Find friend in cache
		uint64 friendGuid = 0;
		for (const auto &friendData : m_friendCache)
		{
			if (friendData.name == friendName)
			{
				friendGuid = friendData.guid;
				break;
			}
		}

		if (friendGuid == 0)
		{
			SendPacket([friendName](game::OutgoingPacket &packet)
					   {
					packet.Start(game::realm_client_packet::FriendCommandResult);
					packet << io::write<uint8>(game::friend_command_result::NotFriends);
					packet << io::write_dynamic_range<uint8>(friendName);
					packet.Finish(); });
			return;
		}

		const uint64 myGuid = m_characterData->characterId;

		// Remove from database
		auto handler = [this, friendGuid, myGuid, friendName](bool success)
		{
			if (!success)
			{
				ELOG("Failed to remove friendship between " << myGuid << " and " << friendGuid);
				return;
			}

			// Reload both players' friend lists
			m_friendMgr.LoadCharacterFriends(myGuid, [this](const std::vector<FriendData> &friends)
											 {
						m_friendCache = friends;
						SendFriendListUpdate(); });

			Player *friendPlayer = m_manager.GetPlayerByCharacterGuid(friendGuid);
			if (friendPlayer)
			{
				friendPlayer->m_friendMgr.LoadCharacterFriends(friendGuid, [friendPlayer](const std::vector<FriendData> &friends)
															   {
							friendPlayer->m_friendCache = friends;
							friendPlayer->SendFriendListUpdate(); });
			}

			// Send success result
			SendPacket([friendName](game::OutgoingPacket &packet)
					   {
						packet.Start(game::realm_client_packet::FriendCommandResult);
						packet << io::write<uint8>(game::friend_command_result::Success);
						packet << io::write_dynamic_range<uint8>(friendName);
						packet.Finish(); });

			ILOG("Friendship removed between " << myGuid << " and " << friendGuid);
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::RemoveFriend, myGuid, friendGuid);
	}

	void Player::SendFriendListUpdate()
	{
		SendPacket([this](game::OutgoingPacket &packet)
				   {
				packet.Start(game::realm_client_packet::FriendListUpdate);
				packet << io::write<uint16>(static_cast<uint16>(m_friendCache.size()));

				for (const auto& friendData : m_friendCache)
				{
					packet << io::write<uint64>(friendData.guid);
					packet << io::write_dynamic_range<uint8>(friendData.name);
					packet << io::write<uint32>(friendData.level);
					packet << io::write<uint32>(friendData.raceId);
					packet << io::write<uint32>(friendData.classId);

					// Check if friend is online
					Player* friendPlayer = m_manager.GetPlayerByCharacterGuid(friendData.guid);
					const bool online = (friendPlayer != nullptr);
					packet << io::write<uint8>(online ? 1 : 0);
				}

				packet.Finish(); });
	}

	// Friend packet handlers

	PacketParseResult Player::OnFriendInvite(game::IncomingPacket &packet)
	{
		String targetName;
		if (!(packet >> io::read_container<uint8>(targetName)))
		{
			return PacketParseResult::Disconnect;
		}

		SendFriendInvite(targetName);
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnFriendAccept(game::IncomingPacket &packet)
	{
		AcceptFriendInvite();
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnFriendDecline(game::IncomingPacket &packet)
	{
		DeclineFriendInvite();
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnFriendRemove(game::IncomingPacket &packet)
	{
		String friendName;
		if (!(packet >> io::read_container<uint8>(friendName)))
		{
			return PacketParseResult::Disconnect;
		}

		RemoveFriend(friendName);
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnFriendListRequest(game::IncomingPacket &packet)
	{
		SendFriendListUpdate();
		return PacketParseResult::Pass;
	}

#ifdef MMO_WITH_DEV_COMMANDS
	PacketParseResult Player::OnCheatTeleportToPlayer(game::IncomingPacket &packet)
	{
		// Require GM level 1 for teleport commands
		if (!HasGMLevel(1))
		{
			WLOG("Player " << m_characterData->name << " attempted to use teleport command without sufficient privileges");
			return PacketParseResult::Pass;
		}

		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		// Resolve player by name
		Player *player = m_manager.GetPlayerByCharacterName(playerName);
		if (!player)
		{
			// We could not find a player currently online with the given name, so we ask the database and teleport to the location the character logged out
			std::weak_ptr weakThis{shared_from_this()};
			auto handler = [weakThis, playerName](const std::optional<CharacterLocationData> &result)
			{
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
		std::weak_ptr weakThis{shared_from_this()};
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
				strongThis->SendTeleportRequest(mapId, position, facing); });

		return PacketParseResult::Pass;
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	PacketParseResult Player::OnCheatSummon(game::IncomingPacket &packet)
	{
		// Require GM level 2 for teleport commands
		if (!HasGMLevel(2))
		{
			WLOG("Player " << m_characterData->name << " attempted to use summon command without sufficient privileges");
			return PacketParseResult::Pass;
		}

		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		std::weak_ptr weakThis{shared_from_this()};
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
				player->SendTeleportRequest(mapId, position, facing); });

		return PacketParseResult::Pass;
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	PacketParseResult Player::OnGuildCreate(game::IncomingPacket &packet)
	{
		String guildName;
		if (!(packet >> io::read_container<uint8>(guildName)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to create a guild named '" << guildName << "'...");

		if (!m_characterData)
		{
			ELOG("Player tried to create a guild without having a character data object");
			return PacketParseResult::Disconnect;
		}

		if (m_characterData->guildId != 0)
		{
			ELOG("Player tried to create a guild while already being in a guild");
			return PacketParseResult::Pass;
		}

		// TODO: Create guild
		if (m_guildMgr.GetGuildIdByName(guildName) != 0)
		{
			ELOG("Guild with name '" << guildName << "' already exists");
			return PacketParseResult::Pass;
		}

		std::weak_ptr weak = shared_from_this();
		auto guildCreationHandler = [weak](Guild *guild)
		{
			auto strong = weak.lock();
			if (!strong)
			{
				return;
			}

			if (guild)
			{
				ILOG("Successfully created guild " << guild->GetName());
				strong->GuildChange(guild->GetId());
			}
			else
			{
				ELOG("Guild creation failed");
			}
		};

		if (!m_guildMgr.CreateGuild(guildName, m_characterData->characterId, {m_characterData->characterId}, std::move(guildCreationHandler)))
		{
			ELOG("Failed to create guild");
			return PacketParseResult::Pass;
		}

		return PacketParseResult::Pass;
	}
#endif

	bool Player::InitializeTransfer(uint32 map, const Vector3 &location, const float o, bool shouldLeaveNode)
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
			m_connection->sendSinglePacket([map](game::OutgoingPacket &packet)
										   {
				packet.Start(game::realm_client_packet::TransferPending);
				packet << io::write<uint32>(map);
				packet.Finish(); });
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

		m_connection->sendSinglePacket([this](game::OutgoingPacket &packet)
									   {
			packet.Start(game::realm_client_packet::NewWorld);
			packet
				<< io::write<uint32>(m_transferMap)
				<< io::write<float>(m_transferPosition.x)
				<< io::write<float>(m_transferPosition.y)
				<< io::write<float>(m_transferPosition.z)
				<< io::write<float>(m_transferFacing);
			packet.Finish(); });

		m_newWorldAckHandler += RegisterAutoPacketHandler(game::client_realm_packet::MoveWorldPortAck, *this, &Player::OnMoveWorldPortAck);
	}

	void Player::SetInviterGuid(uint64 inviter)
	{
		m_inviterGuid = inviter;
	}

	void Player::SendPartyInvite(const String &inviterName)
	{
		m_connection->sendSinglePacket([&inviterName](game::OutgoingPacket &packet)
									   {
				packet.Start(game::realm_client_packet::GroupInvite);
				packet << io::write_dynamic_range<uint8>(inviterName);
				packet.Finish(); });
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
			const String &inviterName = GetCharacterName();
			player->SendPacket(
				[&inviterName](game::OutgoingPacket &packet)
				{
					packet.Start(game::realm_client_packet::GroupDecline);
					packet << io::write_dynamic_range<uint8>(inviterName);
					packet.Finish();
				});
		}
	}

	void Player::BuildPartyMemberStatsPacket(game::OutgoingPacket &packet, const uint32 groupUpdateFlags) const
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
			case power_type::Mana:
				packet << io::write<uint16>(m_characterData->mana);
				break;
			case power_type::Rage:
				packet << io::write<uint16>(m_characterData->rage);
				break;
			case power_type::Energy:
				packet << io::write<uint16>(m_characterData->energy);
				break;
			default:
				packet << io::write<uint16>(0);
				break;
			}
		}

		if (groupUpdateFlags & group_update_flags::MaxPower)
		{
			switch (powerType)
			{
			case power_type::Mana:
				packet << io::write<uint16>(m_characterData->maxMana);
				break;
			case power_type::Rage:
				packet << io::write<uint16>(m_characterData->maxRage);
				break;
			case power_type::Energy:
				packet << io::write<uint16>(m_characterData->maxEnergy);
				break;
			default:
				packet << io::write<uint16>(0);
				break;
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

	void Player::GuildChange(const uint64 guildId)
	{
		if (!m_characterData)
		{
			ELOG("Player tried to change guild without having character data");
			return;
		}

		m_pendingGuildInvite = 0;
		m_guildInviter = 0;

		m_characterData->guildId = guildId;

		const auto world = GetWorld();
		if (!world)
		{
			return;
		}

		world->NotifyPlayerGuildChanged(m_characterData->characterId, guildId);
	}

	void Player::NotifyCharacterUpdate(uint32 mapId, InstanceId instanceId, const GamePlayerS &character)
	{
		m_characterData->mapId = mapId;
		m_characterData->instanceId = instanceId;
		m_characterData->level = static_cast<uint8>(character.GetLevel());
		m_characterData->xp = character.Get<uint32>(object_fields::Xp);
		m_characterData->hp = character.GetHealth();
		m_characterData->maxHp = character.GetMaxHealth();
		m_characterData->mana = character.Get<uint32>(object_fields::Mana);
		m_characterData->maxMana = character.Get<uint32>(object_fields::MaxMana);
		m_characterData->rage = character.Get<uint32>(object_fields::Rage);
		m_characterData->maxRage = character.Get<uint32>(object_fields::MaxRage);
		m_characterData->energy = character.Get<uint32>(object_fields::Energy);
		m_characterData->maxEnergy = character.Get<uint32>(object_fields::MaxEnergy);
		m_characterData->money = character.Get<uint32>(object_fields::Money);

		// Keep attribute point distribution in sync for world transfers
		for (uint32 i = 0; i < m_characterData->attributePointsSpent.size(); ++i)
		{
			m_characterData->attributePointsSpent[i] = character.GetAttributePointsByAttribute(i);
		}

		m_characterData->spellIds.clear();
		for (const auto &spell : character.GetSpells())
		{
			if (!spell)
			{
				continue;
			}

			m_characterData->spellIds.push_back(spell->id());
		}

		m_characterData->position = character.GetPosition();
		m_characterData->facing = character.GetFacing();
		m_characterData->bindMap = character.GetBindMap();
		m_characterData->bindPosition = character.GetBindPosition();
		m_characterData->bindFacing = character.GetBindFacing();

		// Cache current inventory snapshot for the next world node (items are also persisted separately)
		m_characterData->items = character.GetInventory().GetItemData();

		// Sync learned talents so the next world node restores the correct ranks
		m_characterData->talentRanks.clear();
		for (const auto& [talentId, rank] : character.GetTalents())
		{
			m_characterData->talentRanks[talentId] = static_cast<uint8>(rank);
		}
		m_characterData->isGameMaster = (m_gmLevel > 0);
	}

	void Player::SendAuthChallenge()
	{
		// We will start accepting LogonChallenge packets from the client
		RegisterPacketHandler(game::client_realm_packet::AuthSession, *this, &Player::OnAuthSession);

		// Send the LogonChallenge packet to the client including our generated seed
		m_connection->sendSinglePacket([this](game::OutgoingPacket &packet)
									   {
			packet.Start(game::realm_client_packet::AuthChallenge);
			packet << io::write<uint32>(m_seed);
			packet.Finish(); });
	}

	void Player::InitializeSession(const BigNumber &sessionKey)
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
		m_connection->sendSinglePacket([](game::OutgoingPacket &packet)
									   {
			packet.Start(game::realm_client_packet::AuthSessionResponse);
			packet << io::write<uint8>(game::auth_result::Success);	// TODO: Write real packet content
			packet.Finish(); });

		// Enable CharEnum packets
		RegisterPacketHandler(game::client_realm_packet::CharEnum, *this, &Player::OnCharEnum);
		RegisterPacketHandler(game::client_realm_packet::CreateChar, *this, &Player::OnCreateChar);
		RegisterPacketHandler(game::client_realm_packet::DeleteChar, *this, &Player::OnDeleteChar);
		RegisterPacketHandler(game::client_realm_packet::EnterWorld, *this, &Player::OnEnterWorld);
	}

	void Player::SendProxyPacket(uint16 packetId, const std::vector<char> &buffer)
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

		m_connection->GetCrypt().EncryptSend(reinterpret_cast<uint8 *>(&sendBuffer[bufferPos]), game::Crypt::CryptedSendLength);
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
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveSplineDone, *this, &Player::OnProxyPacket);

			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AutoStoreLootItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AutoEquipItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::SwapItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::SwapInvItem, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::SplitItem, *this, &Player::OnProxyPacket);
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
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::GossipAction, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::MoveRootAck, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::LearnTalent, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::TimePlayedRequest, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::TimeSyncResponse, *this, &Player::OnProxyPacket);
			m_proxyHandlers += RegisterAutoPacketHandler(game::client_realm_packet::AreaTriggerTriggered, *this, &Player::OnProxyPacket);

			// Guild packet handlers
			RegisterPacketHandler(game::client_realm_packet::GuildInvite, *this, &Player::OnGuildInvite);
			RegisterPacketHandler(game::client_realm_packet::GuildRemove, *this, &Player::OnGuildRemove);
			RegisterPacketHandler(game::client_realm_packet::GuildPromote, *this, &Player::OnGuildPromote);
			RegisterPacketHandler(game::client_realm_packet::GuildDemote, *this, &Player::OnGuildDemote);
			RegisterPacketHandler(game::client_realm_packet::GuildLeave, *this, &Player::OnGuildLeave);
			RegisterPacketHandler(game::client_realm_packet::GuildDisband, *this, &Player::OnGuildDisband);
			RegisterPacketHandler(game::client_realm_packet::GuildMotd, *this, &Player::OnGuildMotd);
			RegisterPacketHandler(game::client_realm_packet::GuildAccept, *this, &Player::OnGuildAccept);
			RegisterPacketHandler(game::client_realm_packet::GuildDecline, *this, &Player::OnGuildDecline);
			RegisterPacketHandler(game::client_realm_packet::GuildRoster, *this, &Player::OnGuildRoster);

			// Friend packet handlers
			RegisterPacketHandler(game::client_realm_packet::FriendInvite, *this, &Player::OnFriendInvite);
			RegisterPacketHandler(game::client_realm_packet::FriendAccept, *this, &Player::OnFriendAccept);
			RegisterPacketHandler(game::client_realm_packet::FriendDecline, *this, &Player::OnFriendDecline);
			RegisterPacketHandler(game::client_realm_packet::FriendRemove, *this, &Player::OnFriendRemove);
			RegisterPacketHandler(game::client_realm_packet::FriendListRequest, *this, &Player::OnFriendListRequest);

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
			RegisterPacketHandler(game::client_realm_packet::GuildQuery, *this, &Player::OnGuildQuery);
			RegisterPacketHandler(game::client_realm_packet::CreatureQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::ItemQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::QuestQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::ObjectQuery, *this, &Player::OnDbQuery);
			RegisterPacketHandler(game::client_realm_packet::SetActionBarButton, *this, &Player::OnSetActionBarButton);
			RegisterPacketHandler(game::client_realm_packet::GroupInvite, *this, &Player::OnGroupInvite);
			RegisterPacketHandler(game::client_realm_packet::GroupUninvite, *this, &Player::OnGroupUninvite);
			RegisterPacketHandler(game::client_realm_packet::GroupAccept, *this, &Player::OnGroupAccept);
			RegisterPacketHandler(game::client_realm_packet::GroupDecline, *this, &Player::OnGroupDecline);
			RegisterPacketHandler(game::client_realm_packet::LogoutRequest, *this, &Player::OnLogoutRequest);

#if MMO_WITH_DEV_COMMANDS
			RegisterPacketHandler(game::client_realm_packet::CheatTeleportToPlayer, *this, &Player::OnCheatTeleportToPlayer);
			RegisterPacketHandler(game::client_realm_packet::CheatSummon, *this, &Player::OnCheatSummon);
			RegisterPacketHandler(game::client_realm_packet::GuildCreate, *this, &Player::OnGuildCreate);
#endif
		}
		else
		{
			ClearPacketHandler(game::client_realm_packet::ChatMessage);
			ClearPacketHandler(game::client_realm_packet::NameQuery);
			ClearPacketHandler(game::client_realm_packet::GuildQuery);
			ClearPacketHandler(game::client_realm_packet::CreatureQuery);
			ClearPacketHandler(game::client_realm_packet::ItemQuery);
			ClearPacketHandler(game::client_realm_packet::QuestQuery);
			ClearPacketHandler(game::client_realm_packet::SetActionBarButton);
			ClearPacketHandler(game::client_realm_packet::GroupInvite);
			ClearPacketHandler(game::client_realm_packet::GroupUninvite);
			ClearPacketHandler(game::client_realm_packet::GroupAccept);
			ClearPacketHandler(game::client_realm_packet::GroupDecline);

			ClearPacketHandler(game::client_realm_packet::GuildInvite);
			ClearPacketHandler(game::client_realm_packet::GuildRemove);
			ClearPacketHandler(game::client_realm_packet::GuildPromote);
			ClearPacketHandler(game::client_realm_packet::GuildDemote);
			ClearPacketHandler(game::client_realm_packet::GuildLeave);
			ClearPacketHandler(game::client_realm_packet::GuildDisband);
			ClearPacketHandler(game::client_realm_packet::GuildMotd);
			ClearPacketHandler(game::client_realm_packet::GuildAccept);
			ClearPacketHandler(game::client_realm_packet::GuildDecline);

#if MMO_WITH_DEV_COMMANDS
			ClearPacketHandler(game::client_realm_packet::CheatTeleportToPlayer);
			ClearPacketHandler(game::client_realm_packet::CheatSummon);
			ClearPacketHandler(game::client_realm_packet::GuildCreate);
#endif
		}
	}

	void Player::JoinWorld() const
	{
		if (const auto strongWorld = m_world.lock())
		{
			strongWorld->GetConnection().sendSinglePacket([](auth::OutgoingPacket &outPacket)
														  {
				outPacket.Start(auth::realm_world_packet::PlayerCharacterJoin);
				outPacket << io::write<uint32>(0);
				outPacket.Finish(); });
		}
	}

	void Player::OnWorldJoined(const std::shared_ptr<World> &world, const InstanceId instanceId)
	{
		DLOG("World join succeeded on instance id " << instanceId);

		m_world = world;
		m_instanceId = instanceId;
		NotifyWorldNodeChanged(world.get());

		EnableProxyPackets(true);

		ASSERT(m_characterData);
		m_characterData->instanceId = instanceId;

		m_connection->sendSinglePacket([&](game::OutgoingPacket &outPacket)
									   {
			outPacket.Start(game::realm_client_packet::LoginVerifyWorld);
			outPacket
				<< io::write<uint64>(m_characterData->mapId)	
				<< io::write<float>(m_characterData->position.x)
				<< io::write<float>(m_characterData->position.y)
				<< io::write<float>(m_characterData->position.z)
				<< io::write<float>(m_characterData->facing.GetValueRadians())
			;
			outPacket.Finish(); });

		// Send Message of the Day to the player
		String motd = GetManager().GetMessageOfTheDay();
		if (!motd.empty())
		{
			SendMessageOfTheDay(motd);
		}

		// Check if we are in a guild
		if (m_characterData->guildId != 0)
		{
			if (Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId))
			{
				// TODO: MOTD guild packet

				guild->BroadcastEvent(guild_event::LoggedIn, m_characterData->characterId, m_characterData->name.c_str());
			}
			else
			{
				ELOG("Player " << log_hex_digit(m_characterData->characterId) << " is in guild " << log_hex_digit(m_characterData->guildId) << " which could not be found (not loaded from database?)");
			}
		}

		// Load friend list and notify friends of online status
		LoadFriendList();
		m_friendMgr.NotifyFriendStatusChange(m_characterData->characterId, true);

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
		auto handler = [weakThis](const std::optional<ActionButtons> &actionButtons)
		{
			if (const auto strongThis = weakThis.lock())
			{
				strongThis->OnActionButtons(*actionButtons);
			}
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::GetActionButtons, m_characterData->characterId);
	}

	void Player::OnWorldChanged(const std::shared_ptr<World> &world, const InstanceId instanceId)
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

		m_connection->sendSinglePacket([response](game::OutgoingPacket &outPacket)
									   {
			outPacket.Start(game::realm_client_packet::EnterWorldFailed);
			outPacket << io::write<uint8>(response);
			outPacket.Finish(); });
	}

	void Player::OnWorldTransferAborted(const game::TransferAbortReason reason)
	{
		ELOG("World transfer aborted: " << reason);

		m_connection->sendSinglePacket([this, reason](game::OutgoingPacket &outPacket)
									   {
				outPacket.Start(game::realm_client_packet::TransferAborted);
				outPacket
					<< io::write<uint32>(m_transferMap)
					<< io::write<uint8>(reason);
				outPacket.Finish(); });
	}

	void Player::OnWorldLeft(const std::shared_ptr<World> &world, auth::WorldLeftReason reason)
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
			m_characterData.reset();
			EnableProxyPackets(false);
			EnableEnterWorldPacket(true);
			ILOG("Successfully logged out");

			m_connection->sendSinglePacket([this, reason](game::OutgoingPacket &outPacket)
										   {
					outPacket.Start(game::realm_client_packet::LogoutResponse);
					outPacket.Finish(); });

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
		m_characterData->isGameMaster = (m_gmLevel > 0);

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
		if (const proto::ClassEntry *classEntry = m_project.classes.getById(m_characterData->classId))
		{
			for (const auto &classSpellEntry : classEntry->spells())
			{
				if (m_characterData->level < classSpellEntry.level())
				{
					continue;
				}

				if (std::find_if(m_characterData->spellIds.begin(), m_characterData->spellIds.end(), [&classSpellEntry](const uint32 spellId)
								 { return spellId == classSpellEntry.spell(); }) == m_characterData->spellIds.end())
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
			} });
	}

	void Player::OnWorldDestroyed(World &world)
	{
		EnableProxyPackets(false);

		m_world.reset();
		NotifyWorldNodeChanged(nullptr);

		connectionLost();
	}

	void Player::NotifyWorldNodeChanged(World *worldNode)
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

		const proto::UnitEntry *unit = m_project.units.getById(entry);
		if (unit == nullptr)
		{
			WLOG("Could not find creature entry " << log_hex_digit(entry));
			m_connection->sendSinglePacket([entry](game::OutgoingPacket &packet)
										   {
					packet.Start(game::realm_client_packet::CreatureQueryResult);
					packet
						<< io::write_packed_guid(entry)
						<< io::write<uint8>(false);
					packet.Finish(); });
			return;
		}

		m_connection->sendSinglePacket([unit](game::OutgoingPacket &packet)
									   {
				packet.Start(game::realm_client_packet::CreatureQueryResult);
				packet
					<< io::write_packed_guid(unit->id())
					<< io::write<uint8>(true)
					<< io::write_range(unit->name()) << io::write<uint8>(0)
					<< io::write_range(unit->subname()) << io::write<uint8>(0);
				packet.Finish(); });
	}

	void Player::OnQueryQuest(uint64 entry)
	{
		DLOG("Querying for quest " << log_hex_digit(entry) << "...");

		// Check for existing quest entry
		const proto::QuestEntry *questEntry = m_project.quests.getById(entry);
		if (!questEntry)
		{
			m_connection->sendSinglePacket([entry](game::OutgoingPacket &packet)
										   {
					packet.Start(game::realm_client_packet::QuestQueryResult);
					packet
						<< io::write_packed_guid(entry)
						<< io::write<uint8>(false);
					packet.Finish(); });
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

		for (const auto &requirement : questEntry->requirements())
		{
			if (requirement.itemid() != 0)
			{
				quest.requiredItems.emplace_back(requirement.itemid(), requirement.itemcount());
			}
			else if (requirement.creatureid() != 0)
			{
				quest.requiredCreatures.emplace_back(requirement.creatureid(), requirement.creaturecount());
			}
		}

		for (const auto &reward : questEntry->rewarditems())
		{
			quest.rewardItems.emplace_back(reward.itemid(), reward.count());
		}

		for (const auto &reward : questEntry->rewarditemschoice())
		{
			quest.optionalItems.emplace_back(reward.itemid(), reward.count());
		}

		m_connection->sendSinglePacket([entry, &quest](game::OutgoingPacket &packet)
									   {
				packet.Start(game::realm_client_packet::QuestQueryResult);
				packet
					<< io::write_packed_guid(entry)
					<< io::write<uint8>(true)
					<< quest;
				packet.Finish(); });
	}

	void Player::OnQueryItem(uint64 entry)
	{
		DLOG("Querying for item " << log_hex_digit(entry) << "...");

		const proto::ItemEntry *itemEntry = m_project.items.getById(entry);
		if (!itemEntry)
		{
			WLOG("Item with entry " << entry << " could not be found!");
			m_connection->sendSinglePacket([entry](game::OutgoingPacket &packet)
										   {
					packet.Start(game::realm_client_packet::ItemQueryResult);
					packet
						<< io::write_packed_guid(entry)
						<< io::write<uint8>(false);
					packet.Finish(); });
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
				const auto &stat = itemEntry->stats(i);
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
				const auto &spell = itemEntry->spells(i);
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

		m_connection->sendSinglePacket([entry, &info](game::OutgoingPacket &packet)
									   {
				packet.Start(game::realm_client_packet::ItemQueryResult);
				packet
					<< io::write_packed_guid(entry)
					<< io::write<uint8>(true)
					<< info;
				packet.Finish(); });
	}

	void Player::OnQueryObject(uint64 entry)
	{
		DLOG("Querying for object " << log_hex_digit(entry) << "...");

		const proto::ObjectEntry *object = m_project.objects.getById(entry);
		if (object == nullptr)
		{
			WLOG("Could not find object entry " << log_hex_digit(entry));
			m_connection->sendSinglePacket([entry](game::OutgoingPacket &packet)
										   {
					packet.Start(game::realm_client_packet::ObjectQueryResult);
					packet
						<< io::write_packed_guid(entry)
						<< io::write<uint8>(false);
					packet.Finish(); });
			return;
		}

		ObjectInfo info;
		info.id = entry;
		info.type = object->type();
		info.displayId = object->displayid();
		info.name = object->name();
		for (int32 i = 0; i < 16; ++i)
		{
			info.data[i] = object->data_size() <= i ? 0 : object->data(i);
		}

		m_connection->sendSinglePacket([&info](game::OutgoingPacket &packet)
									   {
				packet.Start(game::realm_client_packet::ObjectQueryResult);
				packet
					<< io::write_packed_guid(info.id)
					<< io::write<uint8>(true)
					<< info;
				packet.Finish(); });
	}

	void Player::OnActionButtons(const ActionButtons &actionButtons)
	{
		m_actionButtons = actionButtons;

		m_connection->sendSinglePacket([&actionButtons](game::OutgoingPacket &packet)
									   {
				packet.Start(game::realm_client_packet::ActionButtons);
				packet << io::write_range(actionButtons);
				packet.Finish(); });
	}

	void Player::FetchCharacterLocationAsync(CharacterLocationAsyncCallback &&callback)
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

	void Player::CharacterLocationResponseNotification(const bool succeeded, const uint64 ackId, const uint32 mapId, const Vector3 &position, const Radian &facing)
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

	void Player::NotifyQuestData(uint32 questId, const QuestStatusData &questData)
	{
		if (!m_characterData)
		{
			return;
		}

		if (questData.status == quest_status::Rewarded)
		{
			m_characterData->rewardedQuestIds.push_back(questId);
			m_characterData->questStatus.erase(questId);
		}
		else
		{
			m_characterData->questStatus[questId] = questData;
		}
	}

	PacketParseResult Player::OnGroupUpdate(auth::IncomingPacket &packet)
	{
		std::vector<uint64> nearbyMembers;
		uint32 health, maxHealth, power, maxPower, mapId, zoneId;
		uint8 powerType, level;
		Vector3 location;

		if (!(packet >> io::read_container<uint8>(nearbyMembers) >> io::read<uint32>(health) >> io::read<uint32>(maxHealth) >> io::read<uint8>(powerType) >> io::read<uint32>(power) >> io::read<uint32>(maxPower) >> io::read<uint8>(level) >> io::read<uint32>(mapId) >> io::read<uint32>(zoneId) >> io::read<float>(location.x) >> io::read<float>(location.y) >> io::read<float>(location.z)))
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
		case power_type::Mana:
			m_characterData->mana = power;
			m_characterData->maxMana = maxPower;
			break;
		case power_type::Rage:
			m_characterData->rage = power;
			m_characterData->maxRage = maxPower;
			break;
		case power_type::Energy:
			m_characterData->energy = power;
			m_characterData->maxEnergy = maxPower;
			break;
		}

		m_characterData->level = level;
		m_characterData->position = location;
		m_characterData->mapId = mapId;

		nearbyMembers.push_back(m_characterData->characterId);

		// Broadcast to far members
		m_group->BroadcastPacket([&](game::OutgoingPacket &packet)
								 { BuildPartyMemberStatsPacket(packet, group_update_flags::Full); }, &nearbyMembers, m_characterData->characterId);

		return PacketParseResult::Pass;
	}

	void Player::SendTeleportRequest(uint32 mapId, const Vector3 &position, const Radian &facing) const
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

	void Player::SendPartyOperationResult(PartyOperation operation, PartyResult result, const String &playerName)
	{
		m_connection->sendSinglePacket([operation, result, playerName](game::OutgoingPacket &packet)
									   {
				packet.Start(game::realm_client_packet::PartyCommandResult);
				packet
					<< io::write<uint8>(operation)
					<< io::write_dynamic_range<uint8>(playerName)
					<< io::write<uint8>(result);
				packet.Finish(); });
	}

	void Player::SendGuildCommandResult(uint8 command, uint8 result, const String &playerName)
	{
		m_connection->sendSinglePacket([command, result, playerName](game::OutgoingPacket &packet)
									   {
				packet.Start(game::realm_client_packet::GuildCommandResult);
				packet
					<< io::write<uint8>(command)
					<< io::write<uint8>(result)
					<< io::write_dynamic_range<uint8>(playerName);
				packet.Finish(); });
	}

	void Player::OnGuildRemoveCharacterIdResolve(DatabaseId characterId, const String &playerName)
	{
		Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
		if (!guild)
		{
			ELOG("Player tried to remove player from guild that doesn't exist");
			SendGuildCommandResult(game::guild_command::Uninvite, game::guild_command_result::NotInGuild, "");
			return;
		}

		// Remove the player from the guild
		if (!guild->RemoveMember(characterId))
		{
			ELOG("Failed to remove player " << playerName << " from guild");
			return;
		}

		// Removed event
		guild->BroadcastEvent(guild_event::Removed, characterId, playerName.c_str());

		// Notify the player that they have been removed from the guild
		if (Player *targetPlayer = m_manager.GetPlayerByCharacterGuid(characterId))
		{
			targetPlayer->GuildChange(0);
			targetPlayer->GetConnection().sendSinglePacket([&playerName](game::OutgoingPacket &packet)
														   {
				packet.Start(game::realm_client_packet::GuildUninvite);
				packet << io::write_dynamic_range<uint8>(playerName);
				packet.Finish(); });
		}
	}

	void Player::RegisterPacketHandler(uint16 opCode, PacketHandler &&handler)
	{
		std::scoped_lock lock{m_packetHandlerMutex};

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
		std::scoped_lock lock{m_packetHandlerMutex};

		if (auto it = m_packetHandlers.find(opCode); it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}

	PacketParseResult Player::OnGuildInvite(game::IncomingPacket &packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to invite " << playerName << " to guild");

		if (!m_characterData)
		{
			ELOG("Player tried to invite player to guild without having character data");
			return PacketParseResult::Disconnect;
		}

		if (m_characterData->guildId == 0)
		{
			ELOG("Player tried to invite player to guild without being in a guild");
			SendGuildCommandResult(game::guild_command::Invite, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
		if (!guild)
		{
			ELOG("Player tried to invite player to guild that doesn't exist");
			return PacketParseResult::Disconnect;
		}

		// Check if the player has permission to invite members
		if (!guild->HasPermission(m_characterData->characterId, guild_rank_permissions::Invite))
		{
			ELOG("Player tried to invite player to guild without having permission to invite members");
			SendGuildCommandResult(game::guild_command::Invite, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		// Find the target player
		Player *targetPlayer = m_manager.GetPlayerByCharacterName(playerName);
		if (!targetPlayer)
		{
			ELOG("Player tried to invite player to guild who is not online");
			SendGuildCommandResult(game::guild_command::Invite, game::guild_command_result::PlayerNotFound, playerName);
			return PacketParseResult::Pass;
		}

		if (targetPlayer->m_pendingGuildInvite != 0)
		{
			ELOG("Player tried to invite player to guild who already has a pending invite");
			SendGuildCommandResult(game::guild_command::Invite, game::guild_command_result::InvitePending, playerName);
			return PacketParseResult::Pass;
		}

		if (targetPlayer->m_characterData->guildId != 0)
		{
			ELOG("Player tried to invite player to guild who is already in a guild");
			if (targetPlayer->m_characterData->guildId == m_characterData->guildId)
			{
				SendGuildCommandResult(game::guild_command::Invite, game::guild_command_result::AlreadyInGuild, playerName);
			}
			else
			{
				SendGuildCommandResult(game::guild_command::Invite, game::guild_command_result::AlreadyInOtherGuild, playerName);
			}
			return PacketParseResult::Pass;
		}

		// Set pending guild invite
		targetPlayer->m_pendingGuildInvite = guild->GetId();
		targetPlayer->m_guildInviter = m_characterData->characterId;

		// Send guild invite to target player
		targetPlayer->GetConnection().sendSinglePacket([this, guild](game::OutgoingPacket &packet)
													   {
				packet.Start(game::realm_client_packet::GuildInvite);
				packet
					<< io::write_dynamic_range<uint8>(m_characterData->name)
					<< io::write_dynamic_range<uint8>(guild->GetName());
				packet.Finish(); });

		// Success
		SendGuildCommandResult(game::guild_command::Invite, game::guild_command_result::Ok, playerName);
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildRemove(game::IncomingPacket &packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to remove " << playerName << " from guild");
		if (!m_characterData)
		{
			ELOG("Player tried to remove player from guild without having character data");
			return PacketParseResult::Disconnect;
		}

		if (m_characterData->guildId == 0)
		{
			ELOG("Player tried to remove player from guild without being in a guild");
			SendGuildCommandResult(game::guild_command::Leave, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
		if (!guild)
		{
			ELOG("Player tried to remove player from guild that doesn't exist");
			SendGuildCommandResult(game::guild_command::Uninvite, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		// Check if the player has permission to remove members
		if (!guild->HasPermission(m_characterData->characterId, guild_rank_permissions::Remove))
		{
			ELOG("Player tried to remove player from guild without having permission to remove members");
			SendGuildCommandResult(game::guild_command::Uninvite, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		if (playerName == m_characterData->name)
		{
			ELOG("Player tried to remove himself from guild");
			SendGuildCommandResult(game::guild_command::Uninvite, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		// Okay, now we need to find the guid of the player by name
		std::weak_ptr weakThis = shared_from_this();
		auto handler = [weakThis, playerName](const std::optional<DatabaseId> &characterId)
		{
			auto strong = weakThis.lock();
			if (!strong)
			{
				return;
			}

			if (!characterId)
			{
				strong->SendGuildCommandResult(game::guild_command::Uninvite, game::guild_command_result::PlayerNotFound, playerName);
				return;
			}

			strong->OnGuildRemoveCharacterIdResolve(*characterId, playerName);
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterIdByName, playerName);
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildPromote(game::IncomingPacket &packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to promote " << playerName << " in guild");

		if (!m_characterData)
		{
			ELOG("Player tried to promote player in guild without having character data");
			return PacketParseResult::Disconnect;
		}

		if (m_characterData->guildId == 0)
		{
			ELOG("Player tried to promote player in guild without being in a guild");
			SendGuildCommandResult(game::guild_command::Promote, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
		if (!guild)
		{
			ELOG("Player tried to promote player in guild that doesn't exist");
			SendGuildCommandResult(game::guild_command::Promote, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		// Otherwise check if the player has permission to promote members
		if (!guild->HasPermission(m_characterData->characterId, guild_rank_permissions::Promote))
		{
			ELOG("Player tried to promote player in guild without having permission to promote members");
			SendGuildCommandResult(game::guild_command::Promote, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		std::weak_ptr weakThis = shared_from_this();
		auto handler = [weakThis, playerName](const std::optional<CharacterLocationData> &data)
		{
			auto strong = weakThis.lock();
			if (!strong)
			{
				return;
			}

			if (!data)
			{
				ELOG("Failed to find player " << playerName);
				strong->SendGuildCommandResult(game::guild_command::Promote, game::guild_command_result::PlayerNotFound, playerName);
				return;
			}

			Guild *guild = strong->m_guildMgr.GetGuild(strong->m_characterData->guildId);
			if (!guild)
			{
				return;
			}

			// Find the target player's GUID and current rank
			if (!guild->IsMember(data->characterId))
			{
				ELOG("Failed to find player " << playerName << " in guild");
				strong->SendGuildCommandResult(game::guild_command::Promote, game::guild_command_result::PlayerNotFound, playerName);
				return;
			}

			uint64 targetGuid = data->characterId;
			uint32 targetRank = guild->GetMemberRank(targetGuid);

			// Cannot promote a player with a higher or equal rank as the player
			// The guild leader (rank 0) for example can never promote anybody to rank 0 as well.
			// An officer (rank 1) can not promote another player to an officer (rank 1) either.
			// An officer (rank 1) can promote an initiate (rank 4) to a member (rank 3) or a veteran (rank 2).
			if (const uint32 ourRank = guild->GetMemberRank(strong->m_characterData->characterId); targetRank == 0 || targetRank <= ourRank + 1)
			{
				ELOG("Cannot promote a player to the same or higher rank as the promoter");
				strong->SendGuildCommandResult(game::guild_command::Promote, game::guild_command_result::NotAllowed, "");
				return;
			}

			// Promote the player
			if (!guild->PromoteMember(targetGuid, strong->m_characterData->name, playerName))
			{
				ELOG("Failed to promote player " << playerName << " in guild");
			}
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterLocationDataByName, playerName);

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildDemote(game::IncomingPacket &packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to demote " << playerName << " in guild");

		if (!m_characterData)
		{
			ELOG("Player tried to promote player in guild without having character data");
			return PacketParseResult::Disconnect;
		}

		if (m_characterData->guildId == 0)
		{
			ELOG("Player tried to demote player in guild without being in a guild");
			SendGuildCommandResult(game::guild_command::Demote, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
		if (!guild)
		{
			ELOG("Player tried to demote player in guild that doesn't exist");
			SendGuildCommandResult(game::guild_command::Demote, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		// Otherwise check if the player has permission to promote members
		if (!guild->HasPermission(m_characterData->characterId, guild_rank_permissions::Promote))
		{
			ELOG("Player tried to demote player in guild without having permission to demote members");
			SendGuildCommandResult(game::guild_command::Demote, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		std::weak_ptr weakThis = shared_from_this();
		auto handler = [weakThis, playerName](const std::optional<CharacterLocationData> &data)
		{
			auto strong = weakThis.lock();
			if (!strong)
			{
				return;
			}

			if (!data)
			{
				ELOG("Failed to find player " << playerName);
				strong->SendGuildCommandResult(game::guild_command::Demote, game::guild_command_result::PlayerNotFound, playerName);
				return;
			}

			Guild *guild = strong->m_guildMgr.GetGuild(strong->m_characterData->guildId);
			if (!guild)
			{
				return;
			}

			// Find the target player's GUID and current rank
			if (!guild->IsMember(data->characterId))
			{
				ELOG("Failed to find player " << playerName << " in guild");
				strong->SendGuildCommandResult(game::guild_command::Demote, game::guild_command_result::PlayerNotFound, playerName);
				return;
			}

			uint64 targetGuid = data->characterId;
			uint32 targetRank = guild->GetMemberRank(targetGuid);

			if (targetRank >= guild->GetLowestRank())
			{
				ELOG("Unable to demote player who is already on the lowest rank");
				strong->SendGuildCommandResult(game::guild_command::Demote, game::guild_command_result::NotAllowed, playerName);
				return;
			}

			// Cannot demote a player with a higher or equal rank as the player
			if (const uint32 ourRank = guild->GetMemberRank(strong->m_characterData->characterId); targetRank <= ourRank)
			{
				ELOG("Cannot demote a player with the same or a higher rank");
				strong->SendGuildCommandResult(game::guild_command::Demote, game::guild_command_result::NotAllowed, "");
				return;
			}

			// Promote the player
			if (!guild->DemoteMember(targetGuid, strong->m_characterData->name, playerName))
			{
				ELOG("Failed to demote player " << playerName << " in guild");
			}
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterLocationDataByName, playerName);

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildLeave(game::IncomingPacket &packet)
	{
		DLOG("Player wants to leave guild");

		if (!m_characterData)
		{
			ELOG("Player tried to leave guild without having character data");
			return PacketParseResult::Disconnect;
		}

		if (m_characterData->guildId == 0)
		{
			ELOG("Player tried to leave guild without being in a guild");
			SendGuildCommandResult(game::guild_command::Leave, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
		if (!guild)
		{
			ELOG("Player tried to leave guild that doesn't exist");
			SendGuildCommandResult(game::guild_command::Leave, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		// Cannot leave the guild if the player is the guild leader
		if (guild->GetLeaderGuid() == m_characterData->characterId)
		{
			if (!m_guildMgr.DisbandGuild(guild->GetId()))
			{
				ELOG("Failed to disband guild");
			}

			return PacketParseResult::Pass;
		}

		// Remove the player from the guild
		if (!guild->RemoveMember(m_characterData->characterId))
		{
			ELOG("Failed to remove player from guild");
			return PacketParseResult::Pass;
		}

		// Leave event
		guild->BroadcastEvent(guild_event::Left, m_characterData->characterId, m_characterData->name.c_str());

		// Update the player's guild ID
		SendGuildCommandResult(game::guild_command::Leave, game::guild_command_result::Ok, "");
		GuildChange(0);

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildDisband(game::IncomingPacket &packet)
	{
		DLOG("Player wants to disband guild");

		if (!m_characterData)
		{
			ELOG("Player tried to disband guild without having character data");
			return PacketParseResult::Disconnect;
		}

		if (m_characterData->guildId == 0)
		{
			ELOG("Player tried to disband guild without being in a guild");
			return PacketParseResult::Pass;
		}

		Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
		if (!guild)
		{
			ELOG("Player tried to disband guild that doesn't exist");
			SendGuildCommandResult(game::guild_command::Disband, game::guild_command_result::NotInGuild, "");
			return PacketParseResult::Pass;
		}

		// Only the guild leader can disband the guild
		if (guild->GetLeaderGuid() != m_characterData->characterId)
		{
			ELOG("Only the guild leader can disband the guild");
			SendGuildCommandResult(game::guild_command::Disband, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		if (!m_guildMgr.DisbandGuild(guild->GetId()))
		{
			ELOG("Failed to disband guild");
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnGuildMotd(game::IncomingPacket &packet)
	{
		String motd;
		if (!(packet >> io::read_container<uint8>(motd)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to set guild MOTD to: " << motd);

		if (!m_characterData)
		{
			ELOG("Player tried to set guild MOTD without having character data");
			return PacketParseResult::Disconnect;
		}

		if (m_characterData->guildId == 0)
		{
			ELOG("Player tried to set guild MOTD without being in a guild");
			return PacketParseResult::Pass;
		}

		Guild *guild = m_guildMgr.GetGuild(m_characterData->guildId);
		if (!guild)
		{
			ELOG("Player tried to set MOTD for guild that doesn't exist");
			return PacketParseResult::Pass;
		}

		// Check if the player has permission to set the MOTD
		if (!guild->HasPermission(m_characterData->characterId, guild_rank_permissions::SetMotd))
		{
			ELOG("Player tried to set guild MOTD without having permission to set MOTD");
			return PacketParseResult::Pass;
		}

		// TODO: Set the guild MOTD
		ELOG("Set guild MOTD functionality not implemented yet");

		return PacketParseResult::Pass;
	}

	void Player::SendMessageOfTheDay(const std::string &motd)
	{
		ASSERT(m_characterData);

		SendPacket([&motd](game::OutgoingPacket &packet)
				   {
				packet.Start(game::realm_client_packet::MessageOfTheDay);
				packet << io::write_dynamic_range<uint16>(motd);
				packet.Finish(); });
	}

	void Player::HandleCharacterGuildOnDelete(uint64 charGuid)
	{
		// First, we need to get the character's guild information from the database
		// Since this character might not be the currently logged-in character,
		// we need to make an async database request to get their guild membership
		std::weak_ptr weakThis{shared_from_this()};
		auto handler = [weakThis, charGuid](const std::optional<CharacterData> &characterData)
		{
			const auto strongThis = weakThis.lock();
			if (!strongThis)
			{
				return;
			}

			if (!characterData || characterData->guildId == 0)
			{
				// Character is not in a guild, nothing to do
				return;
			}

			const uint64 guildId = characterData->guildId;
			Guild *guild = strongThis->m_guildMgr.GetGuild(guildId);
			if (!guild)
			{
				WLOG("Character " << charGuid << " is in guild " << guildId << " which could not be found");
				return;
			}

			// Check if the character is the guild leader
			if (guild->GetLeaderGuid() == charGuid)
			{
				DLOG("Deleting character " << charGuid << " who is the leader of guild " << guildId << ". Disbanding guild.");

				// Character is the guild leader, disband the guild
				if (!strongThis->m_guildMgr.DisbandGuild(guildId))
				{
					ELOG("Failed to disband guild " << guildId << " when deleting guild leader character " << charGuid);
				}
			}
			else
			{
				DLOG("Removing character " << charGuid << " from guild " << guildId);

				// Character is a regular member, just remove them from the guild
				if (!guild->RemoveMember(charGuid))
				{
					ELOG("Failed to remove character " << charGuid << " from guild " << guildId);
				}
				else
				{
					// Broadcast that the member left (they were removed due to character deletion)
					guild->BroadcastEvent(guild_event::Left, charGuid, characterData->name.c_str());
				}
			}
		};

		// Request character data to check guild membership
		m_database.asyncRequest(std::move(handler), &IDatabase::CharacterEnterWorld, charGuid, m_accountId);
	}

	void Player::HandleCharacterGroupOnDelete(uint64 charGuid)
	{
		// Check if the character is in any group by searching through all groups
		// Since the character might not be currently online, we need to check the database
		std::weak_ptr weakThis{shared_from_this()};
		auto handler = [weakThis, charGuid](const std::optional<CharacterData> &characterData)
		{
			const auto strongThis = weakThis.lock();
			if (!strongThis)
			{
				return;
			}

			if (!characterData || characterData->groupId == 0)
			{
				// Character is not in a group, nothing to do
				return;
			}

			const uint64 groupId = characterData->groupId;

			// Find the group
			auto groupIt = PlayerGroup::ms_groupsById.find(groupId);
			if (groupIt == PlayerGroup::ms_groupsById.end())
			{
				WLOG("Character " << charGuid << " is in group " << groupId << " which could not be found");
				return;
			}

			std::shared_ptr<PlayerGroup> group = groupIt->second;
			if (!group->IsMember(charGuid))
			{
				WLOG("Character " << charGuid << " is not actually a member of group " << groupId);
				return;
			}

			// Check if the character is the group leader
			if (group->GetLeader() == charGuid)
			{
				DLOG("Deleting character " << charGuid << " who is the leader of group " << groupId << ". Disbanding group.");

				// Character is the group leader, disband the group
				group->Disband(false);
			}
			else
			{
				DLOG("Removing character " << charGuid << " from group " << groupId);

				// Character is a regular member, just remove them from the group
				group->RemoveMember(charGuid);
			}
		};

		// Request character data to check group membership
		m_database.asyncRequest(std::move(handler), &IDatabase::CharacterEnterWorld, charGuid, m_accountId);
	}

	void Player::HandleCharacterFriendsOnDelete(uint64 charGuid)
	{
		// Remove the character from all friend lists
		// First, get all characters who have this character as a friend
		std::weak_ptr weakThis{shared_from_this()};
		auto handler = [weakThis, charGuid](const std::vector<uint64> &friendIds)
		{
			const auto strongThis = weakThis.lock();
			if (!strongThis)
			{
				return;
			}

			if (friendIds.empty())
			{
				// No one has this character as a friend, nothing to do
				return;
			}

			DLOG("Removing character " << charGuid << " from " << friendIds.size() << " friend lists");

			// Remove the friendship from the database for each friend
			for (const uint64 friendId : friendIds)
			{
				strongThis->m_database.asyncRequest<void>([charGuid, friendId](auto &&database)
														  { database->RemoveFriend(friendId, charGuid); },
														  [charGuid, friendId](const bool success)
														  {
															  if (!success)
															  {
																  ELOG("Failed to remove friendship between " << friendId << " and deleted character " << charGuid);
															  }
														  });

				// If the friend is currently online, update their friend list
				Player *friendPlayer = strongThis->m_manager.GetPlayerByCharacterGuid(friendId);
				if (friendPlayer)
				{
					// Reload the friend's friend list from the database
					strongThis->m_friendMgr.LoadCharacterFriends(friendId, [friendPlayer](const std::vector<FriendData> &friends)
																 {
						friendPlayer->m_friendCache = friends;
						friendPlayer->SendFriendListUpdate(); });
				}
			}
		};

		// Get all characters who have this character as a friend
		m_database.asyncRequest(std::move(handler), &IDatabase::GetCharactersWithFriend, charGuid);
	}
}
