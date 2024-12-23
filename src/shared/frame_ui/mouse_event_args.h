// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "point.h"

#include "base/typedefs.h"


namespace mmo
{
	/// Enumerates possible mouse button flags.
	enum MouseButton : uint8
	{
		None,

		Left = 1,
		Right = 2,
		Middle = 4,

		Button4 = 8,
		Button5 = 16
	};

	/// This class contains infos about mouse event args.
	class MouseEventArgs final
	{
	public:
		explicit MouseEventArgs(int32 buttons, int32 x, int32 y)
			: m_buttons(buttons)
			, m_x(x)
			, m_y(y)
		{
		}

	public:
		/// Determines whether a mouse button is pressed.
		inline bool IsButtonPressed(MouseButton button) const { return (m_buttons & static_cast<int32>(button)) != 0; }
		/// Gets the pressed buttons as mask.
		inline int32 GetButtons() const { return m_buttons; }
		/// Gets the X coordinate of the mouse cursor.
		inline int32 GetX() const { return m_x; }
		/// Gets the Y coordinate of the mouse cursor.
		inline int32 GetY() const { return m_y; }
		/// Gets the mouse posision as a Point object for syntactic sugar.
		inline Point GetPosition() const { return Point(m_x, m_y); }

	private:
		int32 m_buttons;
		int32 m_x;
		int32 m_y;
	};
}
