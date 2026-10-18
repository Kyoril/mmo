// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <cstddef>
#include <vector>

namespace mmo
{
	/// @brief What the bot's movement driver should do this frame.
	struct BotPathStep final
	{
		enum Kind
		{
			/// Keep moving towards steerTarget.
			Steer,
			/// The last waypoint was reached; the path is done.
			Finished,
			/// No progress for too long; the unit is blocked.
			Stuck
		};

		Kind kind { Finished };

		/// The waypoint to head for. Only meaningful for Steer.
		Vector3 steerTarget {};
	};

	/// @brief Walks a nav mesh path waypoint by waypoint and notices when the unit stops making
	///        progress along it. Pure logic: it knows nothing about units, input or the network.
	/// @details Distances are measured in the XZ plane. Nav waypoints sit on the nav mesh, which
	///          floats a little above or below the rendered ground, so comparing heights would
	///          keep a unit standing on a waypoint from ever reaching it.
	class BotPathFollower final
	{
	public:
		/// A waypoint closer than this (yards) counts as reached.
		static constexpr float WaypointAcceptance = 1.0f;

		/// Less movement than this (yards) within StuckWindowMs counts as no progress.
		static constexpr float StuckDistance = 0.5f;

		/// How long (milliseconds) the unit may fail to make progress before it is stuck.
		static constexpr GameTime StuckWindowMs = 2000;

	public:
		/// @brief Starts following a new path, replacing any previous one.
		/// @param points The waypoints, in walking order.
		/// @param now The current time, which starts the first stuck window.
		void Start(std::vector<Vector3> points, GameTime now);

		/// @brief Drops the current path.
		void Clear();

		/// @brief Whether a path is being followed.
		[[nodiscard]] bool IsActive() const { return m_nextIndex < m_points.size(); }

		/// @brief The last waypoint of the current (or the most recent) path.
		[[nodiscard]] const Vector3& GetGoal() const { return m_goal; }

		/// @brief Advances along the path from the unit's current position.
		/// @details Skips every waypoint already within reach, and finishes once the goal is.
		///          Finished and Stuck both end the path; the caller decides what to do next.
		/// @param position The unit's current position.
		/// @param now The current time.
		[[nodiscard]] BotPathStep Advance(const Vector3& position, GameTime now);

	private:
		std::vector<Vector3> m_points;
		std::size_t m_nextIndex { 0 };
		Vector3 m_goal {};

		bool m_hasProgressMark { false };
		Vector3 m_progressMark {};
		GameTime m_progressMarkTime { 0 };
	};
}
