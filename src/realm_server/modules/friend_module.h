// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "realm_module.h"

namespace mmo
{
	class AsyncDatabase;
	class FriendMgr;
	class PlayerManager;

	/// The friend list: one-sided friendships (adding someone needs no acceptance), online/offline
	/// notifications and level updates for everyone who befriended a character.
	///
	/// The friend lists themselves are cached by FriendMgr, keyed by character guid; this module
	/// holds no per-player state. Asynchronous callbacks look players up by guid again rather than
	/// keeping a Player pointer, since the player may have logged out by the time they run.
	///
	/// While the FRIENDS subsystem is off, every friend request is answered with
	/// friend_command_result::Disabled and no lists or online notifications are sent; friend data
	/// of deleted characters is still cleaned up.
	class FriendModule final : public RealmModule
	{
	public:
		explicit FriendModule(FriendMgr& friendMgr, PlayerManager& playerManager, AsyncDatabase& database);

	public:
		[[nodiscard]] const char* GetName() const override { return "Friends"; }

		[[nodiscard]] std::optional<game::Subsystem> GetSubsystem() const override { return game::subsystem::Friends; }

		void RegisterPacketHandlers(Player& player, Player::PacketHandlerHandleContainer& handlers) override;

		void OnEnteredWorld(Player& player) override;

		void OnLeftWorld(Player& player) override;

		void OnLevelChanged(Player& player, uint32 previousLevel) override;

		void OnCharacterDeleting(Player& player, uint64 characterGuid) override;

	private:
		PacketParseResult OnFriendInvite(Player& player, game::IncomingPacket& packet);

		PacketParseResult OnFriendRemove(Player& player, game::IncomingPacket& packet);

		PacketParseResult OnFriendListRequest(Player& player, game::IncomingPacket& packet);

		/// Adds the character with the given guid to the player's friend list, after the checks
		/// that need the target's guid (self, already friends, list full).
		void AddFriend(Player& player, uint64 targetGuid, const String& targetName);

		/// Reloads a character's friend list from the database and sends it to the character's
		/// client, if they are online and the module is available to them.
		void ReloadFriendList(uint64 characterGuid);

		/// Sends the character's cached friend list to the player's client.
		void SendFriendList(Player& player);

	private:
		FriendMgr& m_friendMgr;
		PlayerManager& m_playerManager;
		AsyncDatabase& m_database;
	};

	/// Sends a friend command result to the player's client.
	void SendFriendCommandResult(Player& player, uint8 result, const String& playerName);
}
