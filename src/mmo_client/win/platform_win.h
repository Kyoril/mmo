
#pragma once

namespace mmo
{
	class PlatformWin
	{
	public:
		static void CaptureMouse();

		static void ReleaseMouseCapture();

		static void ResetCursorPosition();

		static void ShowCursor();

		static void HideCursor();
	};
}
