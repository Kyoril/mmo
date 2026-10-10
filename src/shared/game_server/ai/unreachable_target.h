// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	/// @brief Tells a chasing creature when its victim has been out of its reach for too long.
	///
	/// Paths are allowed to be partial: when the victim stands where the navigation mesh does not
	/// lead (on a ledge, behind a gap, on another floor), the path ends as close as it gets and the
	/// creature walks there and waits. That counts as a successful move, so without this the
	/// creature would wait there forever - stuck on the edge of its area, and an easy target for
	/// ranged attacks. Once the victim has been unreachable for EvadeAfter, the creature evades.
	class UnreachableTargetTracker final
	{
	public:
		/// @brief How long a victim may stay unreachable before the creature evades.
		static constexpr GameTime EvadeAfter = 5000;

		/// @brief Slack beyond attack range before a path counts as ending short of the victim. A full
		/// path ends inside attack range (on the standoff ring, trimmed by the acceptance radius).
		static constexpr float Slack = 0.5f;

	public:
		/// @brief Checks whether a chase path ends short of its victim.
		///
		/// The path leads to a slot on a ring around where the victim is predicted to be, so for a
		/// victim on the run a complete path can end farther than attack range from where it stands
		/// now. Only a path that neither reaches its slot nor ends within attack range of the victim
		/// counts as short. Distances are flat, like melee range.
		///
		/// @param pathEnd Where the creature's path ends.
		/// @param slot Where the path was asked to lead.
		/// @param acceptance Acceptance radius the path end was trimmed by.
		/// @param victim Where the victim stands.
		/// @param attackRange The creature's attack range against the victim.
		/// @return True if the path ends short of both the slot and the victim.
		static bool EndsShortOfVictim(const Vector3& pathEnd, const Vector3& slot, const float acceptance, const Vector3& victim, const float attackRange)
		{
			return !IsFlatWithin(pathEnd, slot, acceptance + Slack) && !IsFlatWithin(pathEnd, victim, attackRange + Slack);
		}

		/// @brief Records the outcome of one chase move.
		/// @param unreachable Whether the victim could not be reached this time.
		/// @param now Current time.
		void Update(const bool unreachable, const GameTime now)
		{
			if (!unreachable)
			{
				m_unreachableSince = 0;
			}
			else if (m_unreachableSince == 0)
			{
				m_unreachableSince = now;
			}
		}

		/// @brief Whether the victim has been unreachable for EvadeAfter.
		/// @param now Current time.
		/// @return True if the creature should evade.
		bool ShouldEvade(const GameTime now) const
		{
			return m_unreachableSince != 0 && now >= m_unreachableSince + EvadeAfter;
		}

		/// @brief Forgets any unreachable streak, e.g. for a new victim.
		void Reset()
		{
			m_unreachableSince = 0;
		}

	private:
		static bool IsFlatWithin(const Vector3& a, const Vector3& b, const float distance)
		{
			const float dx = a.x - b.x;
			const float dz = a.z - b.z;
			return dx * dx + dz * dz <= distance * distance;
		}

	private:
		GameTime m_unreachableSince { 0 };
	};
}
