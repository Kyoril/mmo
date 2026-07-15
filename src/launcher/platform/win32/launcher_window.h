// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "launcher_model.h"
#include "launcher_view.h"
#include "platform_host.h"
#include "update_worker.h"

#include "base/typedefs.h"
#include "base/non_copyable.h"

#include <memory>
#include <string>

#include <Windows.h>

namespace mmo
{
	/// The Win32 shell around the portable launcher UI.
	///
	/// This is the only class that knows about windows, messages or GDI. It owns the
	/// DIB section that the portable Canvas composites into and blits it to the screen;
	/// no copy or format conversion happens in between, because the DIB's memory layout
	/// is already the premultiplied BGRA the compositor uses.
	class LauncherWindow final
		: public IPlatformHost
		, public NonCopyable
	{
	public:
		LauncherWindow(LauncherModel& model, UpdateWorker& worker);
		~LauncherWindow() override;

		/// Registers the class, creates the window and builds the surface.
		bool Create(HINSTANCE instance);

		/// Pumps messages until the window closes.
		int Run();

		HWND GetHandle() const { return m_handle; }

		/// Asks the UI thread to close the window because a self-update has been
		/// applied. Safe to call from the update thread: it only posts.
		void NotifySelfUpdateFinished() const;

		// IPlatformHost
		void LaunchGame() override;
		void Minimize() override;
		void Close() override;
		void ShowMessage(const std::string& title, const std::string& body) override;

	private:
		static LRESULT CALLBACK WindowProcThunk(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);
		LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

		/// Allocates (or reallocates) the DIB section for the current DPI scale.
		bool CreateSurface();
		void DestroySurface();

		void OnPaint();
		void OnTimer();
		void OnDpiChanged(uint32 dpi, const RECT& suggested);

		/// Converts a physical client point into the view's logical coordinate space.
		Point ToLogical(POINT physical) const;

		/// Asks DWM for rounded corners. A no-op before Windows 11.
		void ApplyRoundedCorners() const;

		LauncherModel& m_model;
		UpdateWorker& m_worker;
		LauncherView m_view;

		HWND m_handle = nullptr;
		HINSTANCE m_instance = nullptr;

		HBITMAP m_dib = nullptr;
		HDC m_memoryDc = nullptr;
		Color* m_surface = nullptr;
		int32 m_surfaceWidth = 0;
		int32 m_surfaceHeight = 0;

		float m_dpiScale = 1.0f;
		bool m_trackingMouse = false;
		uint32 m_lastSeenVersion = 0;
	};
}
