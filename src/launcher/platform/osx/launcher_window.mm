// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "launcher_window.h"
#include "launcher_osx.h"

#include "canvas.h"
#include "launcher_layout.h"
#include "version.h"

#include "log/default_log_levels.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

namespace
{
	constexpr const char* WindowTitle = "Alestia Online";

	/// The render clock. Like on Windows the model is polled here rather than being
	/// pushed to the UI thread per progress callback.
	constexpr NSTimeInterval RenderTimerInterval = 0.016;

	/// Bundle of the game client, next to Launcher.app.
	constexpr const char* ClientBundleName = "mmo_client.app";

	// Virtual key codes from Carbon's Events.h, mapped to the Windows virtual keys
	// LauncherView::OnKey understands.
	constexpr uint16 KeyTab = 48;
	constexpr uint16 KeyReturn = 36;
	constexpr uint16 KeyKeypadEnter = 76;
	constexpr uint16 KeySpace = 49;
	constexpr uint16 KeyEscape = 53;
	constexpr uint16 KeyUp = 126;
	constexpr uint16 KeyDown = 125;
	constexpr uint16 KeyPageUp = 116;
	constexpr uint16 KeyPageDown = 121;
	constexpr uint16 KeyHome = 115;
	constexpr uint16 KeyEnd = 119;

	mmo::int32 toViewKey(const uint16 keyCode)
	{
		switch (keyCode)
		{
		case KeyTab: return 9;
		case KeyReturn:
		case KeyKeypadEnter: return 13;
		case KeySpace: return 32;
		case KeyPageUp: return 33;
		case KeyPageDown: return 34;
		case KeyEnd: return 35;
		case KeyHome: return 36;
		case KeyUp: return 38;
		case KeyDown: return 40;
		default: return 0;
		}
	}

	NSString* toNSString(const std::string& text)
	{
		NSString* result = [NSString stringWithUTF8String:text.c_str()];
		return result ? result : @"";
	}

	/// Reads `key` from `[section]` of a Windows style ini file, as GetPrivateProfileInt does.
	bool readIniValue(const std::filesystem::path& path, const std::string& section, const std::string& key, std::string& value)
	{
		std::ifstream file(path);
		std::string line;
		bool inSection = false;
		while (std::getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			if (!line.empty() && line.front() == '[')
			{
				inSection = line == "[" + section + "]";
				continue;
			}
			const size_t separator = line.find('=');
			if (inSection && separator != std::string::npos && line.substr(0, separator) == key)
			{
				value = line.substr(separator + 1);
				return true;
			}
		}
		return false;
	}

	/// Sets `key` in `[section]`, keeping every other line, as WritePrivateProfileString does.
	bool writeIniValue(const std::filesystem::path& path, const std::string& section, const std::string& key, const std::string& value)
	{
		std::vector<std::string> lines;
		{
			std::ifstream file(path);
			std::string line;
			while (std::getline(file, line))
			{
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}
				lines.push_back(line);
			}
		}

		const std::string header = "[" + section + "]";
		const std::string entry = key + "=" + value;
		auto sectionIt = std::find(lines.begin(), lines.end(), header);
		if (sectionIt == lines.end())
		{
			lines.push_back(header);
			lines.push_back(entry);
		}
		else
		{
			auto it = std::next(sectionIt);
			for (; it != lines.end() && (it->empty() || it->front() != '['); ++it)
			{
				if (it->compare(0, key.size() + 1, key + "=") == 0)
				{
					*it = entry;
					break;
				}
			}
			if (it == lines.end() || it->front() == '[')
			{
				lines.insert(it, entry);
			}
		}

		std::ofstream file(path, std::ios::trunc);
		for (const std::string& line : lines)
		{
			file << line << "\r\n";
		}
		return file.good();
	}
}

/// Borderless windows refuse key status by default, which would leave the launcher
/// without keyboard input.
@interface MMOLauncherNSWindow : NSWindow
@end

@implementation MMOLauncherNSWindow
- (BOOL)canBecomeKeyWindow
{
	return YES;
}
- (BOOL)canBecomeMainWindow
{
	return YES;
}
@end

/// Forwards input to the C++ window. The content itself is a layer whose contents are
/// replaced with every frame, so nothing is drawn through drawRect.
@interface MMOLauncherView : NSView
@property(nonatomic, assign) mmo::LauncherWindow* owner;
@end

@implementation MMOLauncherView
{
	NSTrackingArea* m_trackingArea;
}

- (BOOL)isFlipped
{
	// Top-left origin, like the logical layout.
	return YES;
}

- (BOOL)acceptsFirstResponder
{
	return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
	// A click on the inactive launcher acts right away, as it does on Windows.
	return YES;
}

- (BOOL)wantsUpdateLayer
{
	return YES;
}

- (void)updateTrackingAreas
{
	if (m_trackingArea)
	{
		[self removeTrackingArea:m_trackingArea];
	}
	m_trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
												  options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingCursorUpdate |
														  NSTrackingActiveAlways | NSTrackingInVisibleRect
													owner:self
												 userInfo:nil];
	[self addTrackingArea:m_trackingArea];
	[super updateTrackingAreas];
}

- (NSPoint)localPoint:(NSEvent*)event
{
	return [self convertPoint:[event locationInWindow] fromView:nil];
}

- (void)updateCursor:(NSEvent*)event
{
	const NSPoint point = [self localPoint:event];
	if (self.owner && self.owner->HasControlAt(point.x, point.y))
	{
		[[NSCursor pointingHandCursor] set];
	}
	else
	{
		[[NSCursor arrowCursor] set];
	}
}

- (void)cursorUpdate:(NSEvent*)event
{
	[self updateCursor:event];
}

- (void)mouseMoved:(NSEvent*)event
{
	const NSPoint point = [self localPoint:event];
	if (self.owner)
	{
		self.owner->OnMouseMove(point.x, point.y);
	}
	[self updateCursor:event];
}

- (void)mouseDragged:(NSEvent*)event
{
	// AppKit keeps delivering drags and the release to the view that saw the press,
	// which is what SetCapture provides on Windows.
	const NSPoint point = [self localPoint:event];
	if (self.owner)
	{
		self.owner->OnMouseMove(point.x, point.y);
	}
}

- (void)mouseExited:(NSEvent*)event
{
	if (self.owner)
	{
		self.owner->OnMouseLeave();
	}
}

- (void)mouseDown:(NSEvent*)event
{
	const NSPoint point = [self localPoint:event];
	bool startDrag = false;
	if (self.owner)
	{
		self.owner->OnMouseDown(point.x, point.y, startDrag);
	}
	if (startDrag)
	{
		// The window server moves the window, which brings snapping to screen edges and
		// Spaces behaviour along, like HTCAPTION does on Windows.
		[self.window performWindowDragWithEvent:event];
	}
}

- (void)mouseUp:(NSEvent*)event
{
	const NSPoint point = [self localPoint:event];
	if (self.owner)
	{
		self.owner->OnMouseUp(point.x, point.y);
	}
}

- (void)scrollWheel:(NSEvent*)event
{
	if (self.owner)
	{
		self.owner->OnScroll([event scrollingDeltaY], [event hasPreciseScrollingDeltas]);
	}
}

- (void)keyDown:(NSEvent*)event
{
	if (self.owner)
	{
		self.owner->OnKey([event keyCode], ([event modifierFlags] & NSEventModifierFlagShift) != 0);
	}
}

@end

/// Window and application delegate: routes closing, quitting and screen changes into
/// the C++ window, and owns the menu actions.
@interface MMOLauncherWindowDelegate : NSObject<NSWindowDelegate, NSApplicationDelegate>
@property(nonatomic, assign) mmo::LauncherWindow* owner;
@end

@implementation MMOLauncherWindowDelegate

- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
	if (self.owner)
	{
		self.owner->OnBackingChanged();
	}
}

- (void)windowDidChangeScreen:(NSNotification*)notification
{
	if (self.owner)
	{
		self.owner->OnBackingChanged();
	}
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
	if (self.owner)
	{
		self.owner->Close();
	}
	return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
	// Cmd+Q, the Dock menu and logging out go through the normal close, so that main()
	// still stops the update worker before the process ends.
	if (self.owner)
	{
		self.owner->Close();
		return NSTerminateCancel;
	}
	return NSTerminateNow;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag
{
	// Clicking the Dock icon restores the minimized launcher.
	for (NSWindow* window in [sender windows])
	{
		if ([window isMiniaturized])
		{
			[window deminiaturize:nil];
		}
	}
	return YES;
}

- (void)showAbout:(id)sender
{
	mmo::ShowLauncherVersionInfo();
}

- (void)minimizeLauncher:(id)sender
{
	if (self.owner)
	{
		self.owner->Minimize();
	}
}

- (void)closeLauncher:(id)sender
{
	if (self.owner)
	{
		self.owner->Close();
	}
}

@end

namespace mmo
{
	namespace
	{
		void createMenuBar(MMOLauncherWindowDelegate* delegate)
		{
			NSMenu* menuBar = [[NSMenu alloc] init];

			NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
			[menuBar addItem:appMenuItem];
			NSMenu* appMenu = [[NSMenu alloc] init];
			NSString* name = toNSString(WindowTitle);

			NSMenuItem* about = [appMenu addItemWithTitle:[@"About " stringByAppendingString:name] action:@selector(showAbout:) keyEquivalent:@""];
			about.target = delegate;
			[appMenu addItem:[NSMenuItem separatorItem]];
			[appMenu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
			NSMenuItem* hideOthers = [appMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
			hideOthers.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
			[appMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
			[appMenu addItem:[NSMenuItem separatorItem]];
			[appMenu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
			[appMenuItem setSubmenu:appMenu];

			NSMenuItem* windowMenuItem = [[NSMenuItem alloc] init];
			[menuBar addItem:windowMenuItem];
			NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
			NSMenuItem* minimize = [windowMenu addItemWithTitle:@"Minimize" action:@selector(minimizeLauncher:) keyEquivalent:@"m"];
			minimize.target = delegate;
			NSMenuItem* close = [windowMenu addItemWithTitle:@"Close" action:@selector(closeLauncher:) keyEquivalent:@"w"];
			close.target = delegate;
			[windowMenuItem setSubmenu:windowMenu];

			[NSApp setMainMenu:menuBar];
			[NSApp setWindowsMenu:windowMenu];
		}
	}

	std::filesystem::path GetLauncherLogDirectory()
	{
		@autoreleasepool
		{
			NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
			if ([paths count] == 0)
			{
				return {};
			}
			return std::filesystem::path([[paths firstObject] fileSystemRepresentation]) / "Logs" / "MMORPG";
		}
	}

	std::filesystem::path GetLauncherCacheDirectory()
	{
		@autoreleasepool
		{
			NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
			if ([paths count] == 0)
			{
				return {};
			}
			return std::filesystem::path([[paths firstObject] fileSystemRepresentation]) / "AlestiaOnline" / "Launcher";
		}
	}

	void ShowLauncherVersionInfo()
	{
		const std::string body =
			"Version: " MMO_LAUNCHER_VERSION_STR "\n"
			"Build date: " __DATE__ " " __TIME__ "\n"
#ifndef NDEBUG
			"Debug configuration\n"
#endif
			;

		@autoreleasepool
		{
			NSAlert* alert = [[NSAlert alloc] init];
			alert.messageText = @"MMORPG Launcher";
			alert.informativeText = toNSString(body);
			alert.alertStyle = NSAlertStyleInformational;
			[alert runModal];
		}
	}

	LauncherWindow::LauncherWindow(LauncherModel& model, UpdateWorker& worker)
		: m_model(model)
		, m_worker(worker)
		, m_view(*this)
	{
	}

	LauncherWindow::~LauncherWindow()
	{
		[m_timer invalidate];
		m_timer = nil;

		m_contentView.owner = nullptr;
		m_delegate.owner = nullptr;
		[NSApp setDelegate:nil];
		[m_window setDelegate:nil];
		[m_window close];
	}

	bool LauncherWindow::Create()
	{
		m_delegate = [[MMOLauncherWindowDelegate alloc] init];
		m_delegate.owner = this;
		[NSApp setDelegate:m_delegate];
		createMenuBar(m_delegate);

		// Miniaturizable without Titled keeps the window borderless but lets it go to
		// the Dock, which Minimize needs.
		m_window = [[MMOLauncherNSWindow alloc] initWithContentRect:NSMakeRect(0, 0, layout::WindowWidth, layout::WindowHeight)
														  styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskMiniaturizable
															backing:NSBackingStoreBuffered
															  defer:NO];
		if (!m_window)
		{
			ELOG("Failed to create the launcher window");
			return false;
		}

		// A clear, non-opaque window gives real per-pixel alpha, so the frame art's
		// ornamental silhouette shows the desktop through it, like WS_EX_LAYERED. There
		// is no system shadow on Windows either, and one computed from that silhouette
		// would trace every gap in the frame.
		[m_window setTitle:toNSString(WindowTitle)];
		[m_window setOpaque:NO];
		[m_window setBackgroundColor:[NSColor clearColor]];
		[m_window setHasShadow:NO];
		[m_window setReleasedWhenClosed:NO];
		[m_window setDelegate:m_delegate];
		[m_window setAcceptsMouseMovedEvents:YES];

		m_contentView = [[MMOLauncherView alloc] initWithFrame:NSMakeRect(0, 0, layout::WindowWidth, layout::WindowHeight)];
		m_contentView.owner = this;
		[m_contentView setWantsLayer:YES];
		m_contentView.layer.opaque = NO;
		m_contentView.layer.contentsGravity = kCAGravityResize;
		m_contentView.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
		[m_window setContentView:m_contentView];
		[m_window makeFirstResponder:m_contentView];

		UpdateScale();

		if (!m_view.Initialize(m_dpiScale))
		{
			ELOG("Failed to initialize the launcher view");
			return false;
		}

		CreateSurface();

		// Center on the visible area of the screen it landed on, now that its size is known.
		NSScreen* screen = [m_window screen] ? [m_window screen] : [NSScreen mainScreen];
		const NSRect visible = [screen visibleFrame];
		const NSSize size = NSMakeSize(layout::WindowWidth * m_fitScale, layout::WindowHeight * m_fitScale);
		[m_window setFrame:NSMakeRect(NSMidX(visible) - size.width / 2, NSMidY(visible) - size.height / 2, size.width, size.height) display:NO];

		// Like a layered window, the layer is empty until the first frame is handed to
		// it, so that frame has to exist before the window is shown.
		RenderAndPresent();

		mmo::LauncherWindow* const self = this;
		m_timer = [NSTimer timerWithTimeInterval:RenderTimerInterval
										 repeats:YES
										   block:^(NSTimer*) {
											   self->OnTimer();
										   }];
		// Common modes keep the clock running while the window is dragged or an alert is up.
		[[NSRunLoop mainRunLoop] addTimer:m_timer forMode:NSRunLoopCommonModes];

		[m_window makeKeyAndOrderFront:nil];
		[NSApp activateIgnoringOtherApps:YES];
		return true;
	}

	void LauncherWindow::UpdateScale()
	{
		NSScreen* screen = [m_window screen] ? [m_window screen] : [NSScreen mainScreen];
		const float backing = screen ? static_cast<float>([screen backingScaleFactor]) : 1.0f;

		m_fitScale = 1.0f;
		if (screen)
		{
			const NSRect visible = [screen visibleFrame];
			const float widthScale = static_cast<float>(visible.size.width - 24) / layout::WindowWidth;
			const float heightScale = static_cast<float>(visible.size.height - 24) / layout::WindowHeight;
			m_fitScale = std::max(0.25f, std::min({ 1.0f, widthScale, heightScale }));
		}

		m_dpiScale = backing * m_fitScale;
		m_contentView.layer.contentsScale = backing;
	}

	void LauncherWindow::CreateSurface()
	{
		m_surfaceWidth = m_view.Scale(layout::WindowWidth);
		m_surfaceHeight = m_view.Scale(layout::WindowHeight);
		m_surface.assign(static_cast<size_t>(m_surfaceWidth) * static_cast<size_t>(m_surfaceHeight), Color{});
	}

	Point LauncherWindow::ToLogical(const double x, const double y) const
	{
		return Point{
			static_cast<int32>(x / m_fitScale),
			static_cast<int32>(y / m_fitScale)
		};
	}

	int LauncherWindow::Run()
	{
		[NSApp run];
		return 0;
	}

	void LauncherWindow::StartContent(const std::string& url)
	{
		const std::filesystem::path cacheDirectory = GetLauncherCacheDirectory();
		if (cacheDirectory.empty())
		{
			WLOG("Launcher content cache directory unavailable");
			return;
		}

		m_contentService.Start(url, cacheDirectory / "content");
		std::shared_ptr<const RemoteLauncherContent> content;
		if (m_contentService.TryGetContent(content, m_contentRevision))
		{
			m_view.ApplyRemoteContent(std::move(content));
		}
	}

	void LauncherWindow::NotifySelfUpdateFinished()
	{
		// Called from the update thread; AppKit may only be used on the main thread.
		mmo::LauncherWindow* const self = this;
		dispatch_async(dispatch_get_main_queue(), ^{
			self->Close();
		});
	}

	void LauncherWindow::LaunchGame()
	{
		const std::filesystem::path client = std::filesystem::path(GetGameDirectory()) / ClientBundleName;

		std::error_code error;
		if (!std::filesystem::exists(client, error))
		{
			ELOG("Game client not found at " << client);
			ShowMessage("Error", "Failed to start the game client.");
			return;
		}

		NSURL* url = [NSURL fileURLWithPath:toNSString(client.string()) isDirectory:YES];
		NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
		configuration.arguments = @[ @"-uptodate" ];
		// Like CreateProcess, a second launch starts a second client instead of only
		// activating the running one.
		configuration.createsNewApplicationInstance = YES;
		configuration.activates = YES;

		mmo::LauncherWindow* const self = this;
		[[NSWorkspace sharedWorkspace] openApplicationAtURL:url
											  configuration:configuration
										  completionHandler:^(NSRunningApplication* application, NSError* launchError) {
											  NSString* description = launchError ? [launchError localizedDescription] : nil;
											  const std::string reason = description ? [description UTF8String] : "";
											  dispatch_async(dispatch_get_main_queue(), ^{
												  if (!application)
												  {
													  ELOG("Failed to start the game client: " << reason);
													  self->ShowMessage("Error", "Failed to start the game client.");
													  return;
												  }

												  if (!self->GetKeepOpen())
												  {
													  self->Close();
												  }
											  });
										  }];
	}

	std::string LauncherWindow::GetGameDirectory() const
	{
		std::error_code error;
		const std::filesystem::path directory = std::filesystem::current_path(error);
		return error ? "." : directory.string();
	}

	std::filesystem::path LauncherWindow::GetSettingsPath() const
	{
		return std::filesystem::path(GetGameDirectory()) / "launcher.ini";
	}

	bool LauncherWindow::GetKeepOpen() const
	{
		std::string value;
		return readIniValue(GetSettingsPath(), "Launcher", "KeepOpen", value) && std::atoi(value.c_str()) != 0;
	}

	bool LauncherWindow::SaveKeepOpen(const bool keepOpen)
	{
		return writeIniValue(GetSettingsPath(), "Launcher", "KeepOpen", keepOpen ? "1" : "0");
	}

	void LauncherWindow::OpenGameDirectory()
	{
		[[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:toNSString(GetGameDirectory()) isDirectory:YES]];
	}

	void LauncherWindow::OpenLogDirectory()
	{
		const std::filesystem::path directory = GetLauncherLogDirectory();
		if (!directory.empty())
		{
			[[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:toNSString(directory.string()) isDirectory:YES]];
		}
	}

	bool LauncherWindow::SetDownloadPaused(const bool paused)
	{
		UpdateSnapshot snapshot;
		uint32 version = 0;
		m_model.TryGetSnapshot(snapshot, version);
		if (snapshot.phase != UpdatePhase::Updating)
		{
			return false;
		}
		m_worker.SetPaused(paused);
		return true;
	}

	bool LauncherWindow::RepairGame()
	{
		if (!m_worker.Restart())
		{
			return false;
		}
		UpdateSnapshot snapshot;
		snapshot.phase = UpdatePhase::Preparing;
		snapshot.statusText = "Checking game files...";
		m_view.ApplySnapshot(snapshot);
		return true;
	}

	void LauncherWindow::Minimize()
	{
		[m_window miniaturize:nil];
	}

	void LauncherWindow::Close()
	{
		if (m_closing)
		{
			return;
		}
		m_closing = true;

		// Start aborting the update right away rather than only after the event loop
		// has ended, so downloads stop while the window is going away.
		m_worker.RequestStop();

		[m_timer invalidate];
		m_timer = nil;
		[m_window orderOut:nil];

		// stop: only takes effect once an event has been processed, so one is posted in
		// case this was not called from inside an event handler.
		[NSApp stop:nil];
		NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
										   location:NSZeroPoint
									  modifierFlags:0
										  timestamp:0
									   windowNumber:0
											context:nil
											subtype:0
											  data1:0
											  data2:0];
		[NSApp postEvent:wake atStart:NO];
	}

	void LauncherWindow::ShowMessage(const std::string& title, const std::string& body)
	{
		NSAlert* alert = [[NSAlert alloc] init];
		alert.messageText = toNSString(title);
		alert.informativeText = toNSString(body);
		alert.alertStyle = NSAlertStyleCritical;
		[alert runModal];
	}

	void LauncherWindow::RenderAndPresent()
	{
		if (m_surface.empty())
		{
			return;
		}

		Canvas canvas(m_surface.data(), m_surfaceWidth, m_surfaceHeight, m_surfaceWidth);
		m_view.Render(canvas);

		// The image gets its own copy of the pixels. CGBitmapContextCreateImage would
		// share them copy-on-write, but the canvas writes to memory directly rather than
		// through the context, so the next frame would show through a frame on screen.
		const size_t bytesPerRow = static_cast<size_t>(m_surfaceWidth) * sizeof(Color);
		CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(m_surface.data()), bytesPerRow * m_surfaceHeight);
		CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
		CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);

		// Exactly the compositor's premultiplied BGRA.
		CGImageRef image = CGImageCreate(m_surfaceWidth, m_surfaceHeight, 8, 32, bytesPerRow, colorSpace,
			static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little, provider, nullptr, false, kCGRenderingIntentDefault);

		[CATransaction begin];
		[CATransaction setDisableActions:YES];
		m_contentView.layer.contents = (__bridge id)image;
		[CATransaction commit];

		CGImageRelease(image);
		CGColorSpaceRelease(colorSpace);
		CGDataProviderRelease(provider);
		CFRelease(data);
	}

	void LauncherWindow::OnTimer()
	{
		std::shared_ptr<const RemoteLauncherContent> content;
		if (m_contentService.TryGetContent(content, m_contentRevision))
		{
			m_view.ApplyRemoteContent(std::move(content));
		}
		UpdateSnapshot snapshot;
		if (m_model.TryGetSnapshot(snapshot, m_lastSeenVersion))
		{
			m_view.ApplySnapshot(snapshot);
		}

		const float delta = static_cast<float>(RenderTimerInterval);
		if (m_view.Tick(delta) || m_view.IsDirty())
		{
			RenderAndPresent();
		}
	}

	void LauncherWindow::OnBackingChanged()
	{
		const float previous = m_dpiScale;
		UpdateScale();
		if (previous == m_dpiScale)
		{
			return;
		}

		// Rebuilds the fonts at the new pixel size, rescales the splash and re-composites
		// the cached background.
		m_view.SetDpiScale(m_dpiScale);
		CreateSurface();

		// Keep the top-left corner in place while the point size follows the fit scale.
		NSRect frame = [m_window frame];
		const NSSize size = NSMakeSize(layout::WindowWidth * m_fitScale, layout::WindowHeight * m_fitScale);
		frame.origin.y += frame.size.height - size.height;
		frame.size = size;
		[m_window setFrame:frame display:NO];

		RenderAndPresent();
	}

	void LauncherWindow::OnMouseMove(const double x, const double y)
	{
		m_view.OnMouseMove(ToLogical(x, y));
	}

	void LauncherWindow::OnMouseLeave()
	{
		m_view.OnMouseLeave();
	}

	void LauncherWindow::OnMouseDown(const double x, const double y, bool& startDrag)
	{
		const Point point = ToLogical(x, y);

		// The caption is not client area on Windows either: a press there drags the
		// window and never reaches the view.
		startDrag = m_view.HitTestCaption(point);
		if (!startDrag)
		{
			m_view.OnMouseDown(point);
		}
	}

	void LauncherWindow::OnMouseUp(const double x, const double y)
	{
		m_view.OnMouseUp(ToLogical(x, y));
	}

	void LauncherWindow::OnScroll(const double deltaY, const bool precise)
	{
		// Trackpads report points, wheels report lines; a line scrolls as far as one
		// wheel notch does on Windows. Either way a positive delta moves content down.
		const double logical = precise ? -deltaY / m_fitScale : -deltaY * 72.0;
		m_scrollRemainder += logical;

		const int32 whole = static_cast<int32>(m_scrollRemainder);
		if (whole != 0)
		{
			m_scrollRemainder -= whole;
			m_view.OnScroll(whole);
		}
	}

	void LauncherWindow::OnKey(const uint16 keyCode, const bool shift)
	{
		if (keyCode == KeyEscape)
		{
			Close();
			return;
		}

		if (const int32 key = toViewKey(keyCode); key != 0)
		{
			m_view.OnKey(key, shift);
		}
	}

	bool LauncherWindow::HasControlAt(const double x, const double y)
	{
		return m_view.HasControlAt(ToLogical(x, y));
	}
}
