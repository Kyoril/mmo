// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "event_loop.h"

#include "base/clock.h"
#include "graphics/graphics_device.h"

#include <thread>

#import <Cocoa/Cocoa.h>


namespace mmo
{
    // Event loop events
    signal<void(float deltaSeconds, GameTime timestamp)> EventLoop::Idle;
    signal<void()> EventLoop::Paint;
    signal<bool(int32, bool)> EventLoop::KeyDown;
    signal<bool(uint16)> EventLoop::KeyChar;
    signal<bool(int32)> EventLoop::KeyUp;
    signal<bool(EMouseButton, int32, int32)> EventLoop::MouseDown;
    signal<bool(EMouseButton, int32, int32)> EventLoop::MouseUp;
    signal<bool(int32, int32)> EventLoop::MouseMove;
    signal<bool(int32)> EventLoop::MouseWheel;

    static int s_captureCount = 0;

    void EventLoop::Initialize()
    {
    }

    void EventLoop::Destroy()
    {
    }

    bool EventLoop::ProcessOsInput()
    {
        //TODO("Implement os input handling on operating system!");
        return true;
    }

    void EventLoop::Run()
    {
        // Capture timestamp
        auto lastIdle = GetAsyncTimeMs();

        auto& gx = GraphicsDevice::Get();
        const auto gxWindow = gx.GetAutoCreatedWindow();

        // Run the event loop until the operating system tells us to stop here
        while (true)
        {
            // Process operating system input
            if (!ProcessOsInput())
                break;

            // Get current timestamp
            auto currentTime = GetAsyncTimeMs();
            auto timePassed = static_cast<float>((currentTime - lastIdle) / 1000.0);
            lastIdle = currentTime;

            // Raise idle event
            Idle(timePassed, currentTime);

            // Reset state for new frame
            gx.Reset();

            // Activate the main window as render target and clear it
            gxWindow->Activate();
            gxWindow->Clear(mmo::ClearFlags::All);

            // Paint contents
            Paint();

            // Flip buffers
            gxWindow->Update();
        }
    }

    void EventLoop::Terminate(int32 exitCode)
    {
        [[NSApplication sharedApplication] terminate:nil];
    }
}
