
#pragma once

#include <map>

#include "base/typedefs.h"
#include "math/vector3.h"

namespace mmo
{
	class MovementPath
	{
	public:

		typedef GameTime Timestamp;
		/// Maps a position vector to a timestamp.
		typedef std::map<Timestamp, Vector3> PositionMap;

	public:

		/// Default constructor.
		MovementPath();

		/// Destructor.
		virtual ~MovementPath();

		/// Clears the current movement path, deleting all informations.
		void Clear();

		/// Determines whether some positions have been given to this path.
		virtual bool HasPositions() const { return !m_position.empty(); }

		/// Adds a new position to the path and assigns it to a specific timestamp.
		/// @param timestamp The timestamp of when the position will be or was reached.
		/// @param position The position at the given time.
		virtual void AddPosition(Timestamp timestamp, const Vector3& position);

		/// Calculates the units position on the path based on the given timestamp.
		/// @param timestamp The timestamp.
		/// @returns The position of the unit at the given timestamp or a null vector 
		///          if this is an empty path.
		virtual Vector3 GetPosition(Timestamp timestamp) const;

		/// 
		virtual const PositionMap& GetPositions() const { return m_position; }

		/// Prints some debug infos to the log.
		virtual void PrintDebugInfo();

	private:

		/// Stored position values with timestamp information.
		PositionMap m_position;

		/// The first position timestamp information stored.
		Timestamp m_firstPosTimestamp;

		/// The last position timestamp information stored.
		Timestamp m_lastPosTimestamp;
	};
}
