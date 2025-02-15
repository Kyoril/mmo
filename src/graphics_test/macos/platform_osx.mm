// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "platform_osx.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

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

    void PlatformOsX::ResetCursorPosition()
    {
        
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
            
            [NSCursor unhide];
		}
	}

	void PlatformOsX::HideCursor()
	{
		if (++s_mouseCursorHiddenCount == 1)
		{
            [NSCursor hide];
		}
	}

    bool PlatformOsX::IsMouseCaptured()
    {
        return false;
    }

    void PlatformOsX::GetCapturedMousePosition(int& x, int& y)
    {
        
    }

    void PlatformOsX::GetCursorPos(int& x, int& y)
    {
        // Get the mouse location in screen coordinates
        NSPoint mouseLocation = [NSEvent mouseLocation];
        
        // Convert the NSPoint to a CGPoint (if needed)
        x = mouseLocation.x;
        y = mouseLocation.y;
    }

    void PlatformOsX::SetCursorPos(int x, int y)
    {
        // Warp the cursor to the specified position
        CGPoint position;
        position.x = x;
        position.y = y;
        CGWarpMouseCursorPosition(position);

        // Optional: To reflect the new position without any inertia
        CGAssociateMouseAndMouseCursorPosition(true);
    }
}
