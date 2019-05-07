
#include "event_loop.h"

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#endif

#include <thread>
#include <chrono>


namespace mmo
{
	// Event loop events
	signal<void(float deltaSeconds, GameTime timestamp)> EventLoop::Idle;
	signal<void()> EventLoop::Paint;
	signal<bool(int)> EventLoop::KeyDown;
	signal<bool(int)> EventLoop::KeyUp;


	void EventLoop::Initialize()
	{
	}

	void EventLoop::Destroy()
	{
	}

	bool EventLoop::ProcessOsInput()
	{
#ifdef _WIN32
		// Process windows messages
		MSG msg = { 0 };
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// If WM_QUIT is received, quit
		return (msg.message != WM_QUIT);
#else
		ASSERT(!"TODO: Implement os input handling on operating system!");
		return true;
#endif
	}

	void EventLoop::Run()
	{
		// Capture timestamp
		GameTime lastIdle = GetAsyncTimeMs();

		// Run the event loop until the operating system tells us to stop here
		while (true)
		{
			// Process operating system input
			if (!ProcessOsInput())
				break;

			// Get current timestamp
			GameTime currentTime = GetAsyncTimeMs();
			float timePassed = static_cast<float>((currentTime - lastIdle) / 1000.0);
			lastIdle = currentTime;

			// Raise idle event
			Idle(timePassed, currentTime);

			// Raise paint event
			Paint();

			// Pretend we have some work to do
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}
