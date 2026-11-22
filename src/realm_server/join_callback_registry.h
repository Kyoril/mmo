// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/game.h"

#include <functional>
#include <map>
#include <mutex>

namespace mmo
{
	/// Called once a world node answered a character join request.
	typedef std::function<void(InstanceId instanceId, bool success)> JoinWorldCallback;

	/// Keeps the pending join request per character until the world node answers it.
	///
	/// There is at most one pending request per character. A world node can answer a character
	/// more than once (the client timed out on a slow instance creation and asked again), so an
	/// answer without a pending request is expected and must be ignored, never invoked.
	class JoinCallbackRegistry final
	{
	public:
		/// Stores the callback for the character's join request. A request still pending for the
		/// same character is replaced: it belongs to an attempt the client has given up on.
		/// @param characterGuid The joining character.
		/// @param callback Called with the world node's answer. An empty callback is not stored.
		void Register(uint64 characterGuid, JoinWorldCallback callback);

		/// Hands the world node's answer to the character's pending request and forgets it.
		/// @param characterGuid The character the answer is for.
		/// @param instanceId The instance the character joined (only valid on success).
		/// @param success Whether the character joined.
		/// @returns true if a pending request received the answer, false if there was none.
		bool Consume(uint64 characterGuid, const InstanceId& instanceId, bool success);

	private:
		std::mutex m_mutex;
		std::map<uint64, JoinWorldCallback> m_callbacks;
	};
}
