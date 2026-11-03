// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "friend_module.h"

#include "database.h"
#include "friend_mgr.h"
#include "player_manager.h"

#include "log/default_log_levels.h"

namespace mmo
{
	FriendModule::FriendModule(FriendMgr& friendMgr, PlayerManager& playerManager, AsyncDatabase& database)
		: m_friendMgr(friendMgr)
		, m_playerManager(playerManager)
		, m_database(database)
	{
	}

	void FriendModule::RegisterPacketHandlers(Player& player, Player::PacketHandlerHandleContainer& handlers)
	{
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::FriendInvite, [this, &player](game::IncomingPacket& packet)
		{
			return OnFriendInvite(player, packet);
		});
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::FriendRemove, [this, &player](game::IncomingPacket& packet)
		{
			return OnFriendRemove(player, packet);
		});
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::FriendListRequest, [this, &player](game::IncomingPacket& packet)
		{
			return OnFriendListRequest(player, packet);
		});

		// Friendships are one-sided and take effect immediately, so there is never an invite to
		// accept or decline. The opcodes stay on the wire; answering them quietly keeps an old
		// client's buttons from logging "unhandled packet" warnings.
		const auto ignore = [](game::IncomingPacket&) { return PacketParseResult::Pass; };
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::FriendAccept, ignore);
		handlers += player.RegisterAutoPacketHandler(game::client_realm_packet::FriendDecline, ignore);
	}

	void FriendModule::OnEnteredWorld(Player& player)
	{
		// The cached list is loaded even while the module is off: level updates and the
		// already-friends check work on it, and it is ready when the module comes back.
		ReloadFriendList(player.GetCharacterGuid());

		if (IsAvailableTo(player))
		{
			m_friendMgr.NotifyFriendStatusChange(player.GetCharacterGuid(), true);
		}
	}

	void FriendModule::OnLeftWorld(Player& player)
	{
		if (IsAvailableTo(player))
		{
			m_friendMgr.NotifyFriendStatusChange(player.GetCharacterGuid(), false);
		}
	}

	void FriendModule::OnLevelChanged(Player& player, uint32 /*previousLevel*/)
	{
		// Friend lists are database snapshots taken when a player logged in, so a level up only
		// reaches everyone who befriended this character if their cached entry is refreshed and
		// their list is sent again.
		const std::vector<uint64> changedCharacters = m_friendMgr.UpdateCachedFriendLevel(player.GetCharacterGuid(), player.GetCharacterLevel());

		for (const uint64 characterGuid : changedCharacters)
		{
			Player* friendOfMine = m_playerManager.GetPlayerByCharacterGuid(characterGuid);
			if (friendOfMine && IsAvailableTo(*friendOfMine))
			{
				SendFriendList(*friendOfMine);
			}
		}
	}

	void FriendModule::OnCharacterDeleting(Player& /*player*/, const uint64 characterGuid)
	{
		// Remove the character from every friend list it is on
		auto handler = [this, characterGuid](const std::vector<uint64>& friendIds)
		{
			if (friendIds.empty())
			{
				return;
			}

			DLOG("Removing character " << characterGuid << " from " << friendIds.size() << " friend lists");

			for (const uint64 friendId : friendIds)
			{
				m_database.asyncRequestKeyed<void>(characterGuid, [characterGuid, friendId](auto&& database)
					{
						database->RemoveFriend(friendId, characterGuid);
					},
					[characterGuid, friendId](const bool success)
					{
						if (!success)
						{
							ELOG("Failed to remove friendship between " << friendId << " and deleted character " << characterGuid);
						}
					});

				if (m_playerManager.GetPlayerByCharacterGuid(friendId))
				{
					ReloadFriendList(friendId);
				}
			}
		};

		m_database.asyncRequestKeyed(characterGuid, std::move(handler), &IDatabase::GetCharactersWithFriend, characterGuid);
	}

	PacketParseResult FriendModule::OnFriendInvite(Player& player, game::IncomingPacket& packet)
	{
		String targetName;
		if (!(packet >> io::read_container<uint8>(targetName)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Pass;
		}

		if (!IsAvailableTo(player))
		{
			SendFriendCommandResult(player, game::friend_command_result::Disabled, targetName);
			return PacketParseResult::Pass;
		}

		if (const Player* targetPlayer = m_playerManager.GetPlayerByCharacterName(targetName))
		{
			AddFriend(player, targetPlayer->GetCharacterGuid(), targetName);
			return PacketParseResult::Pass;
		}

		// The target is offline: look the name up in the database
		const uint64 myGuid = player.GetCharacterGuid();
		auto handler = [this, myGuid, targetName](const std::optional<DatabaseId> targetGuid)
		{
			Player* me = m_playerManager.GetPlayerByCharacterGuid(myGuid);
			if (!me)
			{
				return;
			}

			if (!targetGuid)
			{
				SendFriendCommandResult(*me, game::friend_command_result::PlayerNotFound, targetName);
				return;
			}

			AddFriend(*me, *targetGuid, targetName);
		};

		m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterIdByName, targetName);
		return PacketParseResult::Pass;
	}

	void FriendModule::AddFriend(Player& player, const uint64 targetGuid, const String& targetName)
	{
		const uint64 myGuid = player.GetCharacterGuid();

		if (targetGuid == myGuid)
		{
			SendFriendCommandResult(player, game::friend_command_result::CannotAddSelf, targetName);
			return;
		}

		if (m_friendMgr.AreFriends(myGuid, targetGuid))
		{
			SendFriendCommandResult(player, game::friend_command_result::AlreadyFriends, targetName);
			return;
		}

		if (!m_friendMgr.CanAddFriend(myGuid))
		{
			SendFriendCommandResult(player, game::friend_command_result::FriendListFull, targetName);
			return;
		}

		// One-sided friendship: directly add the target to the player's list
		auto handler = [this, myGuid, targetGuid, targetName](const bool success)
		{
			if (!success)
			{
				ELOG("Failed to add friendship between " << myGuid << " and " << targetGuid);
				return;
			}

			ILOG("Friendship established (one-sided) between " << myGuid << " and " << targetGuid);
			ReloadFriendList(myGuid);

			if (Player* me = m_playerManager.GetPlayerByCharacterGuid(myGuid))
			{
				SendFriendCommandResult(*me, game::friend_command_result::Success, targetName);
			}
		};

		m_database.asyncRequestKeyed(myGuid, std::move(handler), &IDatabase::AddFriend, myGuid, targetGuid);
	}

	PacketParseResult FriendModule::OnFriendRemove(Player& player, game::IncomingPacket& packet)
	{
		String friendName;
		if (!(packet >> io::read_container<uint8>(friendName)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Pass;
		}

		if (!IsAvailableTo(player))
		{
			SendFriendCommandResult(player, game::friend_command_result::Disabled, friendName);
			return PacketParseResult::Pass;
		}

		const uint64 myGuid = player.GetCharacterGuid();

		uint64 friendGuid = 0;
		for (const auto& friendData : m_friendMgr.GetCachedFriends(myGuid))
		{
			if (friendData.name == friendName)
			{
				friendGuid = friendData.guid;
				break;
			}
		}

		if (friendGuid == 0)
		{
			SendFriendCommandResult(player, game::friend_command_result::NotFriends, friendName);
			return PacketParseResult::Pass;
		}

		auto handler = [this, myGuid, friendGuid, friendName](const bool success)
		{
			if (!success)
			{
				ELOG("Failed to remove friendship between " << myGuid << " and " << friendGuid);
				return;
			}

			ILOG("Friendship removed between " << myGuid << " and " << friendGuid);
			ReloadFriendList(myGuid);

			if (Player* me = m_playerManager.GetPlayerByCharacterGuid(myGuid))
			{
				SendFriendCommandResult(*me, game::friend_command_result::Success, friendName);
			}
		};

		m_database.asyncRequestKeyed(myGuid, std::move(handler), &IDatabase::RemoveFriend, myGuid, friendGuid);
		return PacketParseResult::Pass;
	}

	PacketParseResult FriendModule::OnFriendListRequest(Player& player, game::IncomingPacket& /*packet*/)
	{
		if (!player.HasCharacterGuid())
		{
			return PacketParseResult::Pass;
		}

		if (!IsAvailableTo(player))
		{
			SendFriendCommandResult(player, game::friend_command_result::Disabled, String());
			return PacketParseResult::Pass;
		}

		SendFriendList(player);
		return PacketParseResult::Pass;
	}

	void FriendModule::ReloadFriendList(const uint64 characterGuid)
	{
		m_friendMgr.LoadCharacterFriends(characterGuid, [this, characterGuid](const std::vector<FriendData>&)
		{
			Player* player = m_playerManager.GetPlayerByCharacterGuid(characterGuid);
			if (player && IsAvailableTo(*player))
			{
				SendFriendList(*player);
			}
		});
	}

	void FriendModule::SendFriendList(Player& player)
	{
		const std::vector<FriendData>& friends = m_friendMgr.GetCachedFriends(player.GetCharacterGuid());

		player.SendPacket([this, &friends](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::FriendListUpdate);
			packet << io::write<uint16>(static_cast<uint16>(friends.size()));

			for (const auto& friendData : friends)
			{
				packet << io::write<uint64>(friendData.guid);
				packet << io::write_dynamic_range<uint8>(friendData.name);
				packet << io::write<uint32>(friendData.level);
				packet << io::write<uint32>(friendData.raceId);
				packet << io::write<uint32>(friendData.classId);

				const bool online = m_playerManager.GetPlayerByCharacterGuid(friendData.guid) != nullptr;
				packet << io::write<uint8>(online ? 1 : 0);
			}

			packet.Finish();
		});
	}

	void SendFriendCommandResult(Player& player, const uint8 result, const String& playerName)
	{
		player.SendPacket([result, &playerName](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::FriendCommandResult);
			packet << io::write<uint8>(result);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
		});
	}
}
