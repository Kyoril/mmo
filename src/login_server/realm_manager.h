// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include <memory>
#include <mutex>
#include <list>

#include "base/signal.h"

namespace mmo
{
	class Realm;

	/// Manages all connected players.
	class RealmManager final : public NonCopyable
	{
	public:

		typedef std::list<std::shared_ptr<Realm>> Realms;

	public:

		/// Initializes a new instance of the player manager class.
		/// @param playerCapacity The maximum number of connections that can be connected at the same time.
		explicit RealmManager(
		    size_t playerCapacity
		);
		~RealmManager();

		/// Notifies the manager that a player has been disconnected which will
		/// delete the player instance.
		void RealmDisconnected(Realm &realm);

		/// Determines whether the player capacity limit has been reached.
		bool HasCapacityBeenReached();

		/// Adds a new player instance to the manager.
		void AddRealm(std::shared_ptr<Realm> added);

		/// Gets a realm by it's name.
		Realm *GetRealmByName(const String &realmName);

		/// 
		Realm *GetRealmByID(uint32 id);

		void NotifyAccountBanned(uint64 accountId);

		/// Executes a function callback for each realm.
		template<class Functor>
		void ForEachRealm(Functor f)
		{
			std::scoped_lock lock{ m_realmsMutex };

			for (const auto& realm : m_realms)
			{
				f(*realm);
			}
		}

	private:

		Realms m_realms;
		size_t m_capacity;
		std::mutex m_realmsMutex;
	};
}
