// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "main_window.h"
#include "resource.h"

#include "base/macros.h"

#include "imgui.h"

#ifdef _WIN32
#	include "imgui_impl_win32.h"
#	include "imgui_impl_dx11.h"
#endif


namespace mmo
{
	static constexpr char* s_mainWindowClassName = "MainWindow";


	MainWindow::MainWindow()
		: m_windowHandle(nullptr)
	{
		CreateWindowHandle();

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
		//io.ConfigViewportsNoAutoMerge = true;
		//io.ConfigViewportsNoTaskBarIcon = true;
		//io.ConfigViewportsNoDefaultParent = true;
		//io.ConfigDockingAlwaysTabBar = true;
		//io.ConfigDockingTransparentPayload = true;
#if 1
		io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP!
		io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI
#endif

		// Setup Dear ImGui style
		//ImGui::StyleColorsDark();
		ImGui::StyleColorsClassic();

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		// Setup Platform/Renderer bindings
		ImGui_ImplWin32_Init(m_windowHandle);
		//TODO: ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

		// Load fonts
		io.Fonts->AddFontDefault();
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

		m_windowHandle = CreateWindowEx(0, TEXT(s_mainWindowClassName), TEXT("MMOEdit"), WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720, nullptr, nullptr, GetModuleHandle(nullptr), this);

		ShowWindow(m_windowHandle, SW_SHOWNORMAL);
		UpdateWindow(m_windowHandle);
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
