// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "main_window.h"
#include "resource.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"

#ifdef _WIN32
#	include "imgui_impl_win32.h"
#	include "imgui_impl_dx11.h"
#	include "imgui_internal.h"
#	include "graphics/d3d11/graphics_device_d3d11.h"
#endif


// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


namespace mmo
{
	static constexpr char* s_mainWindowClassName = "MainWindow";

	static bool s_initialized = false;

	MainWindow::MainWindow()
		: m_windowHandle(nullptr)
	{
		// Create the native window
		CreateWindowHandle();

		// Initialize the graphics device
		GraphicsDeviceDesc desc;
		desc.customWindowHandle = m_windowHandle;
		auto& device = GraphicsDevice::CreateD3D11(desc);

		m_worlds.push_back("Alesta");
		m_worlds.push_back("Test");

		// Initialize imgui
		InitImGui();

		s_initialized = true;
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

	void MainWindow::RenderImGui()
	{
		// Start the Dear ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

		// Get the viewport and make the dockspace window fullscreen
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->GetWorkPos());
		ImGui::SetNextWindowSize(viewport->GetWorkSize());
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (m_dockSpaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		// Begin the dockspace window
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("DockSpace", nullptr, window_flags);
		ImGui::PopStyleVar();
		ImGui::PopStyleVar(2);
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), m_dockSpaceFlags);

			// The main menu
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("Project"))
				{
					if (ImGui::MenuItem("Worlds")) m_showWorlds = true;
					if (ImGui::MenuItem("Close", nullptr))
					{
						// Terminate the application
						PostQuitMessage(0);
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenuBar();
			}

			// Add a frame
			if (ImGui::Begin("Objects", &m_showObjects))
			{
				ImGui::Text("Test");
			}
			ImGui::End();

			// Add the viewport
			if (ImGui::Begin("Viewport", &m_showViewport))
			{
			}
			ImGui::End();

			static auto vector_getter = [](void* vec, int idx, const char** out_text)
			{
				auto& vector = *static_cast<std::vector<std::string>*>(vec);
				if (idx < 0 || idx >= static_cast<int>(vector.size())) { return false; }
				*out_text = vector.at(idx).c_str();
				return true;
			};

			// World window
			if (m_showWorlds)
			{
				if (ImGui::Begin("Worlds", &m_showWorlds))
				{
					ImGui::PushItemWidth(-1);
					if (ImGui::ListBox("", &m_selectedWorld, vector_getter, (void*)&m_worlds, m_worlds.size(), -1))
					{
						// Handle selection changed event
					}
					ImGui::PopItemWidth();

					ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
					ImGui::Button("Open");
					ImGui::PopItemFlag();
				}
				ImGui::End();
			}

			// Initialize the layout
			if (m_applyDefaultLayout)
			{
				// Set the default dock layout
				ImGuiDefaultDockLayout();
			}
		}
		ImGui::End();

		// Rendering
		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		// Update and Render additional Platform Windows
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}

	void MainWindow::ImGuiDefaultDockLayout()
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");

		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace); // Add empty node
		ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

		ImGuiID dock_main_id = dockspace_id; // This variable will track the document node, however we are not using it here as we aren't docking anything into it.
		ImGuiID dock_id_prop = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 300.0f / ImGui::GetMainViewport()->Size.x, nullptr, &dock_main_id);

		ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
		ImGui::DockBuilderDockWindow("Objects", dock_id_prop);
		ImGui::DockBuilderFinish(dockspace_id);

		// Finish default layout
		m_applyDefaultLayout = false;
	}

	void MainWindow::ShutdownImGui()
	{

	}

	void MainWindow::InitImGui()
	{
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
		io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP!
		io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		// Setup Platform/Renderer bindings
		ImGui_ImplWin32_Init(m_windowHandle);

		// This is hacky :/ Get the d3d11 device and immediate device context required for rendering
		auto& d3d11Dev = reinterpret_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
		ID3D11Device& device = d3d11Dev;
		ID3D11DeviceContext& immContext = d3d11Dev;
		ImGui_ImplDX11_Init(&device, &immContext);

		// Load fonts
		io.Fonts->AddFontDefault();

		// Dockspace flags
		m_dockSpaceFlags = ImGuiDockNodeFlags_None;
	}

	LRESULT MainWindow::WindowMsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		// Handle window messages for ImGui like mouse and key inputs
		if (ImGui_ImplWin32_WndProcHandler(wnd, msg, wparam, lparam))
			return true;

		// If this is the creation message, we will set the class long to the instance pointer
		// of the mainwindow instance which was provided in CreateWindowEx as the last parameter
		// so that we can get the instance everywhere from the window handle.
		if (msg == WM_NCCREATE)
		{
			LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
			SetWindowLongPtr(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
		}
		else
		{
			// Retrieve the window instance
			MainWindow* window = reinterpret_cast<MainWindow*>(GetWindowLongPtr(wnd, GWLP_USERDATA));
			if (window)
			{
				// Call the MsgProc instance function
				return window->MsgProc(wnd, msg, wparam, lparam);
			}
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
		case WM_PAINT:
			if (s_initialized)
			{
				GraphicsDevice::Get().Reset();
				GraphicsDevice::Get().GetAutoCreatedWindow()->Clear(ClearFlags::All);
				RenderImGui();
				GraphicsDevice::Get().GetAutoCreatedWindow()->Update();
			}
			return 0;
		case WM_SIZE:
			if (s_initialized)
			{
				GraphicsDevice::Get().GetAutoCreatedWindow()->Resize(LOWORD(lparam), HIWORD(lparam));
			}
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
