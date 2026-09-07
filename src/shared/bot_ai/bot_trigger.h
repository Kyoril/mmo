// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <string>
#include <utility>

namespace mmo
{
	class BotAiContext;

	/// A condition the engine polls to decide whether to push actions.
	///
	/// Triggers are shared and stateless: one instance answers for every bot in the swarm, and
	/// everything it needs comes from the context it is handed. Anything it wants to remember
	/// belongs on the context, not here.
	class BotTrigger : public NonCopyable
	{
	public:
		explicit BotTrigger(std::string name, const GameTime checkIntervalMs = 0)
			: m_name(std::move(name))
			, m_checkIntervalMs(checkIntervalMs)
		{
		}

		virtual ~BotTrigger() = default;

		[[nodiscard]] const std::string& GetName() const { return m_name; }

		/// Minimum time between two checks of this trigger. Zero means every tick. Use it for
		/// conditions that are expensive to evaluate or that cannot meaningfully change quickly.
		[[nodiscard]] GameTime GetCheckIntervalMs() const { return m_checkIntervalMs; }

		/// Whether the condition holds right now.
		[[nodiscard]] virtual bool IsActive(BotAiContext& context) const = 0;

	private:
		std::string m_name;
		GameTime m_checkIntervalMs { 0 };
	};
}
