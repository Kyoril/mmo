// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "realm_module.h"

#include <map>

namespace mmo
{
	class AsyncDatabase;
	class Guild;
	class GuildMgr;
	class PlayerManager;

	/// Guilds: invites, removal, promotion and demotion, leaving and disbanding, the message of the
	/// day, the roster, guild and officer chat, and the online/offline events members see.
	///
	/// Guilds and their rosters are owned by GuildMgr; the only per-player state here is the pending
	/// invite, keyed by the invited character's guid. Asynchronous callbacks look players up by guid
	/// again rather than keeping a Player pointer, since the player may have logged out (or switched
	/// characters) by the time they run.
	///
	/// While the GUILD subsystem is off, guild requests and guild chat are answered with
	/// guild_command_result::Disabled and no online/offline events are sent. Guild queries (names
	/// above heads) keep working, and deleted characters still leave their guild.
	class GuildModule final : public RealmModule
	{
	public:
		explicit GuildModule(GuildMgr& guildMgr, PlayerManager& playerManager, AsyncDatabase& database);

	public:
		[[nodiscard]] const char* GetName() const override { return "Guild"; }

		[[nodiscard]] std::optional<game::Subsystem> GetSubsystem() const override { return game::subsystem::Guild; }

		void RegisterPacketHandlers(Player& player, Player::PacketHandlerHandleContainer& handlers) override;

		void OnEnteredWorld(Player& player) override;

		void OnLeftWorld(Player& player) override;

		void OnCharacterDeleting(Player& player, uint64 characterGuid) override;

		bool OnChatMessage(Player& player, ChatType type, const String& message) override;

	private:
		/// An invite waiting for the invited character to accept or decline it.
		struct PendingInvite
		{
			uint64 guildId = 0;
			uint64 inviterGuid = 0;
		};

	private:
		PacketParseResult OnGuildInvite(Player& player, game::IncomingPacket& packet);

		PacketParseResult OnGuildRemove(Player& player, game::IncomingPacket& packet);

		/// Handles both promotion and demotion; @p command is guild_command::Promote or Demote.
		PacketParseResult OnGuildRankChange(Player& player, game::IncomingPacket& packet, game::guild_command::Type command);

		PacketParseResult OnGuildLeave(Player& player, game::IncomingPacket& packet);

		PacketParseResult OnGuildDisband(Player& player, game::IncomingPacket& packet);

		PacketParseResult OnGuildMotd(Player& player, game::IncomingPacket& packet);

		PacketParseResult OnGuildAccept(Player& player, game::IncomingPacket& packet);

		PacketParseResult OnGuildDecline(Player& player, game::IncomingPacket& packet);

		PacketParseResult OnGuildRoster(Player& player, game::IncomingPacket& packet);

		/// Sends the player the last known positions of its online guild mates in its world instance.
		PacketParseResult OnGuildMemberPositions(Player& player, game::IncomingPacket& packet);

#if MMO_WITH_DEV_COMMANDS
		PacketParseResult OnGuildCreate(Player& player, game::IncomingPacket& packet);
#endif

		/// Returns the guild the player's character is in, or nullptr if it is in none (or its
		/// guild is missing). If @p command is given, the player is told NotInGuild in that case.
		Guild* GetPlayerGuild(Player& player, std::optional<game::guild_command::Type> command = std::nullopt) const;

		/// Removes the character with the given guid from the remover's guild, after its name has
		/// been resolved. The remover is looked up again, as they may have logged out meanwhile.
		void RemoveMember(uint64 removerGuid, uint64 targetGuid, const String& targetName);

		/// Promotes or demotes the given member of the actor's guild, after its name has been
		/// resolved. The actor is looked up again, as they may have logged out meanwhile.
		void ChangeMemberRank(uint64 actorGuid, uint64 targetGuid, const String& targetName, game::guild_command::Type command);

	private:
		GuildMgr& m_guildMgr;
		PlayerManager& m_playerManager;
		AsyncDatabase& m_database;

		/// Pending invites by invited character guid.
		std::map<uint64, PendingInvite> m_pendingInvites;

		/// Time of the last answered member position request, by character guid.
		std::map<uint64, GameTime> m_lastPositionRequest;
	};

	/// Sends a guild command result to the player's client.
	void SendGuildCommandResult(Player& player, uint8 command, uint8 result, const String& playerName);
}
