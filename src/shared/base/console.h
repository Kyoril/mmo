// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "typedefs.h"

namespace mmo
{
	/// Handles console specific things.
	class Console final
	{
	public:

		/// Enumerates possible console color values used as foreground
		/// and background color.
		enum Color
		{
			Black,
			DarkBlue,
			DarkGreen,
			DarkCyan,
			DarkRed,
			DarkMagenta,
			DarkBrown,
			LightGray,
			DarkGray,
			Blue,
			Green,
			Cyan,
			Red,
			Magenta,
			Yellow,
			White
		};

	public:

		/// Gets the active foreground color of the console, if the
		/// operating system supports this.
		static Color getTextColor();
		/// Gets the active background color of the console, if the
		/// operating system supports this.
		static Color getBackgroundColor();
		/// Sets the active foreground color of the console, if the
		/// operating system supports this.
		static void setTextColor(Color id);
		/// Sets the active background color of the console, if the
		/// operating system supports this.
		static void setBackgroundColor(Color id);
	};
}
