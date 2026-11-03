// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "platform_osx.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

namespace mmo
{
	namespace
	{
		bool s_mouseCaptured = false;
		int s_mouseCursorHiddenCount = 0;
		int s_mouseCaptureX = 0;
		int s_mouseCaptureY = 0;

		/// Position of the virtual cursor while the mouse is captured. Kept in double precision
		/// because trackpads report fractions of a point.
		double s_virtualCursorX = 0.0;
		double s_virtualCursorY = 0.0;

		void GetGlobalCursorPosition(int& x, int& y)
		{
			CGEventRef event = CGEventCreate(nullptr);
			const CGPoint location = CGEventGetLocation(event);
			CFRelease(event);

			x = static_cast<int>(location.x);
			y = static_cast<int>(location.y);
		}
	}

	void PlatformOsX::CaptureMouse()
	{
		if (s_mouseCaptured)
		{
			return;
		}

		GetGlobalCursorPosition(s_mouseCaptureX, s_mouseCaptureY);
		s_virtualCursorX = s_mouseCaptureX;
		s_virtualCursorY = s_mouseCaptureY;

		HideCursor();
		CGAssociateMouseAndMouseCursorPosition(false);
		s_mouseCaptured = true;
	}

	void PlatformOsX::ReleaseMouseCapture()
	{
		if (!s_mouseCaptured)
		{
			return;
		}

		CGAssociateMouseAndMouseCursorPosition(true);
		ShowCursor();
		s_mouseCaptured = false;
	}

	void PlatformOsX::ResetCursorPosition()
	{
		s_virtualCursorX = s_mouseCaptureX;
		s_virtualCursorY = s_mouseCaptureY;
	}

	void PlatformOsX::AddCapturedMouseDelta(const double deltaX, const double deltaY)
	{
		if (s_mouseCaptured)
		{
			s_virtualCursorX += deltaX;
			s_virtualCursorY += deltaY;
		}
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
		return s_mouseCaptured;
	}

	void PlatformOsX::GetCapturedMousePosition(int& x, int& y)
	{
		if (!s_mouseCaptured)
		{
			x = -1;
			y = -1;
			return;
		}

		x = s_mouseCaptureX;
		y = s_mouseCaptureY;
	}

	void PlatformOsX::GetCursorPos(int& x, int& y)
	{
		if (s_mouseCaptured)
		{
			x = static_cast<int>(s_virtualCursorX);
			y = static_cast<int>(s_virtualCursorY);
			return;
		}

		GetGlobalCursorPosition(x, y);
	}

	void PlatformOsX::SetCursorPos(const int x, const int y)
	{
		if (s_mouseCaptured)
		{
			s_virtualCursorX = x;
			s_virtualCursorY = y;
			return;
		}

		CGWarpMouseCursorPosition(CGPointMake(x, y));
		// Without this, macOS ignores mouse movement for a moment after the warp.
		CGAssociateMouseAndMouseCursorPosition(true);
	}

	bool PlatformOsX::IsShiftKeyDown()
	{
		return ([NSEvent modifierFlags] & NSEventModifierFlagShift) != 0;
	}

	bool PlatformOsX::IsAltKeyDown()
	{
		return ([NSEvent modifierFlags] & NSEventModifierFlagOption) != 0;
	}

	bool PlatformOsX::IsControlKeyDown()
	{
		// Command is reported as control so the usual macOS shortcuts (Cmd+C, Cmd+V) work.
		return ([NSEvent modifierFlags] & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) != 0;
	}

	std::pair<int, int> PlatformOsX::GetPrimaryDisplayResolution()
	{
		const NSRect frame = [[NSScreen mainScreen] frame];
		return { static_cast<int>(frame.size.width), static_cast<int>(frame.size.height) };
	}

	void PlatformOsX::ShowErrorDialog(const std::string& title, const std::string& message)
	{
		@autoreleasepool
		{
			NSAlert* alert = [[NSAlert alloc] init];
			[alert setAlertStyle:NSAlertStyleCritical];
			[alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
			[alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
			[alert addButtonWithTitle:@"OK"];
			[alert runModal];
		}
	}
}
