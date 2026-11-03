// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "event_loop.h"
#include "platform_osx.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#include <cmath>
#include <unordered_set>

// Part of the Objective-C runtime ABI; used to give every frame its own autorelease pool.
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);

namespace mmo
{
	namespace
	{
		// Windows virtual key codes, which is what the game's key handling works with.
		constexpr int32 VK_BACK = 0x08;
		constexpr int32 VK_TAB = 0x09;
		constexpr int32 VK_RETURN = 0x0D;
		constexpr int32 VK_SHIFT = 0x10;
		constexpr int32 VK_CONTROL = 0x11;
		constexpr int32 VK_MENU = 0x12;
		constexpr int32 VK_CAPITAL = 0x14;
		constexpr int32 VK_ESCAPE = 0x1B;
		constexpr int32 VK_SPACE = 0x20;
		constexpr int32 VK_PRIOR = 0x21;
		constexpr int32 VK_NEXT = 0x22;
		constexpr int32 VK_END = 0x23;
		constexpr int32 VK_HOME = 0x24;
		constexpr int32 VK_LEFT = 0x25;
		constexpr int32 VK_UP = 0x26;
		constexpr int32 VK_RIGHT = 0x27;
		constexpr int32 VK_DOWN = 0x28;
		constexpr int32 VK_INSERT = 0x2D;
		constexpr int32 VK_DELETE = 0x2E;
		constexpr int32 VK_NUMPAD0 = 0x60;
		constexpr int32 VK_MULTIPLY = 0x6A;
		constexpr int32 VK_ADD = 0x6B;
		constexpr int32 VK_SUBTRACT = 0x6D;
		constexpr int32 VK_DECIMAL = 0x6E;
		constexpr int32 VK_DIVIDE = 0x6F;
		constexpr int32 VK_F1 = 0x70;
		constexpr int32 VK_OEM_1 = 0xBA;
		constexpr int32 VK_OEM_PLUS = 0xBB;
		constexpr int32 VK_OEM_COMMA = 0xBC;
		constexpr int32 VK_OEM_MINUS = 0xBD;
		constexpr int32 VK_OEM_PERIOD = 0xBE;
		constexpr int32 VK_OEM_2 = 0xBF;
		constexpr int32 VK_OEM_3 = 0xC0;
		constexpr int32 VK_OEM_4 = 0xDB;
		constexpr int32 VK_OEM_5 = 0xDC;
		constexpr int32 VK_OEM_6 = 0xDD;
		constexpr int32 VK_OEM_7 = 0xDE;

		bool s_quitRequested = false;
		void* s_framePool = nullptr;

		/// Keys reported as pressed, so they can be released when the window loses focus (macOS
		/// does not deliver the key up to an inactive application).
		std::unordered_set<int32> s_keysDown;

		/// Scroll distance not yet reported, since trackpads scroll in tiny fractions.
		double s_scrollAccumulator = 0.0;

		/// Translates a hardware key code into a Windows virtual key code. Letters come from the
		/// keyboard layout so that e.g. Z on a German keyboard is reported as Z.
		int32 TranslateKey(NSEvent* event)
		{
			const unsigned short keyCode = [event keyCode];
			switch (keyCode)
			{
			case kVK_Return: return VK_RETURN;
			case kVK_ANSI_KeypadEnter: return VK_RETURN;
			case kVK_Tab: return VK_TAB;
			case kVK_Space: return VK_SPACE;
			case kVK_Delete: return VK_BACK;
			case kVK_Escape: return VK_ESCAPE;
			case kVK_ForwardDelete: return VK_DELETE;
			case kVK_Help: return VK_INSERT;
			case kVK_Home: return VK_HOME;
			case kVK_End: return VK_END;
			case kVK_PageUp: return VK_PRIOR;
			case kVK_PageDown: return VK_NEXT;
			case kVK_LeftArrow: return VK_LEFT;
			case kVK_RightArrow: return VK_RIGHT;
			case kVK_UpArrow: return VK_UP;
			case kVK_DownArrow: return VK_DOWN;
			case kVK_F1: return VK_F1;
			case kVK_F2: return VK_F1 + 1;
			case kVK_F3: return VK_F1 + 2;
			case kVK_F4: return VK_F1 + 3;
			case kVK_F5: return VK_F1 + 4;
			case kVK_F6: return VK_F1 + 5;
			case kVK_F7: return VK_F1 + 6;
			case kVK_F8: return VK_F1 + 7;
			case kVK_F9: return VK_F1 + 8;
			case kVK_F10: return VK_F1 + 9;
			case kVK_F11: return VK_F1 + 10;
			case kVK_F12: return VK_F1 + 11;
			case kVK_ANSI_Keypad0: return VK_NUMPAD0;
			case kVK_ANSI_Keypad1: return VK_NUMPAD0 + 1;
			case kVK_ANSI_Keypad2: return VK_NUMPAD0 + 2;
			case kVK_ANSI_Keypad3: return VK_NUMPAD0 + 3;
			case kVK_ANSI_Keypad4: return VK_NUMPAD0 + 4;
			case kVK_ANSI_Keypad5: return VK_NUMPAD0 + 5;
			case kVK_ANSI_Keypad6: return VK_NUMPAD0 + 6;
			case kVK_ANSI_Keypad7: return VK_NUMPAD0 + 7;
			case kVK_ANSI_Keypad8: return VK_NUMPAD0 + 8;
			case kVK_ANSI_Keypad9: return VK_NUMPAD0 + 9;
			case kVK_ANSI_KeypadMultiply: return VK_MULTIPLY;
			case kVK_ANSI_KeypadPlus: return VK_ADD;
			case kVK_ANSI_KeypadMinus: return VK_SUBTRACT;
			case kVK_ANSI_KeypadDecimal: return VK_DECIMAL;
			case kVK_ANSI_KeypadDivide: return VK_DIVIDE;
			case kVK_ANSI_1: return '1';
			case kVK_ANSI_2: return '2';
			case kVK_ANSI_3: return '3';
			case kVK_ANSI_4: return '4';
			case kVK_ANSI_5: return '5';
			case kVK_ANSI_6: return '6';
			case kVK_ANSI_7: return '7';
			case kVK_ANSI_8: return '8';
			case kVK_ANSI_9: return '9';
			case kVK_ANSI_0: return '0';
			case kVK_ANSI_Minus: return VK_OEM_MINUS;
			case kVK_ANSI_Equal: return VK_OEM_PLUS;
			case kVK_ANSI_LeftBracket: return VK_OEM_4;
			case kVK_ANSI_RightBracket: return VK_OEM_6;
			case kVK_ANSI_Backslash: return VK_OEM_5;
			case kVK_ANSI_Semicolon: return VK_OEM_1;
			case kVK_ANSI_Quote: return VK_OEM_7;
			case kVK_ANSI_Comma: return VK_OEM_COMMA;
			case kVK_ANSI_Period: return VK_OEM_PERIOD;
			case kVK_ANSI_Slash: return VK_OEM_2;
			case kVK_ANSI_Grave: return VK_OEM_3;
			// The key left of 1 on ISO keyboards (^ on German ones), which Windows reports as OEM_5.
			case kVK_ISO_Section: return VK_OEM_5;
			default:
				break;
			}

			NSString* characters = [event charactersIgnoringModifiers];
			if ([characters length] == 1)
			{
				const unichar c = [[characters uppercaseString] characterAtIndex:0];
				if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
				{
					return c;
				}
			}

			return 0;
		}

		void FireKeyDown(const int32 key, const bool repeat)
		{
			if (key == 0)
			{
				return;
			}

			s_keysDown.insert(key);
			EventLoop::KeyDown(key, repeat);
		}

		void FireKeyUp(const int32 key)
		{
			if (key == 0)
			{
				return;
			}

			s_keysDown.erase(key);
			EventLoop::KeyUp(key);
		}

		void ReleaseAllKeys()
		{
			const std::unordered_set<int32> keys = s_keysDown;
			for (const int32 key : keys)
			{
				FireKeyUp(key);
			}
		}

		/// Modifier keys arrive as flag changes rather than key events.
		void HandleFlagsChanged(NSEvent* event)
		{
			const NSEventModifierFlags flags = [event modifierFlags];
			int32 key = 0;
			bool down = false;
			switch ([event keyCode])
			{
			case kVK_Shift:
			case kVK_RightShift:
				key = VK_SHIFT;
				down = (flags & NSEventModifierFlagShift) != 0;
				break;
			case kVK_Control:
			case kVK_RightControl:
			case kVK_Command:
			case kVK_RightCommand:
				key = VK_CONTROL;
				down = (flags & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) != 0;
				break;
			case kVK_Option:
			case kVK_RightOption:
				key = VK_MENU;
				down = (flags & NSEventModifierFlagOption) != 0;
				break;
			case kVK_CapsLock:
				key = VK_CAPITAL;
				down = (flags & NSEventModifierFlagCapsLock) != 0;
				break;
			default:
				return;
			}

			const bool wasDown = s_keysDown.contains(key);
			if (down && !wasDown)
			{
				FireKeyDown(key, false);
			}
			else if (!down && wasDown)
			{
				FireKeyUp(key);
			}
		}

		void HandleKeyDown(NSEvent* event)
		{
			FireKeyDown(TranslateKey(event), [event isARepeat]);

			// Text input. Command shortcuts are not text; control combinations are passed on as the
			// control characters Windows reports for them (the text field expects those).
			if (([event modifierFlags] & NSEventModifierFlagCommand) != 0)
			{
				return;
			}

			NSString* characters = [event characters];
			for (NSUInteger i = 0; i < [characters length]; ++i)
			{
				unichar c = [characters characterAtIndex:i];
				if (c >= 0xF700 && c <= 0xF8FF)
				{
					// Function and cursor keys
					continue;
				}

				if (c == 0x7F)
				{
					c = VK_BACK;
				}
				else if (c == 0x03)
				{
					// Enter on the keypad
					c = VK_RETURN;
				}

				EventLoop::KeyChar(static_cast<uint16>(c));
			}
		}

		bool GetMousePosition(NSEvent* event, int32& x, int32& y)
		{
			NSWindow* window = [event window];
			if (!window)
			{
				window = [NSApp keyWindow];
			}

			NSView* view = [window contentView];
			if (!view)
			{
				return false;
			}

			NSPoint location = [event locationInWindow];
			if ([event window] != window)
			{
				location = [window convertPointFromScreen:[NSEvent mouseLocation]];
			}

			const NSPoint point = [view convertPoint:location fromView:nil];
			x = static_cast<int32>(point.x);
			y = static_cast<int32>([view bounds].size.height - point.y);
			return true;
		}

		EMouseButton TranslateButton(NSEvent* event)
		{
			switch ([event type])
			{
			case NSEventTypeLeftMouseDown:
			case NSEventTypeLeftMouseUp:
				// Control-click is the right click of one button mice and trackpads.
				return ([event modifierFlags] & NSEventModifierFlagControl) != 0 ? MouseButton_Right : MouseButton_Left;
			case NSEventTypeRightMouseDown:
			case NSEventTypeRightMouseUp:
				return MouseButton_Right;
			default:
				switch ([event buttonNumber])
				{
				case 3: return MouseButton_Four;
				case 4: return MouseButton_Five;
				default: return MouseButton_Middle;
				}
			}
		}

		/// The button a pressed left button was reported as, so its release matches it.
		EMouseButton s_leftButtonAs = MouseButton_Left;

		/// Returns whether the event may also be handed to AppKit.
		bool HandleEvent(NSEvent* event)
		{
			int32 x = 0;
			int32 y = 0;

			switch ([event type])
			{
			case NSEventTypeKeyDown:
				if (([event modifierFlags] & NSEventModifierFlagCommand) != 0 && [event keyCode] == kVK_ANSI_Q)
				{
					EventLoop::Terminate(0);
					return false;
				}

				HandleKeyDown(event);
				// AppKit would only beep for keys the view does not handle.
				return false;

			case NSEventTypeKeyUp:
				FireKeyUp(TranslateKey(event));
				return false;

			case NSEventTypeFlagsChanged:
				HandleFlagsChanged(event);
				return true;

			case NSEventTypeLeftMouseDown:
			case NSEventTypeRightMouseDown:
			case NSEventTypeOtherMouseDown:
				if (GetMousePosition(event, x, y))
				{
					const EMouseButton button = TranslateButton(event);
					if ([event type] == NSEventTypeLeftMouseDown)
					{
						s_leftButtonAs = button;
					}
					EventLoop::MouseDown(button, x, y);
				}
				return true;

			case NSEventTypeLeftMouseUp:
			case NSEventTypeRightMouseUp:
			case NSEventTypeOtherMouseUp:
				if (GetMousePosition(event, x, y))
				{
					const EMouseButton button = [event type] == NSEventTypeLeftMouseUp ? s_leftButtonAs : TranslateButton(event);
					EventLoop::MouseUp(button, x, y);
				}
				return true;

			case NSEventTypeMouseMoved:
			case NSEventTypeLeftMouseDragged:
			case NSEventTypeRightMouseDragged:
			case NSEventTypeOtherMouseDragged:
				if (PlatformOsX::IsMouseCaptured())
				{
					PlatformOsX::AddCapturedMouseDelta([event deltaX], [event deltaY]);
				}
				if (GetMousePosition(event, x, y))
				{
					EventLoop::MouseMove(x, y);
				}
				return true;

			case NSEventTypeScrollWheel:
			{
				double delta = [event scrollingDeltaY];
				if ([event hasPreciseScrollingDeltas])
				{
					// Trackpads report points; turn every 10 points into one wheel step.
					delta /= 10.0;
				}

				s_scrollAccumulator += delta;
				const int32 steps = static_cast<int32>(s_scrollAccumulator);
				if (steps != 0)
				{
					s_scrollAccumulator -= steps;
					EventLoop::MouseWheel(steps);
				}
				return true;
			}

			case NSEventTypeAppKitDefined:
				if ([event subtype] == NSEventSubtypeApplicationDeactivated)
				{
					ReleaseAllKeys();
				}
				return true;

			default:
				return true;
			}
		}
	}

	bool EventLoop::ProcessOsInput()
	{
		// Everything autoreleased during a frame (Metal descriptors, strings, ...) is released
		// at the start of the next one; the game loop never returns to an AppKit run loop.
		if (s_framePool)
		{
			objc_autoreleasePoolPop(s_framePool);
		}
		s_framePool = objc_autoreleasePoolPush();

		@autoreleasepool
		{
			while (NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES])
			{
				if (HandleEvent(event))
				{
					[NSApp sendEvent:event];
				}
			}

			[NSApp updateWindows];

			if (NSWindow* keyWindow = [NSApp keyWindow]; !keyWindow && !s_keysDown.empty())
			{
				ReleaseAllKeys();
			}
		}

		return !s_quitRequested;
	}

	void EventLoop::Terminate(int32 exitCode)
	{
		s_quitRequested = true;
	}
}
