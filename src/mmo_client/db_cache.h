#pragma once

#include "base/typedefs.h"

#include <unordered_map>
#include <optional>

#include "net/realm_connector.h"

namespace mmo
{
	class RealmConnector;

	template<typename T>
	class DBCache
	{
	public:
		DBCache(const RealmConnector& realmConnector)
			: m_realmConnector(realmConnector)
		{
		}

	public:
		template<typename TCallback>
		std::optional<const T&> Get(uint64 guid, TCallback callback) const
		{
			auto it = m_cache.find(guid);
			if (it != m_cache.end())
			{
				return it->second;
			}

			// Send request to realm server asking for entry
			m_realmConnector.sendSinglePacket([guid](const game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::NameQuery);
					packet
						<< io::write_packed_guid(guid);
					packet.Finish();
				});

			return {};
		}

	private:
		RealmConnector& m_realmConnector;
		std::unordered_map<uint64, T> m_cache;
	};
}
