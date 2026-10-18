// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_path_follower.h"

#include <cmath>
#include <utility>

namespace mmo
{
	namespace
	{
		float PlanarDistance(const Vector3& a, const Vector3& b)
		{
			const float dx = a.x - b.x;
			const float dz = a.z - b.z;
			return std::sqrt(dx * dx + dz * dz);
		}
	}

	void BotPathFollower::Start(std::vector<Vector3> points, const GameTime now)
	{
		m_points = std::move(points);
		m_nextIndex = 0;
		m_goal = m_points.empty() ? Vector3() : m_points.back();

		// The mark's position is only known on the first Advance; the window starts now.
		m_hasProgressMark = false;
		m_progressMarkTime = now;
	}

	void BotPathFollower::Clear()
	{
		m_points.clear();
		m_nextIndex = 0;
		m_hasProgressMark = false;
	}

	BotPathStep BotPathFollower::Advance(const Vector3& position, const GameTime now)
	{
		BotPathStep step;

		while (m_nextIndex < m_points.size() && PlanarDistance(position, m_points[m_nextIndex]) <= WaypointAcceptance)
		{
			++m_nextIndex;
		}

		// A unit that cut a corner can stand at the goal without having touched every waypoint.
		if (m_nextIndex >= m_points.size() || PlanarDistance(position, m_goal) <= WaypointAcceptance)
		{
			Clear();
			step.kind = BotPathStep::Finished;
			return step;
		}

		if (!m_hasProgressMark || PlanarDistance(position, m_progressMark) >= StuckDistance)
		{
			if (m_hasProgressMark)
			{
				m_progressMarkTime = now;
			}

			m_hasProgressMark = true;
			m_progressMark = position;
		}
		else if (now - m_progressMarkTime >= StuckWindowMs)
		{
			Clear();
			step.kind = BotPathStep::Stuck;
			return step;
		}

		step.kind = BotPathStep::Steer;
		step.steerTarget = m_points[m_nextIndex];
		return step;
	}
}
