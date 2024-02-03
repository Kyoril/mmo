
#include "movement_path.h"
#include "base/macros.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace
	{
		Vector3 DoInterpolation(MovementPath::Timestamp timestamp, const MovementPath::PositionMap& map)
		{
			// No data! What should we do? ...
			if (map.empty())
			{
				return {};
			}

			// Check if timestamp is less than the end point
			auto endIt = map.rbegin();
			if (endIt->first < timestamp)
				return endIt->second;

			// Get start and end iterator
			typename MovementPath::PositionMap::const_iterator t1 = map.end(), t2 = map.end();

			// Get first position and check if it is ahead in time or just the right time
			typename MovementPath::PositionMap::const_iterator it = map.begin();
			if (it->first >= timestamp)
				return it->second;

			MovementPath::Timestamp startTime = 0;

			// Iterate through all saved positions
			while (it != map.end())
			{
				// Check if this position lies in the past
				if (it->first < timestamp &&
					it->first > startTime)
				{
					t1 = it;
					startTime = it->first;
				}

				// If we have a valid start point, and don't have an end point yet, we can
				// use it as the target point
				if (t1 != map.end() &&
					t2 == map.end())
				{
					if (it->first >= timestamp)
					{
						t2 = it;
						break;
					}
				}

				// Next
				it++;
			}

			// By now we should have a valid start value
			ASSERT(t1 != map.end());
			ASSERT(t2 != map.end());

			// Normalize end time value (end - start) for a range of 0 to END
			MovementPath::Timestamp nEnd = static_cast<MovementPath::Timestamp>(t2->first - t1->first);
			ASSERT(nEnd != 0);

			// Determine normalized position
			MovementPath::Timestamp nPos = static_cast<MovementPath::Timestamp>(timestamp - t1->first);

			// Interpolate between the two values
			return t1->second.Lerp(t2->second,
				static_cast<double>(nPos) / static_cast<double>(nEnd));
		}
	}

	MovementPath::MovementPath()
		: m_firstPosTimestamp(std::numeric_limits<Timestamp>::max())	// first value will definitly be smaller
		, m_lastPosTimestamp(0)	// use the minimum value of course.
	{
		Vector3 v;
		(v, v, 0.5f);
	}

	MovementPath::~MovementPath()
	{
	}

	void MovementPath::Clear()
	{
		m_position.clear();
		m_firstPosTimestamp = std::numeric_limits<Timestamp>::max();
		m_lastPosTimestamp = 0;
	}

	void MovementPath::AddPosition(Timestamp timestamp, const Vector3& position)
	{
		// Save some min/max values
		if (timestamp < m_firstPosTimestamp)
			m_firstPosTimestamp = timestamp;
		if (timestamp > m_lastPosTimestamp)
			m_lastPosTimestamp = timestamp;

		ASSERT(m_lastPosTimestamp >= m_firstPosTimestamp);

		// Store position info
		m_position[timestamp] = position;
	}

	Vector3 MovementPath::GetPosition(Timestamp timestamp) const
	{
		// Interpolate between the two values
		return DoInterpolation(timestamp, m_position);
	}

	void MovementPath::PrintDebugInfo()
	{
		DLOG("MovementPath Debug Info");
		{
			DLOG("\tPosition Elements:\t" << m_position.size());
			Timestamp prev = 0;
			for (auto it : m_position)
			{
				Timestamp diff = 0;
				if (prev > 0) diff = it.first - prev;
				prev = it.first;

				DLOG("\t\t" << std::setw(5) << it.first << ": " << it.second << " [Duration: " << diff << "]");
			}
		}
	}
}
