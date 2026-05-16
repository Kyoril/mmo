// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "base/async_database.h"
#include "log/log_exception.h"
#include "base/sha1.h"
#include "game/character_view.h"

#include <functional>
#include <exception>
#include <optional>
#include <vector>

#include "game/action_button.h"
#include "game_server/character_data.h"
#include "math/angle.h"


namespace mmo
{
	class AvatarConfiguration;
}

namespace mmo
{
	struct QuestStatusData;
	class Degree;
	class Vector3;

	/// Contains data used by a world for authentication.
	struct WorldAuthData final
	{
		/// The unique world id.
		uint64 id;
		/// Name of the world.
		std::string name;
		/// Password salt.
		std::string s;
		/// Password verifier.
		std::string v;
	};

	enum class WorldCreationResult : uint8
	{
		Success,

		WorldNameAlreadyInUse,
		InternalServerError
	};

	enum class CharCreateResult : uint8
	{
		Success,

		NameAlreadyInUse,
		Error
	};

	struct CharacterLocationData
	{
		DatabaseId characterId{0 };

		uint32 map{0};

		Vector3 position{};

		Radian facing{0.0f};
	};

	struct GroupMemberData
	{
		uint64 guid;

		String name;
	};

	/// Represents data of a player group.
	struct GroupData
	{
		/// Guid of the group leader.
		uint64 leaderGuid;

		String leaderName;

		/// Loot method for the group.
		uint8 lootMethod = 3;

		/// Loot quality threshold.
		uint8 lootThreshold = 2;

		/// Loot master character guid.
		uint64 lootMaster = 0;

		/// Vector of guids of all group members, including the leader.
		std::vector<GroupMemberData> members;
	};

	struct GuildRank
	{
		String name;

		uint32 permissions;
	};

	struct GuildMember
	{
		uint64 guid;

		uint32 rank;

		String name;

		uint32 level;

		uint32 raceId;

		uint32 classId;
	};

	struct GuildData
	{
		uint64 id;

		String name;

		uint64 leaderGuid;

		/// Message of the day; empty string if not yet set.
		String motd;

		std::vector<GuildRank> ranks;

		std::vector<GuildMember> members;
	};

	/// Represents data of a friend relationship.
	struct FriendData
	{
		uint64 guid;

		String name;

		uint32 level;

		uint32 raceId;

		uint32 classId;
	};

	/// Basic interface for a database system used by the login server.
	struct IDatabase : public NonCopyable
	{
		virtual ~IDatabase() override;

		/// Gets the list of characters that belong to a certain character id.
		/// @param accountId Id of the account.
		virtual std::optional<std::vector<CharacterView>> GetCharacterViewsByAccountId(uint64 accountId) = 0;

		/// Obtains world data by it's name.
		/// @param name Name of the world.
		virtual std::optional<WorldAuthData> GetWorldAuthData(std::string name) = 0;

		/// Handles a successful login request for a world by storing it's information into the database.
		///	@param worldId Id of the world.
		///	@param sessionKey The current world connection session key.
		///	@param ip The current public ip address of the world.
		///	@param build The build version of the world node.
		virtual void WorldLogin(uint64 worldId, const std::string& sessionKey, const std::string& ip, const std::string& build) = 0;

		/// Deletes a character with the given guid.
		///	@param characterGuid Unique id of the character to delete.
		virtual void DeleteCharacter(uint64 characterGuid) = 0;

		/// Loads all guilds from the database.
		virtual std::optional<std::vector<GuildData>> LoadGuilds() = 0;

		/// Creates a new character on the given account.
		///	@param characterName Name of the character. Has to be unique on the realm.
		///	@param accountId Id of the account the character belongs to.
		///	@param map The map id where the character is spawned.
		///	@param level Start level of the character.
		///	@param hp Current amount of health of the character.
		///	@param gender The characters gender.
		///	@param race The character race.
		///	@param position Position of the character on the map.
		///	@param orientation Facing of the character in the world.
		///	@param items Initial items of the character.
		virtual std::optional<CharCreateResult> CreateCharacter(std::string characterName, uint64 accountId, uint32 map, uint32 level, uint32 hp, uint32 gender, uint32 race, uint32 characterClass, const Vector3& position, const Degree& orientation, std::vector<uint32> spellIds, uint32 mana, uint32 rage, uint32 energy, std::map<uint8, ActionButton> actionButtons, const AvatarConfiguration& configuration,
			const std::vector<ItemData>& items) = 0;

		/// Loads character data of a character who wants to enter a world.
		///	@param characterId Unique id of the character to load.
		///	@param accountId Id of the player account to prevent players from logging in with another account's character.
		///	@returns Character data of the character, if the character exists.
		virtual std::optional<CharacterData> CharacterEnterWorld(uint64 characterId, uint64 accountId) = 0;
		
		/// Creates a new world entry with SRP credentials.
		virtual std::optional<WorldCreationResult> CreateWorkd(const String& name, const String& s, const String& v) = 0;

		/// Stores a chat message in the database.
		virtual void ChatMessage(uint64 characterId, uint16 type, String message) = 0;

		/// Updates character data in the database.
		/// @param characterId The character's database ID.
		/// @note Inventory is persisted separately via SaveInventoryItems() and DeleteInventoryItems().
		virtual void UpdateCharacter(uint64 characterId, uint32 map, const Vector3& position, const Radian& orientation, uint32 level, uint32 xp, uint32 hp, uint32 mana, uint32 rage, uint32 energy, uint32 money, uint32 bindMap, const Vector3& bindPosition, const Radian& bindFacing, std::array<uint32, 5> attributePointsSpent, const std::vector<uint32>& spellIds, const std::unordered_map<uint32, uint32>& talentRanks, uint32 timePlayed) = 0;

		/// Loads the action buttons for a character.
		virtual std::optional<ActionButtons> GetActionButtons(uint64 characterId) = 0;

		/// Sets the action buttons for a character.
		virtual void SetCharacterActionButtons(DatabaseId characterId, ActionButtons buttons) = 0;
		
		/// Adds a learned spell to a character.
		virtual void LearnSpell(DatabaseId characterId, uint32 spellId) = 0;

		/// Updates the quest status data for a character.
		virtual void SetQuestData(DatabaseId characterId, uint32 questId, const QuestStatusData& data) = 0;

		/// Gets character location data by character name.
		virtual std::optional<CharacterLocationData> GetCharacterLocationDataByName(String characterName) = 0;

		/// Gets character id by character name.
		virtual std::optional<DatabaseId> GetCharacterIdByName(String characterName) = 0;

		/// Teleports a character to the given map and position.
		virtual void TeleportCharacterByName(String characterName, uint32 map, Vector3 position, Radian orientation) = 0;

		/// Creates a new group with the given leader.
		virtual void CreateGroup(uint64 id, uint64 leaderGuid, uint8 lootMethod, uint8 lootThreshold) = 0;

		/// Updates the loot method settings for a group.
		virtual void SetGroupLootMethod(uint64 groupId, uint8 lootMethod, uint64 lootMaster, uint8 lootThreshold) = 0;

		/// Sets the leader of a group.
		virtual void SetGroupLeader(uint64 groupId, uint64 leaderGuid) = 0;

		/// Adds a member to a group.
		virtual void AddGroupMember(uint64 groupId, uint64 memberGuid) = 0;

		/// Removes a member from a group.
		virtual void RemoveGroupMember(uint64 groupId, uint64 memberGuid) = 0;

		/// Disbands a group.
		virtual void DisbandGroup(uint64 groupId) = 0;

		/// Lists all group ids.
		virtual std::optional<std::vector<uint64>> ListGroups() = 0;

		/// Loads group data by id.
		virtual std::optional<GroupData> LoadGroup(uint64 groupId) = 0;

		/// Gets a character name by id.
		virtual std::optional<String> GetCharacterNameById(uint64 characterId) = 0;

		/// Creates a new guild with the given members and ranks.
		virtual void CreateGuild(uint64 id, String name, uint64 leaderGuid, const std::vector<GuildRank>& ranks, const std::vector<GuildMember>& member) = 0;

		/// Adds a member to a guild.
		virtual void AddGuildMember(uint64 guildId, uint64 memberGuid, uint32 rank) = 0;

		/// Removes a member from a guild.
		virtual void RemoveGuildMember(uint64 guildId, uint64 memberGuid) = 0;

		/// Disbands a guild.
		virtual void DisbandGuild(uint64 guildId) = 0;

		/// Sets a guild member's rank.
		virtual void SetGuildMemberRank(uint64 guildId, uint64 memberGuid, uint32 rank) = 0;

		/// Sets the MOTD for a guild.
		/// @param guildId The guild's unique identifier.
		/// @param motd The new message of the day text.
		virtual void SetGuildMotd(uint64 guildId, const String& motd) = 0;

		/// Adds a friend relationship between two characters.
		/// @param characterId The character initiating the friendship.
		/// @param friendId The character being added as a friend.
		virtual void AddFriend(uint64 characterId, uint64 friendId) = 0;

		/// Removes a friend relationship between two characters.
		/// @param characterId The character removing the friend.
		/// @param friendId The friend being removed.
		virtual void RemoveFriend(uint64 characterId, uint64 friendId) = 0;

		/// Loads the friend list for a character.
		/// @param characterId The character whose friends to load.
		/// @returns A vector of FriendData containing friend information.
		virtual std::optional<std::vector<FriendData>> LoadFriendList(uint64 characterId) = 0;

		/// Gets all character IDs who have added a specific character as a friend.
		/// @param characterId The character to find admirers for.
		/// @returns A vector of character IDs who have this character in their friend list.
		virtual std::vector<uint64> GetCharactersWithFriend(uint64 characterId) = 0;

		/// Checks if two characters are friends.
		/// @param characterId First character id.
		/// @param friendId Second character id.
		/// @returns True if they are friends, false otherwise.
		virtual bool AreFriends(uint64 characterId, uint64 friendId) = 0;

		/// Gets the current Message of the Day.
		/// @returns The current MOTD string or default message if not set.
		virtual std::optional<String> GetMessageOfTheDay() = 0;

		/// Sets a new Message of the Day.
		/// @param motd The new Message of the Day text.
		virtual void SetMessageOfTheDay(const std::string& motd) = 0;

		/// Saves multiple inventory items for a character.
		/// @param characterId The character whose items to save.
		/// @param items Vector of items to save (will be inserted or updated).
		virtual void SaveInventoryItems(uint64 characterId, const std::vector<ItemData>& items) = 0;

		/// Deletes multiple inventory items for a character by slot indices.
		/// @param characterId The character whose items to delete.
		/// @param slots Vector of slot indices to delete.
		virtual void DeleteInventoryItems(uint64 characterId, const std::vector<uint16>& slots) = 0;
	};

	/// Async database wrapper for the realm server.
	/// Inherits from the shared AsyncDatabaseT template, bound to this server's IDatabase interface.
	class AsyncDatabase final : public AsyncDatabaseT<IDatabase>
	{
	public:
		using AsyncDatabaseT<IDatabase>::AsyncDatabaseT;
	};

	typedef std::function<void()> Action;
}
