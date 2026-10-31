// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "launcher_model.h"
#include "launcher_view.h"
#include "platform_host.h"
#include "update_worker.h"

#include "base/typedefs.h"
#include "base/non_copyable.h"

#include <filesystem>
#include <string>
#include <vector>

@class NSWindow;
@class NSTimer;
@class MMOLauncherView;
@class MMOLauncherWindowDelegate;

namespace mmo
{
	/// The Cocoa shell around the portable launcher UI.
	///
	/// The counterpart of the Win32 LauncherWindow and the only class that knows about
	/// AppKit. It owns the pixel buffer the portable Canvas composites into and hands
	/// each finished frame to a layer as a CGImage; the buffer's premultiplied BGRA is
	/// CoreGraphics' kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, so
	/// nothing is converted on the way.
	///
	/// Only included from Objective-C++ translation units.
	class LauncherWindow final
		: public IPlatformHost
		, public NonCopyable
	{
	public:
		LauncherWindow(LauncherModel& model, UpdateWorker& worker);
		~LauncherWindow() override;

		/// Creates the borderless window and its surface and shows the first frame.
		bool Create();

		/// Runs the application event loop until the window closes.
		int Run();
		/// Starts the independent content worker using the user's local cache.
		void StartContent(const std::string& url);

		/// Asks the UI thread to close the window because a self-update has been
		/// applied. Safe to call from the update thread: it only dispatches.
		void NotifySelfUpdateFinished();

		// IPlatformHost
		void LaunchGame() override;
		std::string GetGameDirectory() const override;
		bool GetKeepOpen() const override;
		bool SaveKeepOpen(bool keepOpen) override;
		void OpenGameDirectory() override;
		void OpenLogDirectory() override;
		bool RepairGame() override;
		bool SetDownloadPaused(bool paused) override;
		void Minimize() override;
		void Close() override;
		void ShowMessage(const std::string& title, const std::string& body) override;

		// Event entry points for the Objective-C view and delegate. Points are in the
		// view's top-left based coordinate space, in points.
		void OnMouseMove(double x, double y);
		void OnMouseLeave();
		void OnMouseDown(double x, double y, bool& startDrag);
		void OnMouseUp(double x, double y);
		void OnScroll(double deltaY, bool precise);
		void OnKey(uint16 keyCode, bool shift);
		bool HasControlAt(double x, double y);
		void OnBackingChanged();

	private:
		/// Computes the scale from the window's screen: the backing factor, reduced so
		/// that the window fits on screens smaller than its logical size.
		void UpdateScale();

		/// Allocates (or reallocates) the pixel buffer for the current scale.
		void CreateSurface();

		/// Composites a frame and hands it to the view's layer.
		void RenderAndPresent();

		void OnTimer();

		/// Converts a view point into the view's logical coordinate space.
		Point ToLogical(double x, double y) const;

		/// The launcher.ini next to the game, shared in format with Windows.
		std::filesystem::path GetSettingsPath() const;

		LauncherModel& m_model;
		UpdateWorker& m_worker;
		LauncherView m_view;
		LauncherContentService m_contentService;
		uint64 m_contentRevision = 0;

		NSWindow* m_window = nullptr;
		MMOLauncherView* m_contentView = nullptr;
		MMOLauncherWindowDelegate* m_delegate = nullptr;
		NSTimer* m_timer = nullptr;

		std::vector<Color> m_surface;
		int32 m_surfaceWidth = 0;
		int32 m_surfaceHeight = 0;

		/// Points per logical pixel; below 1 only on screens too small for the window.
		float m_fitScale = 1.0f;
		/// Pixels per logical pixel: the backing factor times the fit scale.
		float m_dpiScale = 1.0f;

		/// Fractional trackpad scroll that has not added up to a whole pixel yet.
		double m_scrollRemainder = 0.0;
		uint32 m_lastSeenVersion = 0;
		bool m_closing = false;
	};
}
