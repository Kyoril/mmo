#pragma once

#include "cache_provider.h"
#include "base/non_copyable.h"

// The caches own their entries by value, so whoever destroys a ClientCache needs the full types.
#include "game/creature_data.h"
#include "game/item.h"
#include "game/quest_info.h"

namespace mmo
{
	class RealmConnector;

	class ClientCache : public NonCopyable, public ICacheProvider
	{
	public:
		explicit ClientCache(RealmConnector& connector);

	public:
		bool Load();

		void Save();

	public:
		[[nodiscard]] DBItemCache& GetItemCache() override { return m_itemCache; }
		[[nodiscard]] DBCreatureCache& GetCreatureCache() override { return m_creatureCache; }
		[[nodiscard]] DBQuestCache& GetQuestCache() override { return m_questCache; }
		[[nodiscard]] DBNameCache& GetNameCache() override { return m_nameCache; }
		[[nodiscard]] DBGuildCache& GetGuildCache() override { return m_guildCache; }
		[[nodiscard]] DBObjectCache& GetObjectCache() override { return m_objectCache; }

	private:
		DBItemCache m_itemCache;
		DBCreatureCache m_creatureCache;
		DBQuestCache m_questCache;
		DBNameCache m_nameCache;
		DBGuildCache m_guildCache;
		DBObjectCache m_objectCache;
	};
}
