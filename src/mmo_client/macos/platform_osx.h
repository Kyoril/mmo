// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <utility>

namespace mmo
{
	/// @brief macOS implementation of the platform functions the client uses.
	/// @remarks Cursor positions are in global display coordinates with the origin in the top left
	///          corner of the main display, like on Windows.
	class PlatformOsX
	{
	public:
		/// Hides the cursor and freezes it in place; mouse movement is reported as a virtual
		/// cursor position instead (see GetCursorPos) so camera turning never hits a screen edge.
		static void CaptureMouse();

		static void ReleaseMouseCapture();

		/// Moves the virtual cursor back to where the capture started.
		static void ResetCursorPosition();

		static void ShowCursor();

		static void HideCursor();

		static bool IsMouseCaptured();

		static void GetCapturedMousePosition(int& x, int& y);

		static void GetCursorPos(int& x, int& y);

		static void SetCursorPos(int x, int y);

		static bool IsShiftKeyDown();

		static bool IsAltKeyDown();

		static bool IsControlKeyDown();

		static std::pair<int, int> GetPrimaryDisplayResolution();

		static void ShowErrorDialog(const std::string& title, const std::string& message);

		/// Called by the event loop with the raw movement of the mouse while it is captured.
		static void AddCapturedMouseDelta(double deltaX, double deltaY);
	};
}
