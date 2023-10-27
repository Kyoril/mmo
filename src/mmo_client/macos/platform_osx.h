
#pragma once

namespace mmo
{
	class PlatformOsX
	{
	public:
		static void CaptureMouse();

		static void ReleaseMouseCapture();

		static void ShowCursor();

		static void HideCursor();
	};
}
