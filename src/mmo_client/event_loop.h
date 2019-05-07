
#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/clock.h"

namespace mmo
{
	/// This class represents an event loop.
	class EventLoop : public NonCopyable
	{
	public:
		/// This event is fired regularly to update the game logic.
		static signal<void(float deltaSeconds, GameTime timestamp)> Idle;
		/// This event is fired regularly to render the game.
		static signal<void()> Paint;
		/// This event is fired regularly to render the game.
		static signal<bool(int key)> KeyDown;
		/// This event is fired regularly to render the game.
		static signal<bool(int key)> KeyUp;

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
	};
}
