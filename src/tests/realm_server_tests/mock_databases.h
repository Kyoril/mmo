// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
// Mock implementations of the narrow realm database sub-interfaces.
// Header-only — include in test translation units only.

#pragma once

#include "realm_server/database.h"
#include <optional>
#include <vector>
#include <string>
#include <map>

namespace mmo
{

// -----------------------------------------------------------------------
// MockGuildDatabase
// -----------------------------------------------------------------------

struct MockGuildDatabase : IGuildDatabase
{
    std::vector<GuildData> guildsToReturn;
    bool loadGuildsCalled = false;

    std::optional<uint64> lastCreateGuildId;
    std::string lastCreateGuildName;
    uint64 lastCreateGuildLeaderGuid = 0;

    std::vector<std::pair<uint64, uint64>> addedMembers; // (guildId, memberGuid)
    std::vector<std::pair<uint64, uint64>> removedMembers;
    std::optional<uint64> disbandedGuildId;
    std::optional<std::tuple<uint64, uint64, uint32>> lastRankChange; // (guild, member, rank)
    std::optional<std::pair<uint64, std::string>> lastMotd; // (guildId, motd)

    std::optional<std::vector<GuildData>> LoadGuilds() override
    {
        loadGuildsCalled = true;
        return guildsToReturn;
    }

    void CreateGuild(uint64 id, String name, uint64 leaderGuid,
        const std::vector<GuildRank>&, const std::vector<GuildMember>&) override
    {
        lastCreateGuildId = id;
        lastCreateGuildName = name;
        lastCreateGuildLeaderGuid = leaderGuid;
    }

    void AddGuildMember(uint64 guildId, uint64 memberGuid, uint32) override
    {
        addedMembers.emplace_back(guildId, memberGuid);
    }

    void RemoveGuildMember(uint64 guildId, uint64 memberGuid) override
    {
        removedMembers.emplace_back(guildId, memberGuid);
    }

    void DisbandGuild(uint64 guildId) override
    {
        disbandedGuildId = guildId;
    }

    void SetGuildMemberRank(uint64 guildId, uint64 memberGuid, uint32 rank) override
    {
        lastRankChange = { guildId, memberGuid, rank };
    }

    void SetGuildMotd(uint64 guildId, const String& motd) override
    {
        lastMotd = { guildId, motd };
    }
};

// -----------------------------------------------------------------------
// MockFriendDatabase
// -----------------------------------------------------------------------

struct MockFriendDatabase : IFriendDatabase
{
    std::vector<FriendData> friendListToReturn;
    std::vector<uint64> admirersToReturn;
    bool areFriendsResult = false;

    std::optional<std::pair<uint64, uint64>> lastAddFriend;
    std::optional<std::pair<uint64, uint64>> lastRemoveFriend;
    std::optional<uint64> lastLoadFriendListCharId;

    void AddFriend(uint64 characterId, uint64 friendId) override
    {
        lastAddFriend = { characterId, friendId };
    }

    void RemoveFriend(uint64 characterId, uint64 friendId) override
    {
        lastRemoveFriend = { characterId, friendId };
    }

    std::optional<std::vector<FriendData>> LoadFriendList(uint64 characterId) override
    {
        lastLoadFriendListCharId = characterId;
        return friendListToReturn;
    }

    std::vector<uint64> GetCharactersWithFriend(uint64) override
    {
        return admirersToReturn;
    }

    bool AreFriends(uint64, uint64) override
    {
        return areFriendsResult;
    }
};

// -----------------------------------------------------------------------
// MockMOTDDatabase
// -----------------------------------------------------------------------

struct MockMOTDDatabase : IMOTDDatabase
{
    std::optional<String> motdToReturn;
    std::optional<std::string> lastSetMotd;

    std::optional<String> GetMessageOfTheDay() override
    {
        return motdToReturn;
    }

    void SetMessageOfTheDay(const std::string& motd) override
    {
        lastSetMotd = motd;
    }
};

// -----------------------------------------------------------------------
// MockGroupDatabase
// -----------------------------------------------------------------------

struct MockGroupDatabase : IGroupDatabase
{
    std::optional<GroupData> groupToReturn;
    std::vector<uint64> groupIdsToReturn;

    std::optional<uint64> lastCreatedGroupId;
    std::optional<uint64> disbandedGroupId;
    std::vector<std::pair<uint64,uint64>> addedMembers;
    std::vector<std::pair<uint64,uint64>> removedMembers;

    void CreateGroup(uint64 id, uint64 leaderGuid, uint8, uint8) override
    {
        lastCreatedGroupId = id;
        if (!groupToReturn)
        {
            GroupData g;
            g.leaderGuid = leaderGuid;
            groupToReturn = g;
        }
    }

    void SetGroupLootMethod(uint64, uint8, uint64, uint8) override {}

    void SetGroupLeader(uint64, uint64) override {}

    void AddGroupMember(uint64 groupId, uint64 memberGuid) override
    {
        addedMembers.emplace_back(groupId, memberGuid);
    }

    void RemoveGroupMember(uint64 groupId, uint64 memberGuid) override
    {
        removedMembers.emplace_back(groupId, memberGuid);
    }

    void DisbandGroup(uint64 groupId) override
    {
        disbandedGroupId = groupId;
    }

    std::optional<std::vector<uint64>> ListGroups() override
    {
        return groupIdsToReturn;
    }

    std::optional<GroupData> LoadGroup(uint64) override
    {
        return groupToReturn;
    }
};

// -----------------------------------------------------------------------
// MockPetDatabase
// -----------------------------------------------------------------------

struct MockPetDatabase : IPetDatabase
{
    /// The rows per character. Loading fails while loadFails is set.
    std::map<uint64, std::vector<PersistentPetData>> petsByCharacter;
    bool loadFails = false;

    /// The id the next created pet gets; 0 makes the creation fail.
    uint64 nextPetId = 1;

    struct SaveCall
    {
        uint64 characterId;
        std::vector<PersistentPetData> pets;
        std::vector<uint64> removedPetIds;
    };
    std::vector<SaveCall> saves;
    std::vector<std::pair<uint64, PersistentPetData>> creations;
    std::vector<uint64> deletedCharacters;

    std::optional<std::vector<PersistentPetData>> LoadCharacterPets(uint64 characterId) override
    {
        if (loadFails)
        {
            return std::nullopt;
        }

        const auto it = petsByCharacter.find(characterId);
        return it != petsByCharacter.end() ? it->second : std::vector<PersistentPetData>{};
    }

    void SaveCharacterPets(uint64 characterId, const std::vector<PersistentPetData>& pets, const std::vector<uint64>& removedPetIds) override
    {
        saves.push_back({ characterId, pets, removedPetIds });
    }

    uint64 CreateCharacterPet(uint64 characterId, const PersistentPetData& pet) override
    {
        creations.emplace_back(characterId, pet);
        if (nextPetId == 0)
        {
            return 0;
        }

        return nextPetId++;
    }

    void DeleteCharacterPets(uint64 characterId) override
    {
        deletedCharacters.push_back(characterId);
        petsByCharacter.erase(characterId);
    }
};

} // namespace mmo
