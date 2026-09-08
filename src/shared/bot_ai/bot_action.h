// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_next_action.h"

#include "base/non_copyable.h"

#include <string>
#include <utility>

namespace mmo
{
	class BotAiContext;

	/// Something a bot can do. Shared and stateless across the swarm, like triggers.
	///
	/// An action is asked three questions in order, and the split matters:
	///
	///   IsUseful   - would doing this accomplish anything? A no drops it silently.
	///   IsPossible - are the preconditions met? A no runs the prerequisites first, then retries
	///                on a later tick. This is what makes chains like "face the target before
	///                swinging at it" fall out of the data rather than being coded.
	///   Execute    - do it. A false runs the alternatives.
	///
	/// Answering "no" to IsUseful when the real answer is "not yet" is the common mistake: it
	/// throws the action away instead of arranging for it to become possible.
	class BotAction : public NonCopyable
	{
	public:
		explicit BotAction(std::string name)
			: m_name(std::move(name))
		{
		}

		virtual ~BotAction() = default;

		[[nodiscard]] const std::string& GetName() const { return m_name; }

		[[nodiscard]] virtual bool IsUseful(BotAiContext& context) const { return true; }

		[[nodiscard]] virtual bool IsPossible(BotAiContext& context) const { return true; }

		/// Runs the action. Returning false is not an error - it means "this did not work out",
		/// and the engine will try the alternatives.
		virtual bool Execute(BotAiContext& context) = 0;

		/// Actions that must run before this one can become possible. Pushed above this action
		/// when IsPossible says no.
		[[nodiscard]] virtual BotNextActionList GetPrerequisites() const { return {}; }

		/// Actions to try when Execute returns false.
		[[nodiscard]] virtual BotNextActionList GetAlternatives() const { return {}; }

		/// Actions to queue after this one succeeds, so that a multi-step behaviour keeps going
		/// without needing a trigger to notice it started.
		[[nodiscard]] virtual BotNextActionList GetContinuers() const { return {}; }

	private:
		std::string m_name;
	};
}
