// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "join_callback_registry.h"

namespace mmo
{
	void JoinCallbackRegistry::Register(const uint64 characterGuid, JoinWorldCallback callback)
	{
		if (!callback)
		{
			return;
		}

		std::scoped_lock lock{ m_mutex };
		m_callbacks.insert_or_assign(characterGuid, std::move(callback));
	}

	bool JoinCallbackRegistry::Consume(const uint64 characterGuid, const InstanceId& instanceId, const bool success)
	{
		JoinWorldCallback callback;

		{
			std::scoped_lock lock{ m_mutex };

			const auto it = m_callbacks.find(characterGuid);
			if (it == m_callbacks.end())
			{
				return false;
			}

			callback = std::move(it->second);
			m_callbacks.erase(it);
		}

		// Invoked outside the lock: the callback may start another join for the same character.
		callback(instanceId, success);
		return true;
	}
}
