#pragma once

#include "base/typedefs.h"
#include "math/radian.h"
#include "math/vector3.h"

namespace mmo
{
	namespace movement_type
	{
		enum Type
		{
			Walk = 0,
			Run = 1,
			Backwards = 2,
			Swim = 3,
			SwimBackwards = 4,
			Turn = 5,
			Flight = 6,
			FlightBackwards = 7,

			Count = 8
		};
	}

	typedef movement_type::Type MovementType;

	namespace MovementFlags
	{
		/// @brief Enumerates movement flags.
		enum Type
		{
			/// @brief No movement flags at all.
			None = 0,


			/// @brief Unit is moving forward.
			Forward = 1 << 0,

			/// @brief Unit is moving backward.
			Backward = 1 << 1,

			/// @brief Unit is moving strafe left.
			StrafeLeft = 1 << 2,

			/// @brief Unit is moving strafe right.
			StrafeRight = 1 << 3,

			/// @brief Unit is turning left.
			TurnLeft = 1 << 4,

			/// @brief Unit is turning right.
			TurnRight = 1 << 5,


			/// @brief Unit is pitching up.
			PitchUp = 1 << 6,

			/// @brief Unit is pitching down.
			PitchDown = 1 << 7,


			/// @brief Unit has walk mode enabled.
			WalkMode = 1 << 8,
			

			/// @brief Unit is rooted and can't move.
			Rooted = 1 << 10,


			/// @brief Unit is jumping, which means the unit is still moving upwards.
			Falling = 1 << 11,


			/// @brief A root is pending for the unit.
			PendingRoot = 1 << 12,

			/// @brief Unit is swimming.
			Swimming = 1 << 13,

			/// @brief Unit is moving straight upwards.
			Ascending = 1 << 14,

			/// @brief Unit is moving straight downwards.
			Descending = 1 << 15,


			/// @brief The unit is able to walk on liquids.
			WaterWalking = 1 << 16,

			/// @brief The unit is falling slowly.
			SlowFall = 1 << 17,

			/// @brief The unit is levitating in the air.
			Levitating = 1 << 18,

			/// @brief The unit is able to fly.
			CanFly = 1 << 19,

			/// @brief The unit is currently flying.
			Flying = 1 << 20,


			/// @brief Combined list of flags which imply that the character's position is changing.
			Moving =
				Forward | Backward | Ascending | Descending | Falling,

			Strafing =
				StrafeLeft | StrafeRight,

			/// @brief Combined list of flags which imply that the character's facing property is changing.
			Turning =
				TurnLeft | TurnRight,

			Pitching = 
				PitchUp | PitchDown,
		};
	}
	

	/// @brief Class which contains a snapshot of a units movement info.
	class MovementInfo final
	{
	public:
		/// @brief Movement flags.
		uint32 movementFlags { MovementFlags::None };

		/// @brief Timestamp of this movement info snapshot.
		GameTime timestamp { 0 };

		/// @brief The position of the unit.
		Vector3 position;

		/// @brief The unit's facing value.
		Radian facing;

		/// @brief The unit's pitch value.
		Radian pitch;

		/// @brief The amount of game time that the unit is falling.
		GameTime fallTime { 0 };

		/// @brief The unit's current jump velocity (velocity upwards the Y axis, reduced over time by gravity).
		float jumpVelocity { 0.0f };

		/// @brief The units sin angle.
		float jumpSinAngle { 0.0f };

		/// @brief The units cos angle.
		float jumpCosAngle { 0.0f };

		/// @brief The units horizontal jump speed in units per seconds.
		float jumpXZSpeed { 0.0f };

	public:
		bool IsMoving() const noexcept { return (movementFlags & MovementFlags::Moving) != 0; }

		bool IsStrafing() const noexcept { return (movementFlags & MovementFlags::Strafing) != 0; }

		bool IsTurning() const noexcept { return (movementFlags & MovementFlags::Turning) != 0; }

		bool IsPitching() const noexcept { return (movementFlags & MovementFlags::Pitching) != 0; }
	};

	inline io::Writer& operator<<(io::Writer& writer, const MovementInfo& info)
	{
		writer
			<< io::write<uint32>(info.movementFlags)
			<< io::write<uint64>(info.timestamp)
			<< info.position
			<< info.facing;
		
		if (info.movementFlags & (MovementFlags::Swimming | MovementFlags::Flying))
		{
			writer << info.pitch;
		}

		writer << io::write<uint64>(info.fallTime);

		if (info.movementFlags & MovementFlags::Falling)
		{
			writer
				<< io::write<float>(info.jumpVelocity)
				<< io::write<float>(info.jumpSinAngle)
				<< io::write<float>(info.jumpCosAngle)
				<< io::write<float>(info.jumpXZSpeed);
		}

		return writer;
	}

	inline io::Reader& operator>>(io::Reader& reader, MovementInfo& info)
	{
		reader
			>> io::read<uint32>(info.movementFlags)
			>> io::read<uint64>(info.timestamp)
			>> info.position
			>> info.facing;

		if (info.movementFlags & (MovementFlags::Swimming | MovementFlags::Flying))
		{
			reader >> info.pitch;
		}

		reader >> io::read<uint64>(info.fallTime);

		if (info.movementFlags & MovementFlags::Falling)
		{
			reader
				>> io::read<float>(info.jumpVelocity)
				>> io::read<float>(info.jumpSinAngle)
				>> io::read<float>(info.jumpCosAngle)
				>> io::read<float>(info.jumpXZSpeed);
		}

		return reader;
	}
}
