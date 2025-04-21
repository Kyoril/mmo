// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/timer_queue.h"
#include "base/countdown.h"
#include "math/vector3.h"
#include "game/movement_info.h"
#include "game/movement_path.h"
#include "base/signal.h"

namespace mmo
{
	class GameUnitS;
	class TileSubscriber;
	struct IShape;

	/// This class is meant to control a units movement. This class should be
	/// inherited, so that, for example, a player character will be controlled
	/// by network input, while a creature is controlled by AI input.
	class UnitMover
	{
	public:

		static const GameTime UpdateFrequency;

	public:

		/// Fired when the unit reached it's target.
		signal<void()> targetReached;

		/// Fired when the movement was stopped.
		signal<void()> movementStopped;

		/// Fired when the target changed.
		signal<void()> targetChanged;

	public:

		/// 
		explicit UnitMover(GameUnitS& unit);
		/// 
		virtual ~UnitMover();

		/// Called when the units movement speed changed.
		void OnMoveSpeedChanged(MovementType moveType);

		/// Moves this unit to a specific location if possible. This does not teleport
		/// the unit, but makes it walk / fly / swim to the target.
		bool MoveTo(const Vector3& target, const Radian* targetFacing = nullptr, const IShape* clipping = nullptr);

		/// Moves this unit to a specific location if possible. This does not teleport
		/// the unit, but makes it walk / fly / swim to the target.
		bool MoveTo(const Vector3& target, float customSpeed, const Radian* targetFacing = nullptr, const IShape* clipping = nullptr);

		/// Stops the current movement if any.
		void StopMovement();

		/// Gets the new movement target.
		const Vector3& GetTarget() const
		{
			return m_target;
		}

		/// 
		GameUnitS& GetMoved() const
		{
			return m_unit;
		}

		/// 
		bool IsMoving() const
		{
			return m_moveReached.IsRunning();
		}

		/// 
		Vector3 GetCurrentLocation() const;

		/// Enables or disables debug output of generated waypoints and other events.
		void SetDebugOutput(bool enable) { m_debugOutputEnabled = enable; }

	public:

		/// Writes the current movement packets to a writer object. This is used for moving
		/// creatures that are spawned for a player.
		void SendMovementPackets(TileSubscriber& subscriber);

	private:

		GameUnitS& m_unit;
		Countdown m_moveReached, m_moveUpdated;
		Vector3 m_start, m_target;
		GameTime m_moveStart, m_moveEnd;
		bool m_customSpeed;
		bool m_debugOutputEnabled;
		MovementPath m_path;
		std::optional<Radian> m_customFacing;
	};
}
