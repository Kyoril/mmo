// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "main_window.h"
#include "resource.h"

#include "base/macros.h"


namespace mmo
{
	static constexpr char* s_mainWindowClassName = "MainWindow";

	MainWindow::MainWindow()
		: m_windowHandle(nullptr)
	{
		CreateWindowHandle();
	}

	void MainWindow::EnsureWindowClassCreated()
	{
		static bool s_windowClassCreated = false;
		if (!s_windowClassCreated)
		{
			WNDCLASSEX wc;
			ZeroMemory(&wc, sizeof(wc));

			wc.cbSize = sizeof(wc);
			wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hInstance = GetModuleHandle(nullptr);
			wc.lpfnWndProc = WindowMsgProc;
			wc.lpszClassName = TEXT(s_mainWindowClassName);
			wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1);
			RegisterClassEx(&wc);

			s_windowClassCreated = true;
		}
	}

	void MainWindow::CreateWindowHandle()
	{
		EnsureWindowClassCreated();

		if (m_windowHandle != nullptr)
		{
			return;
		}

		m_windowHandle = CreateWindowEx(0, TEXT(s_mainWindowClassName), TEXT("MMORPG Editor"), WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720, nullptr, nullptr, GetModuleHandle(nullptr), this);

		ShowWindow(m_windowHandle, SW_SHOWNORMAL);
		UpdateWindow(m_windowHandle);
	}

	void MainWindow::CreateStatusBar()
	{
	}

	LRESULT MainWindow::WindowMsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		// If this is the creation message, we will set the class long to the instance pointer
		// of the mainwindow instance which was provided in CreateWindowEx as the last parameter
		// so that we can get the instance everywhere from the window handle.
		if (msg == WM_NCCREATE)
		{
			LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
			SetClassLongPtr(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
		}
		else
		{
			// Retrieve the window instance
			MainWindow* window = reinterpret_cast<MainWindow*>(GetClassLongPtr(wnd, GWLP_USERDATA));
			ASSERT(window);

			// Call the MsgProc instance function
			return window->MsgProc(wnd, msg, wparam, lparam);
		}

		// Return default window procedure
		return DefWindowProc(wnd, msg, wparam, lparam);
	}

	LRESULT MainWindow::MsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		switch (msg)
		{
		case WM_CLOSE:
			DestroyWindow(wnd);
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_COMMAND:
			switch (LOWORD(wparam))
			{
			case ID_PROJECT_EXIT:
				DestroyWindow(wnd);
				return 0;
			}
		}

		return DefWindowProc(wnd, msg, wparam, lparam);
	}
}
