// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "realm_manager.h"
#include "realm.h"
#include "binary_io/string_sink.h"
#include <cassert>

namespace mmo
{
	RealmManager::RealmManager(
	    size_t capacity)
		: m_capacity(capacity)
	{
	}

	RealmManager::~RealmManager()
	{
	}

	void RealmManager::RealmDisconnected(Realm &client)
	{
		std::scoped_lock scopedLock{ m_realmsMutex };

		const auto p = std::find_if(
		                   m_realms.begin(),
		                   m_realms.end(),
		                   [&client](const std::shared_ptr<Realm> &p)
		{
			return (&client == p.get());
		});
		assert(p != m_realms.end());
		m_realms.erase(p);
	}
	
	bool RealmManager::HasCapacityBeenReached()
	{
		std::scoped_lock scopedLock{ m_realmsMutex };
		return m_realms.size() >= m_capacity;
	}

	void RealmManager::AddRealm(std::shared_ptr<Realm> added)
	{
		std::scoped_lock scopedLock{ m_realmsMutex };

		assert(added);
		m_realms.push_back(std::move(added));
	}

	Realm * RealmManager::GetRealmByName(const String &name)
	{
		std::scoped_lock scopedLock{ m_realmsMutex };

		const auto p = std::find_if(
			m_realms.begin(),
			m_realms.end(),
			[&name](const std::shared_ptr<Realm> &p)
		{
			return (p->IsAuthentificated() && name == p->GetRealmName());
		});

		if (p != m_realms.end())
		{
			return (*p).get();
		}

		return nullptr;
	}

	Realm * RealmManager::GetRealmByID(uint32 id)
	{
		std::scoped_lock scopedLock{ m_realmsMutex };

		const auto p = std::find_if(
			m_realms.begin(),
			m_realms.end(),
			[id](const std::shared_ptr<Realm> &p)
		{
			return (p->IsAuthentificated() && id == p->GetRealmId());
		});

		if (p != m_realms.end())
		{
			return (*p).get();
		}

		return nullptr;
	}
}
