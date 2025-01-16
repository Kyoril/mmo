// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	namespace ControlFlags
	{
		enum Type 
		{
			None,

			TurnPlayer = 1 << 0,
			TurnCamera = 1 << 1,
			MovePlayerOrTurnCamera = 1 << 2,
			MoveForwardKey = 1 << 3,
			MoveBackwardKey = 1 << 4,
			StrafeLeftKey = 1 << 5,
			StrafeRightKey = 1 << 6,
			TurnLeftKey = 1 << 7,
			TurnRightKey = 1 << 8,
			PitchUpKey = 1 << 9,
			PitchDownKey = 1 << 10,
			Autorun = 1 << 11,

			MoveSent = 1 << 12,
			StrafeSent = 1 << 13,
			TurnSent = 1 << 14,
			PitchSent = 1 << 15,

			MovePlayer = MoveForwardKey | MoveBackwardKey | StrafeLeftKey | StrafeRightKey,
			MoveAndTurnPlayer = TurnPlayer | TurnCamera
		};
	}

	class IInputControl
	{
	public:
		virtual ~IInputControl() = default;

	public:
		virtual void SetControlBit(ControlFlags::Type flag, bool set) = 0;

		virtual void ToggleControlBit(const ControlFlags::Type flag) = 0;

		virtual void Jump() = 0;
	};
}
