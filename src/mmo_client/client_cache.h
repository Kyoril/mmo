#pragma once

#include "db_cache.h"
#include "game/creature_data.h"
#include "game/item.h"
#include "game/quest_info.h"

namespace mmo
{
	typedef DBCache<ItemInfo, game::client_realm_packet::ItemQuery> DBItemCache;
	typedef DBCache<CreatureInfo, game::client_realm_packet::CreatureQuery> DBCreatureCache;
	typedef DBCache<QuestInfo, game::client_realm_packet::QuestQuery> DBQuestCache;
	typedef DBCache<String, game::client_realm_packet::NameQuery> DBNameCache;
}