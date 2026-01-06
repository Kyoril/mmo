// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_profile.h"
#include "bot_actions/move_to_position_action.h"
#include "bot_actions/wait_action.h"

#include "log/default_log_levels.h"

#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace mmo
{
	/// A bot profile that moves to a series of waypoints.
	/// Useful for testing movement and pathfinding.
	class PatrolProfile final : public BotProfile
	{
	public:
		PatrolProfile(std::vector<Vector3> waypoints, bool loop = true)
			: m_waypoints(std::move(waypoints))
			, m_loop(loop)
		{
		}

		std::string GetName() const override
		{
			return "Patrol";
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			ILOG("Patrol profile activated with " << m_waypoints.size() << " waypoints");

			// Queue movement to all waypoints
			for (const auto& waypoint : m_waypoints)
			{
				QueueAction(std::make_shared<MoveToPositionAction>(waypoint));
				QueueAction(std::make_shared<WaitAction>(1000ms)); // Pause at each waypoint
			}
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			if (m_loop)
			{
				// Restart patrol
				ILOG("Restarting patrol loop");
				for (const auto& waypoint : m_waypoints)
				{
					QueueAction(std::make_shared<MoveToPositionAction>(waypoint));
					QueueAction(std::make_shared<WaitAction>(1000ms));
				}
				return true;
			}

			// Done patrolling, just idle
			QueueAction(std::make_shared<WaitAction>(std::chrono::hours(1)));
			return true;
		}

	private:
		std::vector<Vector3> m_waypoints;
		bool m_loop;
	};
}
