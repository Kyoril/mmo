#pragma once

#include <list>
#include <unordered_map>
#include <memory>

namespace mmo
{
	namespace terrain
	{
		/// @brief A generic LRU (Least Recently Used) cache implementation with configurable size limit.
		/// @tparam KeyType The type of the cache key.
		/// @tparam ValueType The type of the cached value (must be a unique_ptr).
		template<typename KeyType, typename ValueType>
		class LRUCache
		{
		public:
			/// @brief Constructs an LRU cache with the specified maximum size.
			/// @param maxSize The maximum number of entries the cache can hold. When this limit is reached,
			///                the least recently used entry will be evicted when a new entry is added.
			explicit LRUCache(size_t maxSize)
				: m_maxSize(maxSize)
			{
			}

			~LRUCache() = default;

			LRUCache(const LRUCache&) = delete;
			LRUCache& operator=(const LRUCache&) = delete;

			LRUCache(LRUCache&&) = default;
			LRUCache& operator=(LRUCache&&) = default;

			/// @brief Retrieves a value from the cache.
			/// @param key The key to look up.
			/// @return Pointer to the cached value if found, nullptr otherwise. The returned pointer
			///         remains valid until the entry is evicted or the cache is cleared.
			ValueType* Get(const KeyType& key)
			{
				auto it = m_map.find(key);
				if (it == m_map.end())
				{
					return nullptr;
				}

				// Move this entry to the front of the list (most recently used)
				m_list.splice(m_list.begin(), m_list, it->second.listIterator);

				return it->second.value.get();
			}

			/// @brief Inserts a new entry into the cache.
			/// @param key The key for the entry.
			/// @param value The value to cache (ownership is transferred to the cache).
			/// @details If the cache is at capacity, the least recently used entry will be evicted.
			///          If an entry with the same key already exists, it will be replaced and moved
			///          to the front of the LRU list.
			void Put(const KeyType& key, std::unique_ptr<ValueType> value)
			{
				auto it = m_map.find(key);

				// If key already exists, update it and move to front
				if (it != m_map.end())
				{
					it->second.value = std::move(value);
					m_list.splice(m_list.begin(), m_list, it->second.listIterator);
					return;
				}

				// If cache is full, evict the least recently used entry
				if (m_list.size() >= m_maxSize)
				{
					const KeyType& evictKey = m_list.back();
					m_map.erase(evictKey);
					m_list.pop_back();
				}

				// Add new entry to the front
				m_list.push_front(key);
				CacheEntry entry;
				entry.value = std::move(value);
				entry.listIterator = m_list.begin();
				m_map[key] = std::move(entry);
			}

			/// @brief Checks if a key exists in the cache without updating access time.
			/// @param key The key to check.
			/// @return True if the key exists in the cache, false otherwise.
			bool Contains(const KeyType& key) const
			{
				return m_map.find(key) != m_map.end();
			}

			/// @brief Removes all entries from the cache.
			void Clear()
			{
				m_map.clear();
				m_list.clear();
			}

			/// @brief Gets the current number of entries in the cache.
			/// @return The number of entries.
			size_t Size() const
			{
				return m_list.size();
			}

			/// @brief Gets the maximum capacity of the cache.
			/// @return The maximum number of entries the cache can hold.
			size_t MaxSize() const
			{
				return m_maxSize;
			}

			/// @brief Sets a new maximum capacity for the cache.
			/// @param newMaxSize The new maximum size. If smaller than the current size,
			///                   least recently used entries will be evicted until the size
			///                   matches the new limit.
			void SetMaxSize(size_t newMaxSize)
			{
				m_maxSize = newMaxSize;

				// Evict entries if we're now over capacity
				while (m_list.size() > m_maxSize)
				{
					const KeyType& evictKey = m_list.back();
					m_map.erase(evictKey);
					m_list.pop_back();
				}
			}

		private:
			/// @brief Internal structure for cache entries.
			struct CacheEntry
			{
				std::unique_ptr<ValueType> value;
				typename std::list<KeyType>::iterator listIterator;
			};

			size_t m_maxSize;
			std::list<KeyType> m_list;
			std::unordered_map<KeyType, CacheEntry> m_map;
		};
	}
}
