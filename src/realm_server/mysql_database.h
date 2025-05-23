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

	/// MySQL implementation of the login server database system.
	class MySQLDatabase final 
		: public IDatabase
	{
	public:
		explicit MySQLDatabase(mysql::DatabaseInfo connectionInfo, const proto::Project& project, TimerQueue& timerQueue);

		/// Tries to establish a connection to the MySQL server.
		bool Load();

	private:
		void SetNextPingTimer() const;

	public:
		/// @copydoc IDatabase::GetCharacterViewsByAccountId
		std::optional<std::vector<CharacterView>> GetCharacterViewsByAccountId(uint64 accountId) final override;

		/// @copydoc IDatabase::GetWorldAuthData
		std::optional<WorldAuthData> GetWorldAuthData(std::string name) final override;
		
		/// @copydoc IDatabase::WorldLogin
		void WorldLogin(uint64 worldId, const std::string& sessionKey, const std::string& ip, const std::string& build) final override;

		std::optional<std::vector<GuildData>> LoadGuilds() final override;

		/// @copydoc IDatabase::DeleteCharacter
		void DeleteCharacter(uint64 characterGuid) final override;
		
		/// @copydoc IDatabase::CreateCharacter
		std::optional<CharCreateResult> CreateCharacter(std::string characterName, uint64 accountId, uint32 map, uint32 level, uint32 hp, uint32 gender, uint32 race, uint32 characterClass, const Vector3& position, const Degree& orientation, std::vector<uint32> spellIds, uint32 mana, uint32 rage, uint32 energy, std::map<uint8, ActionButton> actionButtons, const AvatarConfiguration& configuration) final override;
		
		/// @copydoc IDatabase::CharacterEnterWorld
		std::optional<CharacterData> CharacterEnterWorld(uint64 characterId, uint64 accountId) override;

		/// @copydoc IDatabase::CreateWorkd
		std::optional<WorldCreationResult> CreateWorkd(const String& name, const String& s, const String& v) override;

		void ChatMessage(uint64 characterId, uint16 type, String message) override;

		void UpdateCharacter(uint64 characterId, uint32 map, const Vector3& position, const Radian& orientation, uint32 level, uint32 xp, uint32 hp, uint32 mana, uint32 rage, uint32 energy, uint32 money, const std::vector<ItemData>& items, uint32 bindMap, const Vector3& bindPosition, const Radian& bindFacing, std::array<uint32, 5> attributePointsSpent, const std::vector<uint32>& spellIds) override;

		std::optional<ActionButtons> GetActionButtons(uint64 characterId) override;

		// Note: Copy of ActionButtons is intended here
		void SetCharacterActionButtons(DatabaseId characterId, ActionButtons buttons) override;

		void LearnSpell(DatabaseId characterId, uint32 spellId) override;

		void SetQuestData(DatabaseId characterId, uint32 questId, const QuestStatusData& data) override;

		std::optional<CharacterLocationData> GetCharacterLocationDataByName(String characterName) override;

		std::optional<DatabaseId> GetCharacterIdByName(String characterName) override;

		void TeleportCharacterByName(String characterName, uint32 map, Vector3 position, Radian orientation) override;

		void CreateGroup(uint64 id, uint64 leaderGuid) override;

		void SetGroupLeader(uint64 groupId, uint64 leaderGuid) override;

		void AddGroupMember(uint64 groupId, uint64 memberGuid) override;

		void RemoveGroupMember(uint64 groupId, uint64 memberGuid) override;

		void DisbandGroup(uint64 groupId) override;

		std::optional<std::vector<uint64>> ListGroups() override;

		std::optional<GroupData> LoadGroup(uint64 groupId) override;

		std::optional<String> GetCharacterNameById(uint64 characterId) override;

		void CreateGuild(uint64 id, String name, uint64 leaderGuid, const std::vector<GuildRank>& ranks, const std::vector<GuildMember>& member) override;

		void AddGuildMember(uint64 guildId, uint64 memberGuid, uint32 rank) override;

		void RemoveGuildMember(uint64 guildId, uint64 memberGuid) override;

		void DisbandGuild(uint64 guildId) override;

		void SetGuildMemberRank(uint64 guildId, uint64 memberGuid, uint32 rank) override;

		std::optional<String> GetMessageOfTheDay() override;

		void SetMessageOfTheDay(const std::string& motd) override;

	private:
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
