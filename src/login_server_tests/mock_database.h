#pragma once
#include "login_server/database.h"
#include <optional>
#include <string>
#include <tuple>

namespace mmo {

struct MockDatabase : IDatabase {
    std::optional<AccountData> accountData;
    std::optional<AccountCreationResult> accountCreateResult = AccountCreationResult::Success;
    std::optional<RealmCreationResult> realmCreateResult = RealmCreationResult::Success;
    bool setGmLevelResult = true;

    std::optional<AccountData> GetAccountDataByName(std::string) override { return accountData; }
    std::optional<RealmAuthData> GetRealmAuthData(std::string) override { return std::nullopt; }
    std::optional<std::tuple<uint64, std::string, uint8>> sessionKeyResult = std::nullopt;
    std::optional<std::tuple<uint64, std::string, uint8>> GetAccountSessionKey(std::string) override { return sessionKeyResult; }
    bool SetAccountGMLevel(std::string, uint8) override { return setGmLevelResult; }
    void PlayerLogin(uint64, const std::string&, const std::string&) override {}
    void PlayerLoginFailed(uint64, const std::string&) override {}
    void RealmLogin(uint32, const std::string&, const std::string&, const std::string&) override {}
    std::optional<AccountCreationResult> AccountCreate(const std::string&, const std::string&, const std::string&) override { return accountCreateResult; }
    std::optional<RealmCreationResult> RealmCreate(const std::string&, const std::string&, uint16, const std::string&, const std::string&) override { return realmCreateResult; }
    void BanAccountByName(const std::string&, const std::string&, const std::string&) override {}
    void UnbanAccountByName(const std::string&, const std::string&) override {}
    AccountListResult GetAccountList(const AccountListParams&) override { return {}; }
    std::vector<RealmListEntry> GetRealmList() override { return {}; }

    std::vector<FeatureDefinition> GetFeatures() override { return {}; }
    std::optional<uint32> CreateFeature(const std::string&, const std::string&) override { return std::nullopt; }
    bool DeleteFeature(uint32) override { return true; }
    std::optional<uint32> GetFeatureIdByName(const std::string&) override { return std::nullopt; }
    std::optional<uint64> GetAccountIdByName(const std::string&) override { return std::nullopt; }
    bool GrantFeature(uint32, const std::vector<uint64>&, const std::string&) override { return true; }
    bool RevokeFeature(uint32, const std::vector<uint64>&) override { return true; }
    std::vector<AccountFeature> GetActiveAccountFeatures(uint64) override { return {}; }
    bool SetRealmFeatureRequirement(uint32, uint32, bool, bool) override { return true; }
    bool RemoveRealmFeatureRequirement(uint32, uint32) override { return true; }
    std::vector<RealmFeatureRequirement> GetRealmFeatureRequirements(uint32) override { return {}; }
    std::optional<uint32> GetRealmIdByName(const std::string&) override { return std::nullopt; }
    std::optional<AccountAuthData> GetAccountAuthData(std::string) override { return std::nullopt; }
};

} // namespace mmo
