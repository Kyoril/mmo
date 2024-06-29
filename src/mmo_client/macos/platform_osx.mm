// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "platform_osx.h"

namespace mmo
{
	static bool s_mouseCaptured = false;
	static int s_mouseCursorHiddenCount = 0;
	static int s_mouseCaptureX = 0;
	static int s_mouseCaptureY = 0;

	void PlatformOsX::CaptureMouse()
	{
		if (s_mouseCaptured) return;

        // TODO

		s_mouseCaptured = true;
	}

	void PlatformOsX::ReleaseMouseCapture()
	{
		if (!s_mouseCaptured) return;

        // TODO
        
		s_mouseCaptured = false;
	}

	void PlatformOsX::ShowCursor()
	{
		if (--s_mouseCursorHiddenCount <= 0)
		{
			s_mouseCursorHiddenCount = 0;
            
            // TODO
            
		}
	}

	void PlatformOsX::HideCursor()
	{
		if (++s_mouseCursorHiddenCount == 1)
		{
            // TODO
            
		}
	}
}
