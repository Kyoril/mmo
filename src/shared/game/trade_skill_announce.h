// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/trade_skill_data.h"

#include <map>

namespace mmo
{
	/// Decides whether a received skill update warrants a "skill increased" chat message.
	/// Only a genuine rise of the skill value of an already known skill counts: the initial
	/// learn (no cached state) and pure cap changes do not.
	/// @param cached The client's cached skills by skill id, as before applying the update.
	/// @param update The skill state received from the server.
	/// @return True if the skill value is higher than the cached one.
	inline bool ShouldAnnounceSkillIncrease(const std::map<uint32, TradeSkillData>& cached, const TradeSkillData& update)
	{
		const auto it = cached.find(update.skillId);
		return it != cached.end() && update.value > it->second.value;
	}
}
