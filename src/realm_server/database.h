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

	// -------------------------------------------------------------------------
	// Focused sub-interfaces (Interface Segregation Principle)
	// -------------------------------------------------------------------------

	/// Character CRUD and persistence operations.
	struct ICharacterDatabase
	{
		virtual ~ICharacterDatabase() = default;

		/// Gets the list of character views for an account.
		virtual std::optional<std::vector<CharacterView>> GetCharacterViewsByAccountId(uint64 accountId) = 0;

		/// Deletes a character by GUID.
		virtual void DeleteCharacter(uint64 characterGuid) = 0;

		/// Creates a new character.
		virtual std::optional<CharCreateResult> CreateCharacter(std::string characterName, uint64 accountId, uint32 map, uint32 level, uint32 hp, uint32 gender, uint32 race, uint32 characterClass, const Vector3& position, const Degree& orientation, std::vector<uint32> spellIds, uint32 mana, uint32 rage, uint32 energy, std::map<uint8, ActionButton> actionButtons, const AvatarConfiguration& configuration,
			const std::vector<ItemData>& items) = 0;

		/// Loads character data when entering the world.
		virtual std::optional<CharacterData> CharacterEnterWorld(uint64 characterId, uint64 accountId) = 0;

		/// Persists character state (position, level, etc.).
		virtual void UpdateCharacter(uint64 characterId, uint32 map, const Vector3& position, const Radian& orientation, uint32 level, uint32 xp, uint32 hp, uint32 mana, uint32 rage, uint32 energy, uint32 money, uint32 bindMap, const Vector3& bindPosition, const Radian& bindFacing, std::array<uint32, 5> attributePointsSpent, const std::vector<uint32>& spellIds, const std::unordered_map<uint32, uint32>& talentRanks, uint32 timePlayed) = 0;

		/// Replaces the persisted auras of a character. Expiration is stored as remaining duration
		/// (milliseconds), so offline time does not consume the aura.
		virtual void UpdateCharacterAuras(uint64 characterId, const std::vector<PersistentAuraData>& auras) = 0;

		/// Replaces the persisted spell cooldowns of a character. Each entry is the spell id mapped
		/// to the wall-clock end timestamp (unix seconds) at which the cooldown elapses.
		virtual void UpdateCharacterCooldowns(uint64 characterId, const std::vector<std::pair<uint32, GameTime>>& cooldownEnds) = 0;

		/// Loads the action buttons for a character.
		virtual std::optional<ActionButtons> GetActionButtons(uint64 characterId) = 0;

		/// Persists action buttons for a character.
		virtual void SetCharacterActionButtons(DatabaseId characterId, ActionButtons buttons) = 0;

		/// Records a learned spell for a character.
		virtual void LearnSpell(DatabaseId characterId, uint32 spellId) = 0;

		/// Persists quest status for a character.
		virtual void SetQuestData(DatabaseId characterId, uint32 questId, const QuestStatusData& data) = 0;

		/// Looks up character location by name.
		virtual std::optional<CharacterLocationData> GetCharacterLocationDataByName(String characterName) = 0;

		/// Looks up character id by name.
		virtual std::optional<DatabaseId> GetCharacterIdByName(String characterName) = 0;

		/// Teleports a character by name.
		virtual void TeleportCharacterByName(String characterName, uint32 map, Vector3 position, Radian orientation) = 0;

		/// Gets a character name by GUID.
		virtual std::optional<String> GetCharacterNameById(uint64 characterId) = 0;

		/// Saves inventory items for a character.
		virtual void SaveInventoryItems(uint64 characterId, const std::vector<ItemData>& items) = 0;

		/// Deletes inventory items for a character by slot indices.
		virtual void DeleteInventoryItems(uint64 characterId, const std::vector<uint16>& slots) = 0;
	};

	/// World node authentication and registration operations.
	struct IWorldNodeDatabase
	{
		virtual ~IWorldNodeDatabase() = default;

		/// Obtains world auth data by name.
		virtual std::optional<WorldAuthData> GetWorldAuthData(std::string name) = 0;

		/// Records a successful world node login.
		virtual void WorldLogin(uint64 worldId, const std::string& sessionKey, const std::string& ip, const std::string& build) = 0;

		/// Creates a new world node entry with SRP credentials.
		virtual std::optional<WorldCreationResult> CreateWorld(const String& name, const String& s, const String& v) = 0;
	};

	/// Group management operations.
	struct IGroupDatabase
	{
		virtual ~IGroupDatabase() = default;

		virtual void CreateGroup(uint64 id, uint64 leaderGuid, uint8 lootMethod, uint8 lootThreshold) = 0;
		virtual void SetGroupLootMethod(uint64 groupId, uint8 lootMethod, uint64 lootMaster, uint8 lootThreshold) = 0;
		virtual void SetGroupLeader(uint64 groupId, uint64 leaderGuid) = 0;
		virtual void AddGroupMember(uint64 groupId, uint64 memberGuid) = 0;
		virtual void RemoveGroupMember(uint64 groupId, uint64 memberGuid) = 0;
		virtual void DisbandGroup(uint64 groupId) = 0;
		virtual std::optional<std::vector<uint64>> ListGroups() = 0;
		virtual std::optional<GroupData> LoadGroup(uint64 groupId) = 0;
	};

	/// Guild management operations.
	struct IGuildDatabase
	{
		virtual ~IGuildDatabase() = default;

		virtual std::optional<std::vector<GuildData>> LoadGuilds() = 0;
		virtual void CreateGuild(uint64 id, String name, uint64 leaderGuid, const std::vector<GuildRank>& ranks, const std::vector<GuildMember>& member) = 0;
		virtual void AddGuildMember(uint64 guildId, uint64 memberGuid, uint32 rank) = 0;
		virtual void RemoveGuildMember(uint64 guildId, uint64 memberGuid) = 0;
		virtual void DisbandGuild(uint64 guildId) = 0;
		virtual void SetGuildMemberRank(uint64 guildId, uint64 memberGuid, uint32 rank) = 0;
		virtual void SetGuildMotd(uint64 guildId, const String& motd) = 0;
	};

	/// Friend list operations.
	struct IFriendDatabase
	{
		virtual ~IFriendDatabase() = default;

		virtual void AddFriend(uint64 characterId, uint64 friendId) = 0;
		virtual void RemoveFriend(uint64 characterId, uint64 friendId) = 0;
		virtual std::optional<std::vector<FriendData>> LoadFriendList(uint64 characterId) = 0;
		virtual std::vector<uint64> GetCharactersWithFriend(uint64 characterId) = 0;
		virtual bool AreFriends(uint64 characterId, uint64 friendId) = 0;
	};

	/// Message of the Day operations.
	struct IMOTDDatabase
	{
		virtual ~IMOTDDatabase() = default;

		virtual std::optional<String> GetMessageOfTheDay() = 0;
		virtual void SetMessageOfTheDay(const std::string& motd) = 0;
	};

	/// Chat logging operations.
	struct IChatDatabase
	{
		virtual ~IChatDatabase() = default;

		virtual void ChatMessage(uint64 characterId, uint16 type, String message) = 0;
	};

	// -------------------------------------------------------------------------
	// Composite interface — still used by MySQLDatabase and legacy call sites
	// that depend on a single database reference.
	// -------------------------------------------------------------------------

	/// Basic interface for a database system used by the realm server.
	/// Composes all focused sub-interfaces for backward compatibility.
	struct IDatabase
		: public NonCopyable
		, public ICharacterDatabase
		, public IWorldNodeDatabase
		, public IGroupDatabase
		, public IGuildDatabase
		, public IFriendDatabase
		, public IMOTDDatabase
		, public IChatDatabase
	{
		virtual ~IDatabase() override;
	};


	/// Async database wrapper for the realm server.
	/// Inherits from the shared AsyncDatabaseT template, bound to this server's IDatabase interface.
	class AsyncDatabase final : public AsyncDatabaseT<IDatabase>
	{
	public:
		using AsyncDatabaseT<IDatabase>::AsyncDatabaseT;
	};

	/// Narrow async wrappers — one per focused sub-interface.
	/// Use these in subsystem constructors to express the minimum DB surface needed.
	using AsyncGuildDatabase    = AsyncDatabaseT<IGuildDatabase>;
	using AsyncGroupDatabase    = AsyncDatabaseT<IGroupDatabase>;
	using AsyncFriendDatabase   = AsyncDatabaseT<IFriendDatabase>;
	using AsyncMOTDDatabase     = AsyncDatabaseT<IMOTDDatabase>;
	using AsyncCharacterDatabase = AsyncDatabaseT<ICharacterDatabase>;
	using AsyncWorldNodeDatabase = AsyncDatabaseT<IWorldNodeDatabase>;
	using AsyncChatDatabase     = AsyncDatabaseT<IChatDatabase>;

	typedef std::function<void()> Action;
}
