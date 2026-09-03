
#pragma once

#include <string>
#include <utility>

namespace mmo
{
	class PlatformOsX
	{
	public:
		static void CaptureMouse();

		static void ReleaseMouseCapture();

        static void ResetCursorPosition();

		static void ShowCursor();

		static void HideCursor();
        
        static bool IsMouseCaptured();

        static void GetCapturedMousePosition(int& x, int& y);

        static void GetCursorPos(int& x, int& y);

        static void SetCursorPos(int x, int y);

		static bool IsShiftKeyDown();

		/// Returns the primary display resolution in pixels as {width, height}.
		static std::pair<int, int> GetPrimaryDisplayResolution();

		/// Shows a modal error dialog to the user and returns once it has been dismissed.
		/// @remarks Used for startup failures that the player can act on, so that these do not end
		///          up as crash reports which nobody can do anything about.
		/// @param title Caption of the dialog.
		/// @param message Body text of the dialog.
		static void ShowErrorDialog(const std::string& title, const std::string& message);
	};
}
