// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
// Stub implementations for realm Player and World methods that are referenced
// by PlayerManager/PlayerGroup but are not needed for unit tests.
// These satisfy the linker without pulling in the full Player/World implementations.

#include "realm_server/player.h"
#include "realm_server/world.h"

namespace mmo
{
    // ---- Player stubs ----

    void Player::Kick() {}

    void Player::SendAuthChallenge() {}

    void Player::SendMessageOfTheDay(const std::string&) {}

    void Player::ClearDungeonBindingByInstanceId(InstanceId) {}

    // ---- World stubs ----

    void World::NotifyPlayerGroupChanged(uint64, uint64, uint8, uint8) {}
}
