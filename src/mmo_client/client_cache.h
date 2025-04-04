#pragma once

#include "cache_provider.h"
#include "base/non_copyable.h"

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
		[[nodiscard]] DBItemCache& GetItemCache() noexcept override { return m_itemCache; }
		[[nodiscard]] DBCreatureCache& GetCreatureCache() noexcept override { return m_creatureCache; }
		[[nodiscard]] DBQuestCache& GetQuestCache() noexcept override { return m_questCache; }
		[[nodiscard]] DBNameCache& GetNameCache() noexcept override { return m_nameCache; }
		[[nodiscard]] DBGuildCache& GetGuildCache() noexcept override { return m_guildCache; }
		[[nodiscard]] DBObjectCache& GetObjectCache() noexcept override { return m_objectCache; }

	private:
		DBItemCache m_itemCache;
		DBCreatureCache m_creatureCache;
		DBQuestCache m_questCache;
		DBNameCache m_nameCache;
		DBGuildCache m_guildCache;
		DBObjectCache m_objectCache;
	};
}
