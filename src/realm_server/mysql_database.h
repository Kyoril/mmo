// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "database.h"
#include "mysql_wrapper/mysql_connection.h"
#include "base/countdown.h"


namespace mmo
{
	namespace proto
	{
		class Project;
	}

	/// MySQL implementation of the realm server database system.
	class MySQLDatabase final 
		: public IDatabase
	{
	public:
		/// Creates a MySQL database instance for the realm server.
		explicit MySQLDatabase(mysql::DatabaseInfo connectionInfo, const proto::Project& project, TimerQueue& timerQueue);

		/// Tries to establish a connection to the MySQL server.
		bool Load();

	private:
		/// Schedules the next keep-alive ping to the database.
		void SetNextPingTimer() const;

	public:
		/// @copydoc IDatabase::GetCharacterViewsByAccountId
		std::optional<std::vector<CharacterView>> GetCharacterViewsByAccountId(uint64 accountId) final override;

		/// @copydoc IDatabase::GetWorldAuthData
		std::optional<WorldAuthData> GetWorldAuthData(std::string name) final override;
		
		/// @copydoc IDatabase::WorldLogin
		void WorldLogin(uint64 worldId, const std::string& sessionKey, const std::string& ip, const std::string& build) final override;

		/// @copydoc IDatabase::LoadGuilds
		std::optional<std::vector<GuildData>> LoadGuilds() final override;

		/// @copydoc IDatabase::DeleteCharacter
		void DeleteCharacter(uint64 characterGuid) final override;
		
		/// @copydoc IDatabase::CreateCharacter
		std::optional<CharCreateResult> CreateCharacter(std::string characterName, uint64 accountId, uint32 map, uint32 level, uint32 hp, uint32 gender, uint32 race, uint32 characterClass, const Vector3& position, const Degree& orientation, std::vector<uint32> spellIds, uint32 mana, uint32 rage, uint32 energy, std::map<uint8, ActionButton> actionButtons, const AvatarConfiguration& configuration, const std::vector<ItemData>& items) final override;
		
		/// @copydoc IDatabase::CharacterEnterWorld
		std::optional<CharacterData> CharacterEnterWorld(uint64 characterId, uint64 accountId) override;

		/// @copydoc IDatabase::CreateWorld
		std::optional<WorldCreationResult> CreateWorld(const String& name, const String& s, const String& v) override;

		/// @copydoc IDatabase::ChatMessage
		void ChatMessage(uint64 characterId, uint16 type, String message) override;

		/// @copydoc IDatabase::UpdateCharacter
		void UpdateCharacter(uint64 characterId, uint32 map, const Vector3& position, const Radian& orientation, uint32 level, uint32 xp, uint32 hp, uint32 mana, uint32 rage, uint32 energy, uint32 money, uint32 bindMap, const Vector3& bindPosition, const Radian& bindFacing, std::array<uint32, 5> attributePointsSpent, const std::vector<uint32>& spellIds, const std::unordered_map<uint32, uint32>& talentRanks, uint32 timePlayed) override;

		/// @copydoc IDatabase::UpdateCharacterAuras
		void UpdateCharacterAuras(uint64 characterId, const std::vector<PersistentAuraData>& auras) override;

		/// @copydoc IDatabase::UpdateCharacterCooldowns
		void UpdateCharacterCooldowns(uint64 characterId, const std::vector<std::pair<uint32, GameTime>>& cooldownEnds) override;

		/// @copydoc IDatabase::GetActionButtons
		std::optional<ActionButtons> GetActionButtons(uint64 characterId) override;

		// Note: Copy of ActionButtons is intended here
		/// @copydoc IDatabase::SetCharacterActionButtons
		void SetCharacterActionButtons(DatabaseId characterId, ActionButtons buttons) override;

		/// @copydoc IDatabase::LearnSpell
		void LearnSpell(DatabaseId characterId, uint32 spellId) override;

		/// @copydoc IDatabase::SetQuestData
		void SetQuestData(DatabaseId characterId, uint32 questId, const QuestStatusData& data) override;

		/// @copydoc IDatabase::GetCharacterLocationDataByName
		std::optional<CharacterLocationData> GetCharacterLocationDataByName(String characterName) override;

		/// @copydoc IDatabase::GetCharacterIdByName
		std::optional<DatabaseId> GetCharacterIdByName(String characterName) override;

		/// @copydoc IDatabase::TeleportCharacterByName
		void TeleportCharacterByName(String characterName, uint32 map, Vector3 position, Radian orientation) override;

		/// @copydoc IDatabase::CreateGroup
		void CreateGroup(uint64 id, uint64 leaderGuid, uint8 lootMethod, uint8 lootThreshold) override;

		/// @copydoc IDatabase::SetGroupLootMethod
		void SetGroupLootMethod(uint64 groupId, uint8 lootMethod, uint64 lootMaster, uint8 lootThreshold) override;

		/// @copydoc IDatabase::SetGroupLeader
		void SetGroupLeader(uint64 groupId, uint64 leaderGuid) override;

		/// @copydoc IDatabase::AddGroupMember
		void AddGroupMember(uint64 groupId, uint64 memberGuid) override;

		/// @copydoc IDatabase::RemoveGroupMember
		void RemoveGroupMember(uint64 groupId, uint64 memberGuid) override;

		/// @copydoc IDatabase::DisbandGroup
		void DisbandGroup(uint64 groupId) override;

		/// @copydoc IDatabase::ListGroups
		std::optional<std::vector<uint64>> ListGroups() override;

		/// @copydoc IDatabase::LoadGroup
		std::optional<GroupData> LoadGroup(uint64 groupId) override;

		/// @copydoc IDatabase::GetCharacterNameById
		std::optional<String> GetCharacterNameById(uint64 characterId) override;

		/// @copydoc IDatabase::CreateGuild
		void CreateGuild(uint64 id, String name, uint64 leaderGuid, const std::vector<GuildRank>& ranks, const std::vector<GuildMember>& member) override;

		/// @copydoc IDatabase::AddGuildMember
		void AddGuildMember(uint64 guildId, uint64 memberGuid, uint32 rank) override;

		/// @copydoc IDatabase::RemoveGuildMember
		void RemoveGuildMember(uint64 guildId, uint64 memberGuid) override;

		/// @copydoc IDatabase::DisbandGuild
		void DisbandGuild(uint64 guildId) override;

		/// @copydoc IDatabase::SetGuildMemberRank
		void SetGuildMemberRank(uint64 guildId, uint64 memberGuid, uint32 rank) override;

		/// @copydoc IDatabase::SetGuildMotd
		void SetGuildMotd(uint64 guildId, const String& motd) override;

		/// @copydoc IDatabase::AddFriend
		void AddFriend(uint64 characterId, uint64 friendId) override;

		/// @copydoc IDatabase::RemoveFriend
		void RemoveFriend(uint64 characterId, uint64 friendId) override;

		/// @copydoc IDatabase::LoadFriendList
		std::optional<std::vector<FriendData>> LoadFriendList(uint64 characterId) override;

		/// @copydoc IDatabase::GetCharactersWithFriend
		std::vector<uint64> GetCharactersWithFriend(uint64 characterId) override;

		/// @copydoc IDatabase::AreFriends
		bool AreFriends(uint64 characterId, uint64 friendId) override;

		/// @copydoc IDatabase::GetMessageOfTheDay
		std::optional<String> GetMessageOfTheDay() override;

		/// @copydoc IDatabase::SetMessageOfTheDay
		void SetMessageOfTheDay(const std::string& motd) override;

		/// @copydoc IDatabase::SaveInventoryItems
		void SaveInventoryItems(uint64 characterId, const std::vector<ItemData>& items) override;

		/// @copydoc IDatabase::DeleteInventoryItems
		void DeleteInventoryItems(uint64 characterId, const std::vector<uint16>& slots) override;

	private:
		/// Logs the last database error to the default logger.
		void PrintDatabaseError();

	private:
		const proto::Project& m_project;
		mysql::DatabaseInfo m_connectionInfo;
		mysql::Connection m_connection;
		TimerQueue& m_timerQueue;
		Countdown m_pingCountdown;
		scoped_connection m_pingConnection;
	};
}
