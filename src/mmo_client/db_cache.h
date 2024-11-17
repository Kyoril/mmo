#pragma once

#include "base/typedefs.h"

#include "binary_io/writer.h"
#include "binary_io/reader.h"
#include "version.h"

#include <functional>
#include <unordered_map>

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

		void NotifyObjectResponse(uint64 guid, const T& object)
		{
			m_cache[guid] = object;
			const T& objectRef = m_cache[guid];

			auto range = m_pendingRequests.equal_range(guid);
			for (auto i = range.first; i != range.second; ++i)
			{
				i->second(guid, objectRef);
			}

			m_pendingRequests.erase(range.first, range.second);
		}

		void Serialize(io::Writer& writer)
		{
			// Read file format header
			constexpr uint32 header = 'CDBC';
			writer << io::write<uint32>(header) << io::write<uint32>(Revision);

			writer << io::write<uint32>(m_cache.size());
			for (auto& [id, object] : m_cache)
			{
				writer << io::write<uint64>(id);
				writer << object;
			}
		}

		bool Deserialize(io::Reader& reader)
		{
			// Read file format header
			uint32 header = 0;
			if (!(reader >> io::read<uint32>(header)) || header != static_cast<uint32>('CDBC'))
			{
				return false;
			}

			// Read build version number
			uint32 build = 0;
			if (!(reader >> io::read<uint32>(build)))
			{
				return false;
			}

			// If cache is incompatible with our version, refuse to load it
			if (build != Revision)
			{
				return false;
			}

			// Read item count
			uint32 itemCount = 0;
			if (!(reader >> io::read<uint32>(itemCount)))
			{
				return false;
			}

			// Allow for efficient reading
			m_cache.reserve(itemCount);

			for (uint32 i = 0; i < itemCount; ++i)
			{
				uint64 id = 0;
				if (!(reader >> io::read<uint64>(id)))
				{
					return false;
				}

				// Read object entry
				reader >> m_cache[id];
			}

			return reader;
		}

	private:
		RealmConnector& m_realmConnector;
		std::unordered_map<uint64, T> m_cache;
		std::unordered_multimap<uint64, QueryCallback> m_pendingRequests;
	};
}
