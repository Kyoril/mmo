// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "guild_module.h"

#include "database.h"
#include "guild_mgr.h"
#include "player_manager.h"

#include "base/clock.h"
#include "base/utilities.h"
#include "game/guild_info.h"
#include "log/default_log_levels.h"

#include <limits>
#include <vector>

namespace mmo
{
	GuildModule::GuildModule(GuildMgr& guildMgr, PlayerManager& playerManager, AsyncDatabase& database)
		: m_guildMgr(guildMgr)
		, m_playerManager(playerManager)
		, m_database(database)
	{
	}

	void GuildModule::RegisterPacketHandlers(Player& player, Player::PacketHandlerHandleContainer& handlers)
	{
		// Every guild request is refused while the subsystem is off. The command byte of the answer
		// names the closest command; the client shows the same message for any of them.
		const auto add = [this, &player, &handlers](const uint16 opCode, const game::guild_command::Type command, auto handler)
		{
			handlers += player.RegisterAutoPacketHandler(opCode, [this, &player, command, handler](game::IncomingPacket& packet)
			{
				if (!IsAvailableTo(player))
				{
					SendGuildCommandResult(player, command, game::guild_command_result::Disabled, "");
					return PacketParseResult::Pass;
				}

				return (this->*handler)(player, packet);
			});
		};

		add(game::client_realm_packet::GuildInvite, game::guild_command::Invite, &GuildModule::OnGuildInvite);
		add(game::client_realm_packet::GuildRemove, game::guild_command::Uninvite, &GuildModule::OnGuildRemove);
		add(game::client_realm_packet::GuildLeave, game::guild_command::Leave, &GuildModule::OnGuildLeave);
		add(game::client_realm_packet::GuildDisband, game::guild_command::Disband, &GuildModule::OnGuildDisband);
		add(game::client_realm_packet::GuildMotd, game::guild_command::SetMOTD, &GuildModule::OnGuildMotd);
		add(game::client_realm_packet::GuildAccept, game::guild_command::Invite, &GuildModule::OnGuildAccept);
		add(game::client_realm_packet::GuildDecline, game::guild_command::Invite, &GuildModule::OnGuildDecline);

		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::GuildPromote, [this, &player](game::IncomingPacket& packet)
		{
			if (!IsAvailableTo(player))
			{
				SendGuildCommandResult(player, game::guild_command::Promote, game::guild_command_result::Disabled, "");
				return PacketParseResult::Pass;
			}

			return OnGuildRankChange(player, packet, game::guild_command::Promote);
		});
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::GuildDemote, [this, &player](game::IncomingPacket& packet)
		{
			if (!IsAvailableTo(player))
			{
				SendGuildCommandResult(player, game::guild_command::Demote, game::guild_command_result::Disabled, "");
				return PacketParseResult::Pass;
			}

			return OnGuildRankChange(player, packet, game::guild_command::Demote);
		});

		// The roster is requested by the guild panel, which the client keeps closed while the
		// subsystem is off; a request that still arrives is dropped without an answer.
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::GuildRoster, [this, &player](game::IncomingPacket& packet)
		{
			if (!IsAvailableTo(player))
			{
				return PacketParseResult::Pass;
			}

			return OnGuildRoster(player, packet);
		});

		// Polled by the client's world map and minimap. Like the roster, a request that arrives
		// while the subsystem is off is dropped without an answer.
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::GuildMemberPositions, [this, &player](game::IncomingPacket& packet)
		{
			if (!IsAvailableTo(player))
			{
				return PacketParseResult::Pass;
			}

			return OnGuildMemberPositions(player, packet);
		});

#if MMO_WITH_DEV_COMMANDS
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::GuildCreate, [this, &player](game::IncomingPacket& packet)
		{
			if (!IsAvailableTo(player))
			{
				player.SendSystemMessage("Guilds are disabled (GUILD subsystem is off).");
				return PacketParseResult::Pass;
			}

			return OnGuildCreate(player, packet);
		});
#endif
	}

	void GuildModule::OnEnteredWorld(Player& player)
	{
		const uint64 guildId = player.GetCharacterData().guildId;
		if (guildId == 0)
		{
			return;
		}

		Guild* guild = m_guildMgr.GetGuild(guildId);
		if (!guild)
		{
			ELOG("Player " << log_hex_digit(player.GetCharacterGuid()) << " is in guild " << log_hex_digit(guildId) << " which could not be found (not loaded from database?)");
			return;
		}

		if (IsAvailableTo(player))
		{
			guild->BroadcastEvent(guild_event::LoggedIn, player.GetCharacterGuid(), player.GetCharacterName().c_str());
		}
	}

	void GuildModule::OnLeftWorld(Player& player)
	{
		// An invite is for the character, not the session: it ends when the character leaves
		m_pendingInvites.erase(player.GetCharacterGuid());
		m_lastPositionRequest.erase(player.GetCharacterGuid());

		if (!IsAvailableTo(player))
		{
			return;
		}

		if (Guild* guild = GetPlayerGuild(player))
		{
			guild->BroadcastEvent(guild_event::LoggedOut, player.GetCharacterGuid(), player.GetCharacterName().c_str());
		}
	}

	void GuildModule::OnCharacterDeleting(Player& player, const uint64 characterGuid)
	{
		m_pendingInvites.erase(characterGuid);

		// The character is not logged in, so its guild membership has to come from the database
		auto handler = [this, characterGuid](const std::optional<CharacterData>& characterData)
		{
			if (!characterData || characterData->guildId == 0)
			{
				return;
			}

			const uint64 guildId = characterData->guildId;
			Guild* guild = m_guildMgr.GetGuild(guildId);
			if (!guild)
			{
				WLOG("Character " << characterGuid << " is in guild " << guildId << " which could not be found");
				return;
			}

			// A guild cannot exist without its leader
			if (guild->GetLeaderGuid() == characterGuid)
			{
				DLOG("Deleting character " << characterGuid << " who is the leader of guild " << guildId << ". Disbanding guild.");
				if (!m_guildMgr.DisbandGuild(guildId))
				{
					ELOG("Failed to disband guild " << guildId << " when deleting guild leader character " << characterGuid);
				}

				return;
			}

			DLOG("Removing character " << characterGuid << " from guild " << guildId);
			if (!guild->RemoveMember(characterGuid))
			{
				ELOG("Failed to remove character " << characterGuid << " from guild " << guildId);
				return;
			}

			guild->BroadcastEvent(guild_event::Left, characterGuid, characterData->name.c_str());
		};

		m_database.asyncRequestKeyed(characterGuid, std::move(handler), &IDatabase::CharacterEnterWorld, characterGuid, player.GetAccountId());
	}

	bool GuildModule::OnChatMessage(Player& player, const ChatType type, const String& message)
	{
		if (type != ChatType::Guild && type != ChatType::GuildOfficer)
		{
			return false;
		}

		if (!IsAvailableTo(player))
		{
			SendGuildCommandResult(player, game::guild_command::Invite, game::guild_command_result::Disabled, "");
			return true;
		}

		Guild* guild = GetPlayerGuild(player);
		if (!guild)
		{
			WLOG("Player tried to send guild chat message without being in a guild!");
			return true;
		}

		const bool officer = type == ChatType::GuildOfficer;
		const uint64 senderGuid = player.GetCharacterGuid();

		if (!guild->HasPermission(senderGuid, officer ? guild_rank_permissions::WriteOfficerChat : guild_rank_permissions::WriteGuildChat))
		{
			WLOG("Player tried to send guild chat message without having permission to write to guild chat!");
			return true;
		}

		guild->BroadcastPacketWithPermission([senderGuid, type, &message](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::ChatMessage);
			outPacket
				<< io::write_packed_guid(senderGuid)
				<< io::write<uint8>(static_cast<uint8>(type))
				<< io::write_range(message)
				<< io::write<uint8>(0)  // Chat flags
				<< io::write<uint8>(0); // Chat tag
			outPacket.Finish();
		}, officer ? guild_rank_permissions::ReadOfficerChat : guild_rank_permissions::ReadGuildChat);

		return true;
	}

	PacketParseResult GuildModule::OnGuildInvite(Player& player, game::IncomingPacket& packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to invite " << playerName << " to guild");

		Guild* guild = GetPlayerGuild(player, game::guild_command::Invite);
		if (!guild)
		{
			return PacketParseResult::Pass;
		}

		if (!guild->HasPermission(player.GetCharacterGuid(), guild_rank_permissions::Invite))
		{
			SendGuildCommandResult(player, game::guild_command::Invite, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		Player* targetPlayer = m_playerManager.GetPlayerByCharacterName(playerName);
		if (!targetPlayer || !targetPlayer->HasCharacterGuid())
		{
			SendGuildCommandResult(player, game::guild_command::Invite, game::guild_command_result::PlayerNotFound, playerName);
			return PacketParseResult::Pass;
		}

		const uint64 targetGuid = targetPlayer->GetCharacterGuid();
		if (m_pendingInvites.find(targetGuid) != m_pendingInvites.end())
		{
			SendGuildCommandResult(player, game::guild_command::Invite, game::guild_command_result::InvitePending, playerName);
			return PacketParseResult::Pass;
		}

		if (const uint64 targetGuildId = targetPlayer->GetCharacterData().guildId; targetGuildId != 0)
		{
			SendGuildCommandResult(player, game::guild_command::Invite,
				targetGuildId == guild->GetId() ? game::guild_command_result::AlreadyInGuild : game::guild_command_result::AlreadyInOtherGuild,
				playerName);
			return PacketParseResult::Pass;
		}

		m_pendingInvites[targetGuid] = PendingInvite{ guild->GetId(), player.GetCharacterGuid() };

		const String& inviterName = player.GetCharacterName();
		const String& guildName = guild->GetName();
		targetPlayer->GetConnection().sendSinglePacket([&inviterName, &guildName](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::GuildInvite);
			outPacket
				<< io::write_dynamic_range<uint8>(inviterName)
				<< io::write_dynamic_range<uint8>(guildName);
			outPacket.Finish();
		});

		SendGuildCommandResult(player, game::guild_command::Invite, game::guild_command_result::Ok, playerName);
		return PacketParseResult::Pass;
	}

	PacketParseResult GuildModule::OnGuildRemove(Player& player, game::IncomingPacket& packet)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to remove " << playerName << " from guild");

		Guild* guild = GetPlayerGuild(player, game::guild_command::Uninvite);
		if (!guild)
		{
			return PacketParseResult::Pass;
		}

		if (!guild->HasPermission(player.GetCharacterGuid(), guild_rank_permissions::Remove) || playerName == player.GetCharacterName())
		{
			SendGuildCommandResult(player, game::guild_command::Uninvite, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		// The target may be offline, so its guid comes from the database
		const uint64 removerGuid = player.GetCharacterGuid();
		auto handler = [this, removerGuid, playerName](const std::optional<DatabaseId>& characterId)
		{
			if (!characterId)
			{
				if (Player* remover = m_playerManager.GetPlayerByCharacterGuid(removerGuid))
				{
					SendGuildCommandResult(*remover, game::guild_command::Uninvite, game::guild_command_result::PlayerNotFound, playerName);
				}
				return;
			}

			RemoveMember(removerGuid, *characterId, playerName);
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterIdByName, playerName);
		return PacketParseResult::Pass;
	}

	void GuildModule::RemoveMember(const uint64 removerGuid, const uint64 targetGuid, const String& targetName)
	{
		Player* remover = m_playerManager.GetPlayerByCharacterGuid(removerGuid);
		if (!remover)
		{
			return;
		}

		Guild* guild = GetPlayerGuild(*remover, game::guild_command::Uninvite);
		if (!guild)
		{
			return;
		}

		if (!guild->IsMember(targetGuid))
		{
			SendGuildCommandResult(*remover, game::guild_command::Uninvite, game::guild_command_result::PlayerNotFound, targetName);
			return;
		}

		// Only members of a lower rank (higher rank index) can be removed, which also protects the leader
		if (guild->GetMemberRank(targetGuid) <= guild->GetMemberRank(removerGuid))
		{
			SendGuildCommandResult(*remover, game::guild_command::Uninvite, game::guild_command_result::NotAllowed, "");
			return;
		}

		if (!guild->RemoveMember(targetGuid))
		{
			ELOG("Failed to remove player " << targetName << " from guild");
			return;
		}

		guild->BroadcastEvent(guild_event::Removed, targetGuid, targetName.c_str());

		if (Player* targetPlayer = m_playerManager.GetPlayerByCharacterGuid(targetGuid))
		{
			targetPlayer->GuildChange(0);
			targetPlayer->GetConnection().sendSinglePacket([&targetName](game::OutgoingPacket& outPacket)
			{
				outPacket.Start(game::realm_client_packet::GuildUninvite);
				outPacket << io::write_dynamic_range<uint8>(targetName);
				outPacket.Finish();
			});
		}
	}

	PacketParseResult GuildModule::OnGuildRankChange(Player& player, game::IncomingPacket& packet, const game::guild_command::Type command)
	{
		String playerName;
		if (!(packet >> io::read_container<uint8>(playerName)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to " << (command == game::guild_command::Promote ? "promote " : "demote ") << playerName << " in guild");

		Guild* guild = GetPlayerGuild(player, command);
		if (!guild)
		{
			return PacketParseResult::Pass;
		}

		// One permission covers both directions
		if (!guild->HasPermission(player.GetCharacterGuid(), guild_rank_permissions::Promote))
		{
			SendGuildCommandResult(player, command, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		const uint64 actorGuid = player.GetCharacterGuid();
		auto handler = [this, actorGuid, playerName, command](const std::optional<DatabaseId>& characterId)
		{
			if (!characterId)
			{
				if (Player* actor = m_playerManager.GetPlayerByCharacterGuid(actorGuid))
				{
					SendGuildCommandResult(*actor, command, game::guild_command_result::PlayerNotFound, playerName);
				}
				return;
			}

			ChangeMemberRank(actorGuid, *characterId, playerName, command);
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterIdByName, playerName);
		return PacketParseResult::Pass;
	}

	void GuildModule::ChangeMemberRank(const uint64 actorGuid, const uint64 targetGuid, const String& targetName, const game::guild_command::Type command)
	{
		Player* actor = m_playerManager.GetPlayerByCharacterGuid(actorGuid);
		if (!actor)
		{
			return;
		}

		Guild* guild = GetPlayerGuild(*actor, command);
		if (!guild)
		{
			return;
		}

		if (!guild->IsMember(targetGuid))
		{
			SendGuildCommandResult(*actor, command, game::guild_command_result::PlayerNotFound, targetName);
			return;
		}

		// Rank 0 is the leader; a higher index is a lower rank
		const uint32 targetRank = guild->GetMemberRank(targetGuid);
		const uint32 ourRank = guild->GetMemberRank(actorGuid);

		if (command == game::guild_command::Promote)
		{
			// Nobody can promote someone to their own rank or above: the leader can never create a
			// second leader, and an officer (1) can raise an initiate (4) to veteran (2) but not to
			// officer.
			if (targetRank == 0 || targetRank <= ourRank + 1)
			{
				SendGuildCommandResult(*actor, command, game::guild_command_result::NotAllowed, "");
				return;
			}

			if (!guild->PromoteMember(targetGuid, actor->GetCharacterName(), targetName))
			{
				ELOG("Failed to promote player " << targetName << " in guild");
			}

			return;
		}

		if (targetRank >= guild->GetLowestRank())
		{
			SendGuildCommandResult(*actor, command, game::guild_command_result::NotAllowed, targetName);
			return;
		}

		if (targetRank <= ourRank)
		{
			SendGuildCommandResult(*actor, command, game::guild_command_result::NotAllowed, "");
			return;
		}

		if (!guild->DemoteMember(targetGuid, actor->GetCharacterName(), targetName))
		{
			ELOG("Failed to demote player " << targetName << " in guild");
		}
	}

	PacketParseResult GuildModule::OnGuildLeave(Player& player, game::IncomingPacket& /*packet*/)
	{
		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Disconnect;
		}

		Guild* guild = GetPlayerGuild(player, game::guild_command::Leave);
		if (!guild)
		{
			return PacketParseResult::Pass;
		}

		// The leader leaving takes the guild with them
		if (guild->GetLeaderGuid() == player.GetCharacterGuid())
		{
			if (!m_guildMgr.DisbandGuild(guild->GetId()))
			{
				ELOG("Failed to disband guild");
			}

			return PacketParseResult::Pass;
		}

		if (!guild->RemoveMember(player.GetCharacterGuid()))
		{
			ELOG("Failed to remove player from guild");
			return PacketParseResult::Pass;
		}

		guild->BroadcastEvent(guild_event::Left, player.GetCharacterGuid(), player.GetCharacterName().c_str());

		SendGuildCommandResult(player, game::guild_command::Leave, game::guild_command_result::Ok, "");
		player.GuildChange(0);

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildModule::OnGuildDisband(Player& player, game::IncomingPacket& /*packet*/)
	{
		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Disconnect;
		}

		Guild* guild = GetPlayerGuild(player, game::guild_command::Disband);
		if (!guild)
		{
			return PacketParseResult::Pass;
		}

		if (guild->GetLeaderGuid() != player.GetCharacterGuid())
		{
			SendGuildCommandResult(player, game::guild_command::Disband, game::guild_command_result::NotAllowed, "");
			return PacketParseResult::Pass;
		}

		if (!m_guildMgr.DisbandGuild(guild->GetId()))
		{
			ELOG("Failed to disband guild");
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildModule::OnGuildMotd(Player& player, game::IncomingPacket& packet)
	{
		String motd;
		if (!(packet >> io::read_container<uint8>(motd)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Disconnect;
		}

		Guild* guild = GetPlayerGuild(player);
		if (!guild)
		{
			return PacketParseResult::Pass;
		}

		if (!guild->HasPermission(player.GetCharacterGuid(), guild_rank_permissions::SetMotd))
		{
			WLOG("Player tried to set guild MOTD without having permission to set MOTD");
			return PacketParseResult::Pass;
		}

		guild->SetMotd(motd);
		guild->BroadcastEvent(guild_event::Motd, 0, motd.c_str());

		const uint64 guildId = guild->GetId();
		m_database.asyncRequestKeyed(guildId, [](const bool success)
		{
			if (!success)
			{
				ELOG("Failed to persist guild MOTD");
			}
		}, &IDatabase::SetGuildMotd, guildId, motd);

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildModule::OnGuildAccept(Player& player, game::IncomingPacket& /*packet*/)
	{
		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Disconnect;
		}

		// A missing invite is not an error: a second click on "accept" arrives after the first one
		// already used it up.
		const auto it = m_pendingInvites.find(player.GetCharacterGuid());
		if (it == m_pendingInvites.end())
		{
			DLOG("Player accepted a guild invite without having a pending invite");
			return PacketParseResult::Pass;
		}

		const uint64 guildId = it->second.guildId;
		m_pendingInvites.erase(it);

		if (player.GetCharacterData().guildId != 0)
		{
			WLOG("Player accepted a guild invite while already being in a guild");
			return PacketParseResult::Pass;
		}

		Guild* guild = m_guildMgr.GetGuild(guildId);
		if (!guild)
		{
			// Disbanded since the invite went out
			return PacketParseResult::Pass;
		}

		if (!guild->AddMember(player.GetCharacterGuid(), guild->GetLowestRank()))
		{
			ELOG("Failed to add player to guild");
			return PacketParseResult::Pass;
		}

		player.GuildChange(guild->GetId());
		guild->BroadcastEvent(guild_event::Joined, 0, player.GetCharacterName().c_str());

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildModule::OnGuildDecline(Player& player, game::IncomingPacket& /*packet*/)
	{
		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Disconnect;
		}

		const auto it = m_pendingInvites.find(player.GetCharacterGuid());
		if (it == m_pendingInvites.end())
		{
			DLOG("Player declined a guild invite without having a pending invite");
			return PacketParseResult::Pass;
		}

		const uint64 inviterGuid = it->second.inviterGuid;
		m_pendingInvites.erase(it);

		if (Player* inviter = m_playerManager.GetPlayerByCharacterGuid(inviterGuid))
		{
			const String& declinerName = player.GetCharacterName();
			inviter->SendPacket([&declinerName](game::OutgoingPacket& outPacket)
			{
				outPacket.Start(game::realm_client_packet::GuildDecline);
				outPacket << io::write_dynamic_range<uint8>(declinerName);
				outPacket.Finish();
			});
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildModule::OnGuildMemberPositions(Player& player, game::IncomingPacket& /*packet*/)
	{
		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Pass;
		}

		// The client polls every few seconds; anything faster is dropped instead of answered.
		const GameTime now = GetAsyncTimeMs();
		GameTime& lastRequest = m_lastPositionRequest[player.GetCharacterGuid()];
		if (lastRequest != 0 && now < lastRequest + constants::OneSecond)
		{
			return PacketParseResult::Pass;
		}
		lastRequest = now;

		Guild* guild = GetPlayerGuild(player);
		if (!guild)
		{
			return PacketParseResult::Pass;
		}

		const CharacterData& self = player.GetCharacterData();
		const InstanceId& instanceId = player.GetWorldInstanceId();

		struct MemberPosition
		{
			uint64 guid;
			const String* name;
			Vector3 position;
		};

		std::vector<MemberPosition> positions;
		for (const GuildMember& member : guild->GetMembers())
		{
			if (member.guid == player.GetCharacterGuid())
			{
				continue;
			}

			// Only mates in the same world instance: someone in another copy of a dungeon must not
			// show up on this player's map of it.
			const Player* mate = m_playerManager.GetPlayerByCharacterGuid(member.guid);
			if (!mate || !mate->HasCharacterGuid() || mate->GetWorldInstanceId() != instanceId)
			{
				continue;
			}

			const CharacterData& data = mate->GetCharacterData();
			if (data.mapId != self.mapId)
			{
				continue;
			}

			positions.push_back({ member.guid, &member.name, data.position });
			if (positions.size() >= std::numeric_limits<uint16>::max())
			{
				break;
			}
		}

		player.SendPacket([&positions](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::GuildMemberPositions);
			outPacket << io::write<uint16>(positions.size());
			for (const MemberPosition& entry : positions)
			{
				outPacket
					<< io::write<uint64>(entry.guid)
					<< io::write_dynamic_range<uint8>(*entry.name)
					<< io::write<float>(entry.position.x)
					<< io::write<float>(entry.position.z);
			}
			outPacket.Finish();
		});

		return PacketParseResult::Pass;
	}

	PacketParseResult GuildModule::OnGuildRoster(Player& player, game::IncomingPacket& /*packet*/)
	{
		Guild* guild = player.HasCharacterGuid() ? GetPlayerGuild(player) : nullptr;
		if (!guild)
		{
			return PacketParseResult::Pass;
		}

		player.SendPacket([guild](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::GuildRoster);
			guild->WriteRoster(outPacket);
			outPacket.Finish();
		});

		return PacketParseResult::Pass;
	}

#if MMO_WITH_DEV_COMMANDS
	PacketParseResult GuildModule::OnGuildCreate(Player& player, game::IncomingPacket& packet)
	{
		String guildName;
		if (!(packet >> io::read_container<uint8>(guildName)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Player wants to create a guild named '" << guildName << "'...");

		if (player.GetCharacterData().guildId != 0)
		{
			WLOG("Player tried to create a guild while already being in a guild");
			return PacketParseResult::Pass;
		}

		if (m_guildMgr.GetGuildIdByName(guildName) != 0)
		{
			WLOG("Guild with name '" << guildName << "' already exists");
			return PacketParseResult::Pass;
		}

		const uint64 leaderGuid = player.GetCharacterGuid();
		auto onCreated = [this, leaderGuid](Guild* guild)
		{
			if (!guild)
			{
				ELOG("Guild creation failed");
				return;
			}

			ILOG("Successfully created guild " << guild->GetName());
			m_pendingInvites.erase(leaderGuid);

			if (Player* leader = m_playerManager.GetPlayerByCharacterGuid(leaderGuid))
			{
				leader->GuildChange(guild->GetId());
			}
		};

		if (!m_guildMgr.CreateGuild(guildName, leaderGuid, { leaderGuid }, std::move(onCreated)))
		{
			ELOG("Failed to create guild");
		}

		return PacketParseResult::Pass;
	}
#endif

	Guild* GuildModule::GetPlayerGuild(Player& player, const std::optional<game::guild_command::Type> command) const
	{
		Guild* guild = nullptr;
		if (player.HasCharacterGuid())
		{
			if (const uint64 guildId = player.GetCharacterData().guildId; guildId != 0)
			{
				guild = m_guildMgr.GetGuild(guildId);
			}
		}

		if (!guild && command)
		{
			SendGuildCommandResult(player, *command, game::guild_command_result::NotInGuild, "");
		}

		return guild;
	}

	void SendGuildCommandResult(Player& player, const uint8 command, const uint8 result, const String& playerName)
	{
		player.SendPacket([command, result, &playerName](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::GuildCommandResult);
			packet
				<< io::write<uint8>(command)
				<< io::write<uint8>(result)
				<< io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
		});
	}
}
