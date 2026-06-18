// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

	/// Manages all connected realms.
	class RealmManager final : public NonCopyable
	{
	public:

		typedef std::list<std::shared_ptr<Realm>> Realms;

	public:

		/// Initializes a new instance of the realm manager class.
		/// @param playerCapacity The maximum number of realms that can be connected at the same time.
		explicit RealmManager(
		    size_t playerCapacity
		);
		~RealmManager();

		/// Notifies the manager that a realm has been disconnected which will
		/// delete the realm instance.
		void RealmDisconnected(Realm &realm);

		/// Determines whether the realm capacity limit has been reached.
		bool HasCapacityBeenReached();

		/// Adds a new realm instance to the manager.
		void AddRealm(std::shared_ptr<Realm> added);

		/// Gets a realm by it's name.
		Realm *GetRealmByName(const String &realmName);

		/// Gets a realm by id.
		Realm *GetRealmByID(uint32 id);

		/// Notifies all realms that an account has been banned.
		void NotifyAccountBanned(uint64 accountId);

		/// Notifies a connected realm that its feature requirements changed, prompting it to reload
		/// them from the database. Does nothing if the realm is not currently connected.
		void NotifyRealmRequirementsChanged(uint32 realmId);

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
