// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "event_loop.h"
#include "base/typedefs.h"

#import <Cocoa/Cocoa.h>

namespace mmo
{
	int32 CommonMain(int argc, char** argv);
}

/// Routes quit requests (Cmd+Q, the application menu, logging out of macOS) through the game's
/// own shutdown instead of letting AppKit terminate the process under the running game loop.
@interface MMOApplicationDelegate : NSObject<NSApplicationDelegate>
@end

@implementation MMOApplicationDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
	mmo::EventLoop::Terminate(0);
	return NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
	return NO;
}

@end

namespace
{
	void CreateMenuBar()
	{
		NSMenu* menuBar = [[NSMenu alloc] init];
		NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
		[menuBar addItem:appMenuItem];

		NSMenu* appMenu = [[NSMenu alloc] init];
		NSString* appName = [[NSProcessInfo processInfo] processName];
		[appMenu addItemWithTitle:[@"Hide " stringByAppendingString:appName] action:@selector(hide:) keyEquivalent:@"h"];
		[appMenu addItem:[NSMenuItem separatorItem]];
		[appMenu addItemWithTitle:[@"Quit " stringByAppendingString:appName] action:@selector(terminate:) keyEquivalent:@"q"];
		[appMenuItem setSubmenu:appMenu];

		[NSApp setMainMenu:menuBar];
	}
}

/// The macOS entry point. The game runs its own loop (EventLoop::Run polls Cocoa events every
/// frame), so instead of NSApplicationMain this only sets the application up and then runs the
/// common main function.
int main_osx(int argc, char* argv[])
{
	@autoreleasepool
	{
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

		static MMOApplicationDelegate* s_delegate = [[MMOApplicationDelegate alloc] init];
		[NSApp setDelegate:s_delegate];

		CreateMenuBar();
		[NSApp finishLaunching];
		[NSApp activateIgnoringOtherApps:YES];
	}

	return mmo::CommonMain(argc, argv);
}
