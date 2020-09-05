// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"

namespace mmo
{
	/// Enumerates possible mouse buttons.
	enum EMouseButton
	{
		// Default buttons

		/// Left mouse button.
		MouseButton_Left = 0,
		/// Right mouse button.
		MouseButton_Right = 1,
		/// Mouse wheel has been pressed (not scrolled!)
		MouseButton_Middle = 2,

		// Some mice have additional two buttons

		/// Fourth button (if available)
		MouseButton_Four = 3,
		/// Fifth button (if available)
		MouseButton_Five = 4,
	};

	/// This class represents an event loop.
	class EventLoop : public NonCopyable
	{
	public:
		/// This event is fired regularly to update the game logic.
		static signal<void(float deltaSeconds, GameTime timestamp)> Idle;
		/// This event is fired regularly to render the game.
		static signal<void()> Paint;
		/// This event is fired when the OS reports that a key has been pressed.
		static signal<bool(int32 key)> KeyDown;
		/// This event is fired when the OS reports that a key has been released.
		static signal<bool(int32 key)> KeyUp;
		/// This event is fired when the OS reports a key char input event.
		static signal<bool(uint16 codepoint)> KeyChar;
		/// This event is fired regularly to render the game.
		static signal<bool(EMouseButton button, int32 x, int32 y)> MouseDown;
		/// This event is fired regularly to render the game.
		static signal<bool(EMouseButton button, int32 x, int32 y)> MouseUp;
		/// This event is fired regularly to render the game.
		static signal<bool(int32 x, int32 y)> MouseMove;

	public:
		/// Initializes the event loop.
		static void Initialize();
		/// Destroys the event loop.
		static void Destroy();

	private:
		/// This method handles operating system messages
		static bool ProcessOsInput();

	public:
		/// Runs the event loop.
		static void Run();
		/// Terminates the event loop.
		static void Terminate(int32 exitCode);
	};
}
