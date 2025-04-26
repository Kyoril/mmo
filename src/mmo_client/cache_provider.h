#pragma once

#include "db_cache.h"
#include "game/guild_info.h"
#include "game/object_info.h"

namespace mmo
{
	struct QuestInfo;
	struct CreatureInfo;
	struct ItemInfo;

	typedef DBCache<ItemInfo, game::client_realm_packet::ItemQuery> DBItemCache;
	typedef DBCache<CreatureInfo, game::client_realm_packet::CreatureQuery> DBCreatureCache;
	typedef DBCache<QuestInfo, game::client_realm_packet::QuestQuery> DBQuestCache;
	typedef DBCache<String, game::client_realm_packet::NameQuery> DBNameCache;
	typedef DBCache<GuildInfo, game::client_realm_packet::GuildQuery> DBGuildCache;
	typedef DBCache<ObjectInfo, game::client_realm_packet::ObjectQuery> DBObjectCache;

	class ICacheProvider
	{
	public:
		virtual ~ICacheProvider() = default;

	public:
		/// Returns the item cache.
		virtual DBItemCache& GetItemCache() = 0;

		/// Returns the creature cache.
		virtual DBCreatureCache& GetCreatureCache() = 0;

		/// Returns the quest cache.
		virtual DBQuestCache& GetQuestCache() = 0;

		/// Returns the name cache.
		virtual DBNameCache& GetNameCache() = 0;

		/// Returns the guild cache.
		virtual DBGuildCache& GetGuildCache() = 0;

		/// Returns the object cache.
		virtual DBObjectCache& GetObjectCache() = 0;
	};
}
