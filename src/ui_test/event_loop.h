// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
		/// @brief This event is fired regularly to update the game logic.
		static signal<void(float deltaSeconds, GameTime timestamp)> Idle;

		/// @brief This event is fired regularly to render the game.
		static signal<void()> Paint;

		/// @brief This event is fired when the OS reports that a key has been pressed.
		static signal<bool(int32 key, bool repeat)> KeyDown;

		/// @brief This event is fired when the OS reports that a key has been released.
		static signal<bool(int32 key)> KeyUp;

		/// @brief This event is fired when the OS reports a key char input event.
		static signal<bool(uint16 codepoint)> KeyChar;

		/// @brief This event is fired when a mouse button was pressed.
		static signal<bool(EMouseButton button, int32 x, int32 y)> MouseDown;

		/// @brief This event is fired when a mouse button was released.
		static signal<bool(EMouseButton button, int32 x, int32 y)> MouseUp;

		/// @brief This event is fired whenever the mouse was moved.
		static signal<bool(int32 x, int32 y)> MouseMove;

		/// @brief This event is fired whenever the mouse wheel axis changed.
		static signal<bool(int32 delta)> MouseWheel;

	public:
		/// @brief Initializes the event loop.
		static void Initialize();

		/// @brief Destroys the event loop.
		static void Destroy();

	private:
		/// @brief This method handles operating system messages
		static bool ProcessOsInput();

	public:
		/// @brief Runs the event loop.
		static void Run();

		/// @brief Terminates the event loop.
		static void Terminate(int32 exitCode);
	};
}
