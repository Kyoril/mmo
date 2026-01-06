// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"

#include "base/clock.h"

#include <chrono>
#include <sstream>

namespace mmo
{
	/// Action that waits for a specified duration.
	class WaitAction final : public BotAction
	{
	public:
		explicit WaitAction(std::chrono::milliseconds duration)
			: m_duration(duration)
			, m_startTime(0)
		{
		}

		std::string GetDescription() const override
		{
			const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(m_duration).count();
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(m_duration).count() % 1000;
			
			std::ostringstream oss;
			oss << "Wait for ";
			if (seconds > 0)
			{
				oss << seconds << "s";
			}
			if (ms > 0)
			{
				if (seconds > 0)
				{
					oss << " ";
				}
				oss << ms << "ms";
			}
			return oss.str();
		}

		ActionResult Execute(BotContext& context) override
		{
			// Initialize start time on first execution
			if (m_startTime == 0)
			{
				m_startTime = context.GetServerTime();
				return ActionResult::InProgress;
			}

			// Check if enough time has passed
			const GameTime currentTime = context.GetServerTime();
			const auto elapsed = std::chrono::milliseconds(currentTime - m_startTime);
			
			if (elapsed >= m_duration)
			{
				return ActionResult::Success;
			}

			return ActionResult::InProgress;
		}

		void OnAbort(BotContext& context) override
		{
			m_startTime = 0;
		}

		/// Wait actions are interruptible - they can be aborted by urgent event handlers.
		bool IsInterruptible() const override
		{
			return true;
		}

	private:
		std::chrono::milliseconds m_duration;
		GameTime m_startTime;
	};
}
