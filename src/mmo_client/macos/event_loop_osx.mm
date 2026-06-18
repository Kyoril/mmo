// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "event_loop.h"

#import <Cocoa/Cocoa.h>


namespace mmo
{
    bool EventLoop::ProcessOsInput()
    {
        //TODO("Implement os input handling on macOS!");
        return true;
    }

    void EventLoop::Terminate(int32 exitCode)
    {
        [[NSApplication sharedApplication] terminate:nil];
    }
}
