// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "launcher_window.h"

#include "canvas.h"
#include "launcher_layout.h"

#include "base/macros.h"
#include "log/default_log_levels.h"

#include <array>
#include <windowsx.h>

namespace mmo
{
	namespace
	{
		constexpr const wchar_t* WindowClassName = L"MmoLauncherWindow";
		constexpr const wchar_t* WindowTitle = L"Alestia Online";

		/// The render clock. Progress easing and hover fades are driven from here, and
		/// the model is polled here rather than posting a message per progress
		/// callback, which would flood the queue from several worker threads.
		constexpr UINT_PTR RenderTimerId = 1;
		constexpr UINT RenderTimerIntervalMs = 16;

		constexpr UINT WM_APP_SELF_UPDATE_FINISHED = WM_APP + 1;

		/// DWMWA_WINDOW_CORNER_PREFERENCE and DWMWCP_DONOTROUND. Spelled out rather than
		/// including dwmapi.h so the launcher does not hard link dwmapi.lib for a
		/// cosmetic call that does nothing before Windows 11.
		///
		/// Rounding is explicitly disabled: the ornate frame art draws its own corners,
		/// and letting DWM round the window would clip them off.
		constexpr DWORD DwmWindowCornerPreference = 33;
		constexpr DWORD DwmCornerPreferenceDoNotRound = 1;

		/// Opts the process into PerMonitorV2.
		///
		/// The CMake VS_DPI_AWARE property can only emit PerMonitor v1, which does not
		/// scale the non-client area and differs in its WM_DPICHANGED behaviour. This is
		/// resolved dynamically so the binary still runs on Windows without it.
		void EnablePerMonitorV2Dpi()
		{
			using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

			const HMODULE user32 = GetModuleHandleW(L"user32.dll");
			if (!user32)
			{
				return;
			}

			const auto setContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
				reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
			if (setContext)
			{
				setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
			}
		}

		uint32 GetWindowDpi(const HWND handle)
		{
			using GetDpiForWindowFn = UINT(WINAPI*)(HWND);

			const HMODULE user32 = GetModuleHandleW(L"user32.dll");
			if (user32)
			{
				const auto getDpi = reinterpret_cast<GetDpiForWindowFn>(
					reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
				if (getDpi)
				{
					return getDpi(handle);
				}
			}

			return USER_DEFAULT_SCREEN_DPI;
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
		DestroySurface();
	}

	bool LauncherWindow::Create(const HINSTANCE instance)
	{
		m_instance = instance;

		EnablePerMonitorV2Dpi();

		WNDCLASSEXW windowClass = {};
		windowClass.cbSize = sizeof(windowClass);
		// CS_DROPSHADOW gives the borderless window a system shadow, which is most of
		// what a layered window would have been used for.
		windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
		windowClass.lpfnWndProc = &LauncherWindow::WindowProcThunk;
		windowClass.hInstance = instance;
		// IDC_ARROW expands to the ANSI MAKEINTRESOURCE form because UNICODE is not
		// defined project-wide, but it is really just an integer resource id, so casting
		// it to the wide form is safe and keeps this on the W entry points.
		windowClass.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
		windowClass.lpszClassName = WindowClassName;

		if (!RegisterClassExW(&windowClass))
		{
			ELOG("Failed to register the launcher window class (error " << GetLastError() << ")");
			return false;
		}

		// WS_MINIMIZEBOX and WS_SYSMENU are required even though no system frame is
		// visible: without them the taskbar minimize/restore animation does not run and
		// clicking the taskbar button will not restore the window.
		const DWORD style = WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN;

		m_handle = CreateWindowExW(
			WS_EX_APPWINDOW,
			WindowClassName,
			WindowTitle,
			style,
			CW_USEDEFAULT, CW_USEDEFAULT,
			layout::WindowWidth, layout::WindowHeight,
			nullptr, nullptr, instance, this);

		if (!m_handle)
		{
			ELOG("Failed to create the launcher window (error " << GetLastError() << ")");
			return false;
		}

		m_dpiScale = static_cast<float>(GetWindowDpi(m_handle)) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);

		if (!m_view.Initialize(m_dpiScale))
		{
			ELOG("Failed to initialize the launcher view");
			return false;
		}

		if (!CreateSurface())
		{
			return false;
		}

		// Size to the scaled surface and center on the work area of the monitor it
		// landed on, now that the real DPI is known.
		HMONITOR monitor = MonitorFromWindow(m_handle, MONITOR_DEFAULTTONEAREST);
		MONITORINFO monitorInfo = { sizeof(monitorInfo) };
		GetMonitorInfoW(monitor, &monitorInfo);

		const int32 x = monitorInfo.rcWork.left +
			((monitorInfo.rcWork.right - monitorInfo.rcWork.left) - m_surfaceWidth) / 2;
		const int32 y = monitorInfo.rcWork.top +
			((monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) - m_surfaceHeight) / 2;

		SetWindowPos(m_handle, nullptr, x, y, m_surfaceWidth, m_surfaceHeight, SWP_NOZORDER);

		ApplyCornerPreference();

		SetTimer(m_handle, RenderTimerId, RenderTimerIntervalMs, nullptr);

		ShowWindow(m_handle, SW_SHOW);
		UpdateWindow(m_handle);
		return true;
	}

	void LauncherWindow::ApplyCornerPreference() const
	{
		using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

		const HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
		if (!dwmapi)
		{
			return;
		}

		const auto setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
			reinterpret_cast<void*>(GetProcAddress(dwmapi, "DwmSetWindowAttribute")));
		if (setAttribute)
		{
			const DWORD preference = DwmCornerPreferenceDoNotRound;
			setAttribute(m_handle, DwmWindowCornerPreference, &preference, sizeof(preference));
		}

		FreeLibrary(dwmapi);
	}

	bool LauncherWindow::CreateSurface()
	{
		DestroySurface();

		m_surfaceWidth = m_view.Scale(layout::WindowWidth);
		m_surfaceHeight = m_view.Scale(layout::WindowHeight);

		BITMAPINFO info = {};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = m_surfaceWidth;
		// Negative height selects a top-down DIB. A bottom-up one would place row 0 at
		// the bottom and silently invert every rectangle in the layout.
		info.bmiHeader.biHeight = -m_surfaceHeight;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;

		void* bits = nullptr;
		m_dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!m_dib || !bits)
		{
			ELOG("Failed to create the launcher surface (error " << GetLastError() << ")");
			return false;
		}

		m_memoryDc = CreateCompatibleDC(nullptr);
		if (!m_memoryDc)
		{
			ELOG("Failed to create the launcher device context (error " << GetLastError() << ")");
			return false;
		}

		SelectObject(m_memoryDc, m_dib);

		// The DIB's layout is already the compositor's pixel format, so the canvas can
		// draw straight into it and BitBlt can present it with no intermediate.
		m_surface = static_cast<Color*>(bits);
		return true;
	}

	void LauncherWindow::DestroySurface()
	{
		if (m_memoryDc)
		{
			DeleteDC(m_memoryDc);
			m_memoryDc = nullptr;
		}

		if (m_dib)
		{
			DeleteObject(m_dib);
			m_dib = nullptr;
		}

		m_surface = nullptr;
	}

	Point LauncherWindow::ToLogical(const POINT physical) const
	{
		return Point{
			static_cast<int32>(static_cast<float>(physical.x) / m_dpiScale),
			static_cast<int32>(static_cast<float>(physical.y) / m_dpiScale)
		};
	}

	int LauncherWindow::Run()
	{
		MSG message = {};
		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		return static_cast<int>(message.wParam);
	}

	void LauncherWindow::LaunchGame()
	{
		// The command line buffer must be writable for CreateProcessW.
		std::array<wchar_t, MAX_PATH> commandLine = { L"mmo_client.exe -uptodate" };

		STARTUPINFOW startupInfo = {};
		startupInfo.cb = sizeof(startupInfo);

		PROCESS_INFORMATION processInfo = {};

		if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0,
			nullptr, nullptr, &startupInfo, &processInfo))
		{
			const DWORD error = GetLastError();
			ELOG("Failed to start the game client (error " << error << ")");
			ShowMessage("Error", "Failed to start the game client.");
			return;
		}

		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);

		Close();
	}

	void LauncherWindow::NotifySelfUpdateFinished() const
	{
		// Called from the update thread. PostMessage is the whole point: destroying the
		// window is only valid on the thread that created it.
		PostMessageW(m_handle, WM_APP_SELF_UPDATE_FINISHED, 0, 0);
	}

	void LauncherWindow::Minimize()
	{
		ShowWindow(m_handle, SW_MINIMIZE);
	}

	void LauncherWindow::Close()
	{
		PostMessageW(m_handle, WM_CLOSE, 0, 0);
	}

	void LauncherWindow::ShowMessage(const std::string& title, const std::string& body)
	{
		MessageBoxA(m_handle, body.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
	}

	void LauncherWindow::OnPaint()
	{
		PAINTSTRUCT paint;
		const HDC dc = BeginPaint(m_handle, &paint);

		if (m_surface)
		{
			Canvas canvas(m_surface, m_surfaceWidth, m_surfaceHeight, m_surfaceWidth);
			m_view.Render(canvas);

			BitBlt(dc, 0, 0, m_surfaceWidth, m_surfaceHeight, m_memoryDc, 0, 0, SRCCOPY);
		}

		EndPaint(m_handle, &paint);
	}

	void LauncherWindow::OnTimer()
	{
		UpdateSnapshot snapshot;
		if (m_model.TryGetSnapshot(snapshot, m_lastSeenVersion))
		{
			m_view.ApplySnapshot(snapshot);
		}

		const float delta = static_cast<float>(RenderTimerIntervalMs) / 1000.0f;
		if (m_view.Tick(delta) || m_view.IsDirty())
		{
			InvalidateRect(m_handle, nullptr, FALSE);
		}
	}

	void LauncherWindow::OnDpiChanged(const uint32 dpi, const RECT& suggested)
	{
		m_dpiScale = static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);

		// Rebuilds the fonts at the new pixel size, rescales the splash and re-composites
		// the cached background.
		m_view.SetDpiScale(m_dpiScale);

		CreateSurface();

		// Using the suggested rectangle is mandatory rather than computing one: it is
		// what keeps a drag across a DPI boundary from fighting the cursor.
		SetWindowPos(m_handle, nullptr,
			suggested.left, suggested.top,
			suggested.right - suggested.left,
			suggested.bottom - suggested.top,
			SWP_NOZORDER | SWP_NOACTIVATE);

		InvalidateRect(m_handle, nullptr, FALSE);
	}

	LRESULT CALLBACK LauncherWindow::WindowProcThunk(const HWND handle, const UINT message,
		const WPARAM wParam, const LPARAM lParam)
	{
		LauncherWindow* self = nullptr;

		if (message == WM_NCCREATE)
		{
			const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			self = static_cast<LauncherWindow*>(create->lpCreateParams);
			self->m_handle = handle;
			SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		}
		else
		{
			self = reinterpret_cast<LauncherWindow*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
		}

		if (self)
		{
			return self->WindowProc(message, wParam, lParam);
		}

		return DefWindowProcW(handle, message, wParam, lParam);
	}

	LRESULT LauncherWindow::WindowProc(const UINT message, const WPARAM wParam, const LPARAM lParam)
	{
		switch (message)
		{
		case WM_PAINT:
			OnPaint();
			return 0;

		// The whole window is painted from the DIB every frame, so letting the system
		// erase it first would only cost a flash of background.
		case WM_ERASEBKGND:
			return 1;

		case WM_TIMER:
			if (wParam == RenderTimerId)
			{
				OnTimer();
			}
			return 0;

		case WM_NCHITTEST:
		{
			// The window is entirely client area; the caption is synthesized here so
			// that DefWindowProc drives the drag. Doing it this way rather than by hand
			// with SetCapture is what gives snapping, Aero shake and multi-monitor
			// behaviour for free.
			POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ScreenToClient(m_handle, &point);

			return m_view.HitTestCaption(ToLogical(point)) ? HTCAPTION : HTCLIENT;
		}

		// HTCAPTION also brings double-click-to-maximize, which a fixed size launcher
		// must not do.
		case WM_NCLBUTTONDBLCLK:
			return 0;

		case WM_MOUSEMOVE:
		{
			if (!m_trackingMouse)
			{
				// One shot: it has to be re-armed after every WM_MOUSELEAVE, or hover
				// state sticks once the cursor leaves and the Play button glows forever.
				TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_handle, 0 };
				TrackMouseEvent(&track);
				m_trackingMouse = true;
			}

			m_view.OnMouseMove(ToLogical(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }));
			return 0;
		}

		case WM_MOUSELEAVE:
			m_trackingMouse = false;
			m_view.OnMouseLeave();
			return 0;

		case WM_LBUTTONDOWN:
			// Capture is what makes press, drag away, release correctly not fire.
			SetCapture(m_handle);
			m_view.OnMouseDown(ToLogical(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }));
			return 0;

		case WM_LBUTTONUP:
			ReleaseCapture();
			m_view.OnMouseUp(ToLogical(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }));
			return 0;

		case WM_DPICHANGED:
			OnDpiChanged(HIWORD(wParam), *reinterpret_cast<const RECT*>(lParam));
			return 0;

		case WM_APP_SELF_UPDATE_FINISHED:
			DestroyWindow(m_handle);
			return 0;

		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE)
			{
				Close();
			}
			return 0;

		case WM_CLOSE:
			DestroyWindow(m_handle);
			return 0;

		case WM_DESTROY:
			KillTimer(m_handle, RenderTimerId);
			PostQuitMessage(0);
			return 0;
		}

		return DefWindowProcW(m_handle, message, wParam, lParam);
	}
}
