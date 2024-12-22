
#pragma once

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
	};
}
