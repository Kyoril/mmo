// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <string>
#include <utility>

namespace mmo
{
	class BotAiContext;

	/// Scales or vetoes the relevance of an action just before the engine considers running it.
	///
	/// Returning zero drops the action entirely, and that is the point: a whole mode of behaviour
	/// - a dead bot, a passive bot, a bot in a scripted sequence - can be expressed as one
	/// multiplier that vetoes everything outside a whitelist, with no special cases anywhere in
	/// the engine or in the strategies it is layered over.
	class BotMultiplier : public NonCopyable
	{
	public:
		explicit BotMultiplier(std::string name)
			: m_name(std::move(name))
		{
		}

		virtual ~BotMultiplier() = default;

		[[nodiscard]] const std::string& GetName() const { return m_name; }

		/// Factor to apply to the relevance of the action. 1 leaves it alone, 0 vetoes it.
		[[nodiscard]] virtual float GetValue(BotAiContext& context, const std::string& actionName) const = 0;

	private:
		std::string m_name;
	};
}
