#pragma once

#include "base/typedefs.h"

#include <functional>
#include <unordered_map>
#include <optional>
#include <vector>

#include "net/realm_connector.h"

namespace mmo
{
	class RealmConnector;

	template<typename T, game::client_realm_packet::Type RequestOpCode>
	class DBCache
	{
	public:
		typedef std::function<void(uint64 guid, const T&)> QueryCallback;

	public:
		DBCache(RealmConnector& realmConnector)
			: m_realmConnector(realmConnector)
		{
		}

	public:
		void Get(uint64 guid, QueryCallback&& callback)
		{
			auto it = m_cache.find(guid);
			if (it != m_cache.end())
			{
				callback(guid, it->second);
				return;
			}

			// Enqueue pending request callback
			const auto requestIt = m_pendingRequests.find(guid);
			if (requestIt == m_pendingRequests.end())
			{
				// Send request to server asking for entry
				m_realmConnector.sendSinglePacket([guid](game::OutgoingPacket& packet)
					{
						packet.Start(RequestOpCode);
						packet
							<< io::write_packed_guid(guid);
						packet.Finish();
					});
			}

			m_pendingRequests.insert(std::pair{ guid, callback });

		}

		void NotifyObjectResponse(uint64 guid, T&& object)
		{
			m_cache[guid] = std::move(object);
			const T& objectRef = m_cache[guid];

			auto range = m_pendingRequests.equal_range(guid);
			for (auto i = range.first; i != range.second; ++i)
			{
				i->second(guid, objectRef);
			}

			m_pendingRequests.erase(range.first, range.second);
		}

	private:
		RealmConnector& m_realmConnector;
		std::unordered_map<uint64, T> m_cache;
		std::unordered_multimap<uint64, QueryCallback> m_pendingRequests;
	};
}
