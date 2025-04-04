
#include "client_cache.h"

#include "stream_sink.h"
#include "stream_source.h"
#include "assets/asset_registry.h"
#include "net/realm_connector.h"

#include "game/item.h"
#include "game/guild_info.h"
#include "game/creature_data.h"
#include "game/quest_info.h"
#include "game/object_info.h"

namespace mmo
{
	static const char* const s_itemCacheFilename = "Cache/Items.db";
	static const char* const s_creatureCacheFilename = "Cache/Creatures.db";
	static const char* const s_questCacheFilename = "Cache/Quests.db";
	static const char* const s_objectCacheFilename = "Cache/Objects.db";

	ClientCache::ClientCache(RealmConnector& connector)
		: m_itemCache(connector)
		, m_creatureCache(connector)
		, m_questCache(connector)
		, m_nameCache(connector)
		, m_guildCache(connector)
		, m_objectCache(connector)
	{
	}

	bool ClientCache::Load()
	{
		// Initialize item cache (TODO: Try loading cache from file so we maybe won't have to ask the server next time we run the client)
		if (const auto itemCacheFile = AssetRegistry::OpenFile(s_itemCacheFilename))
		{
			io::StreamSource source(*itemCacheFile);
			io::Reader reader(source);
			m_itemCache.Deserialize(reader);
		}

		if (const auto creatureCacheFile = AssetRegistry::OpenFile(s_creatureCacheFilename))
		{
			io::StreamSource source(*creatureCacheFile);
			io::Reader reader(source);
			m_creatureCache.Deserialize(reader);
		}

		if (const auto questCacheFile = AssetRegistry::OpenFile(s_questCacheFilename))
		{
			io::StreamSource source(*questCacheFile);
			io::Reader reader(source);
			m_questCache.Deserialize(reader);
		}

		if (const auto objectCacheFile = AssetRegistry::OpenFile(s_objectCacheFilename))
		{
			io::StreamSource source(*objectCacheFile);
			io::Reader reader(source);
			m_objectCache.Deserialize(reader);
		}

		return true;
	}

	void ClientCache::Save()
	{
		// Serialize caches
		if (const auto itemCacheFile = AssetRegistry::CreateNewFile(s_itemCacheFilename))
		{
			io::StreamSink sink(*itemCacheFile);
			io::Writer writer(sink);
			m_itemCache.Serialize(writer);
		}
		if (const auto creatureCacheFile = AssetRegistry::CreateNewFile(s_creatureCacheFilename))
		{
			io::StreamSink sink(*creatureCacheFile);
			io::Writer writer(sink);
			m_creatureCache.Serialize(writer);
		}
		if (const auto questCacheFile = AssetRegistry::CreateNewFile(s_questCacheFilename))
		{
			io::StreamSink sink(*questCacheFile);
			io::Writer writer(sink);
			m_questCache.Serialize(writer);
		}
		if (const auto objectCacheFile = AssetRegistry::CreateNewFile(s_objectCacheFilename))
		{
			io::StreamSink sink(*objectCacheFile);
			io::Writer writer(sink);
			m_objectCache.Serialize(writer);
		}
	}
}
