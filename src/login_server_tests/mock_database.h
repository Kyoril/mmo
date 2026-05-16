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
    std::optional<std::tuple<uint64, std::string, uint8>> GetAccountSessionKey(std::string) override { return std::nullopt; }
    bool SetAccountGMLevel(std::string, uint8) override { return setGmLevelResult; }
    void PlayerLogin(uint64, const std::string&, const std::string&) override {}
    void PlayerLoginFailed(uint64, const std::string&) override {}
    void RealmLogin(uint32, const std::string&, const std::string&, const std::string&) override {}
    std::optional<AccountCreationResult> AccountCreate(const std::string&, const std::string&, const std::string&) override { return accountCreateResult; }
    std::optional<RealmCreationResult> RealmCreate(const std::string&, const std::string&, uint16, const std::string&, const std::string&) override { return realmCreateResult; }
    void BanAccountByName(const std::string&, const std::string&, const std::string&) override {}
    void UnbanAccountByName(const std::string&, const std::string&) override {}
};

} // namespace mmo
