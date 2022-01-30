// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "main_window.h"

#include <imgui_internal.h>

#include "configuration.h"
#include "assets/asset_registry.h"
#include "base/macros.h"
#include "base/utilities.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"

#ifdef _WIN32
#	include <windowsx.h>

#	include "backends/imgui_impl_dx11.h"
#	include "backends/imgui_impl_win32.h"
#	include "graphics/d3d11/graphics_device_d3d11.h"
#	include "graphics/d3d11/render_texture_d3d11.h"

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace mmo
{
	static const char* s_mainWindowClassName = "MainWindow";
	static bool s_initialized = false;
	
	MainWindow::MainWindow(Configuration& config)
		: m_config(config)
		, m_windowHandle(nullptr)
		, m_imguiContext(nullptr)
		, m_fileLoaded(false)
		, m_worldsWindow(m_project)
	{
		// Create the native platform window
		CreateWindowHandle();

		// Initialize the graphics device
		GraphicsDeviceDesc desc;
		desc.customWindowHandle = m_windowHandle;
		desc.vsync = false;
		auto& device = GraphicsDevice::CreateD3D11(desc);

		// Initialize imgui
		InitImGui();

		// Try to initialize the asset registry
		if (!config.assetRegistryPath.empty())
		{
			// Initialize the asset registry using the given path
			mmo::AssetRegistry::Initialize(config.assetRegistryPath, {});
		}
		else
		{
			WLOG("Unable to initialize asset registry: No asset registry path provided!");
		}
		
		// Setup the viewport render texture
		s_initialized = true;

		// Log success
		ILOG("MMO Edit initialized");

		// Try to load project
		if (!m_project.Load(m_config.projectPath))
		{
			ELOG("Unable to load project!");
		}
	}

	MainWindow::~MainWindow()
	{
		// No longer initialized
		s_initialized = false;
		
		// Terminate ImGui
		ShutdownImGui();

		// Tear down graphics device
		GraphicsDevice::Destroy();
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

		const UINT desktopWidth = ::GetSystemMetrics(SM_CXSCREEN);
		const UINT desktopHeight = ::GetSystemMetrics(SM_CYSCREEN);
		const UINT w = desktopWidth * 0.75f;
		const UINT h = desktopHeight * 0.75f;
		const UINT x = desktopWidth / 2 - w / 2;
		const UINT y = desktopHeight / 2 - h / 2;

		m_windowHandle = CreateWindowEx(0, TEXT(s_mainWindowClassName), TEXT("MMO Edit"), WS_OVERLAPPEDWINDOW,
			x, y, w, h, nullptr, nullptr, GetModuleHandle(nullptr), this);

		// We accept file drops
		DragAcceptFiles(m_windowHandle, TRUE);

		ShowWindow(m_windowHandle, SW_SHOWNORMAL);
		UpdateWindow(m_windowHandle);
	}

	void MainWindow::HandleEditorWindow(EditorWindowBase& window)
	{
		bool visible = window.IsVisible();
		if (visible)
		{
			ImGuiWindowFlags flags = ImGuiWindowFlags_None;

			if (!window.IsResizable()) flags |= ImGuiWindowFlags_NoResize;
			if (!window.IsDockable()) flags |= ImGuiWindowFlags_NoDocking;

			if (ImGui::Begin(window.GetName().c_str(), &visible, flags))
			{
				window.Draw();
			}
			ImGui::End();

			window.SetVisible(visible);
		}
	}

	void MainWindow::HandleMainMenu()
	{
		if (ImGui::BeginMenuBar())
		{
			// File menu
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Save Project", nullptr, nullptr, m_projectLoaded))
				{
					if (!m_project.Save(m_config.projectPath))
					{
						ELOG("Failed to save project");
					}
				}
				
				ImGui::Separator();
					
				if (ImGui::MenuItem("Exit", nullptr))
				{
					// Terminate the application
					PostQuitMessage(0);
				}

				ImGui::EndMenu();
			}

			// View menu
			if (ImGui::BeginMenu("View"))
			{
				for (const auto& window : m_editorWindows)
				{
					if (ImGui::MenuItem(window->GetName().c_str(), nullptr, window->IsVisible()))
					{
						window->Open();
					}
				}

				if (!m_editorWindows.empty())
				{
					ImGui::Separator();	
				}
				
				m_worldsWindow.DrawViewMenuItem();

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}
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
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (m_dockSpaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		// Begin the dockspace window with disabled padding
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("DockSpace", nullptr, window_flags);
		ImGui::PopStyleVar(3);
		{
			const auto dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), m_dockSpaceFlags);
			
			// The main menu
			HandleMainMenu();
			
			// Draw the editor window modules
			for (const auto& window : m_editorWindows)
			{
				HandleEditorWindow(*window);
			}
			
			// Draw the editor window modules
			for (const auto& editor : m_editors)
			{
				editor->Draw();
			}

			// Render log window
			m_worldsWindow.Draw();

			const ImGuiID dockSpaceId = ImGui::GetID("MyDockSpace");
			std::erase_if(m_uninitializedEditorInstances, [dockSpaceId](const String& name)
			{
				ImGui::DockBuilderDockWindow(name.c_str(), dockSpaceId);
				return true;
			});
			ImGui::DockBuilderFinish(dockSpaceId);

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
		const ImGuiID dockSpaceId = ImGui::GetID("MyDockSpace");

		ImGui::DockBuilderRemoveNode(dockSpaceId);
		ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar); // Add empty node
		ImGui::DockBuilderSetNodeSize(dockSpaceId, ImGui::GetMainViewport()->Size);

		auto dockMainId = dockSpaceId; // This variable will track the document node, however we are not using it here as we aren't docking anything into it.
		//auto dockLogId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 400.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);

		auto bottomId =  ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 400.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);
		auto topId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Up, 400.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);
		auto leftId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 400.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);
		auto rightId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 400.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);

		for(const auto& window : m_editorWindows)
		{
			if (!window->IsDockable()) continue;

			switch(window->GetDefaultDockDirection())
			{
			case DockDirection::Bottom:
				ImGui::DockBuilderDockWindow(window->GetName().c_str(), bottomId);
				break;
			case DockDirection::Left:
				ImGui::DockBuilderDockWindow(window->GetName().c_str(), leftId);
				break;
			case DockDirection::Right:
				ImGui::DockBuilderDockWindow(window->GetName().c_str(), rightId);
				break;
			case DockDirection::Top:
				ImGui::DockBuilderDockWindow(window->GetName().c_str(), topId);
				break;
			default:
				ImGui::DockBuilderDockWindow(window->GetName().c_str(), dockMainId);
				break;
			}
		}
		
		ImGui::DockBuilderFinish(dockSpaceId);

		// Finish default layout
		m_applyDefaultLayout = false;
	}

	void MainWindow::ShutdownImGui() const
	{
		ASSERT(m_imguiContext);

		// Shutdown d3d11 and win32 implementation of imgui
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();

		// And destroy the context
		ImGui::DestroyContext(m_imguiContext);
	}

	bool MainWindow::OnFileDrop(const std::string filename)
	{
		const std::filesystem::path p { filename };

		String extension = p.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

		for (const auto& import : m_imports)
		{
			if (!import->SupportsExtension(extension))
			{
				continue;
			}

			const bool result = import->ImportFromFile(p, m_selectedPath);
			if (result)
			{
				assetImported(m_selectedPath);
			}

			return result;
		}

		WLOG("Unsupported file extension " << extension);
		
		return true;
	}

	void MainWindow::OnMouseButtonDown(uint32 button, uint16 x, uint16 y)
	{
		if (m_activeEditorInstance)
		{
			m_activeEditorInstance->OnMouseButtonDown(button, x, y);
		}
	}

	void MainWindow::OnMouseButtonUp(uint32 button, uint16 x, uint16 y)
	{
		if (m_activeEditorInstance)
		{
			m_activeEditorInstance->OnMouseButtonUp(button, x, y);
		}
	}

	void MainWindow::OnMouseMoved(uint16 x, uint16 y)
	{
		if (m_activeEditorInstance)
		{
			m_activeEditorInstance->OnMouseMoved(x, y);
		}
	}
	
	void MainWindow::AddEditorWindow(std::unique_ptr<EditorWindowBase> editorWindow)
	{
		ASSERT(editorWindow);
		m_editorWindows.emplace_back(std::move(editorWindow));
	}

	void MainWindow::RemoveEditorWindow(const String& name)
	{
		std::erase_if(m_editorWindows, [&name](const std::unique_ptr<EditorWindowBase>& window)
		{
			return window->GetName() == name;
		});
	}

	void MainWindow::AddImport(std::unique_ptr<ImportBase> import)
	{
		ASSERT(import);
		m_imports.emplace_back(std::move(import));
	}

	void MainWindow::AddEditor(std::unique_ptr<EditorBase> editor)
	{
		ASSERT(editor);
		m_editors.emplace_back(std::move(editor));
	}

	void MainWindow::ApplyDefaultStyle()
	{
		ImGuiStyle* style = &ImGui::GetStyle();
		ImVec4* colors = style->Colors;

		colors[ImGuiCol_Text]                   = ImVec4(1.000f, 1.000f, 1.000f, 1.000f);
		colors[ImGuiCol_TextDisabled]           = ImVec4(0.500f, 0.500f, 0.500f, 1.000f);
		colors[ImGuiCol_WindowBg]               = ImVec4(0.180f, 0.180f, 0.180f, 1.000f);
		colors[ImGuiCol_ChildBg]                = ImVec4(0.280f, 0.280f, 0.280f, 0.000f);
		colors[ImGuiCol_PopupBg]                = ImVec4(0.313f, 0.313f, 0.313f, 1.000f);
		colors[ImGuiCol_Border]                 = ImVec4(0.266f, 0.266f, 0.266f, 1.000f);
		colors[ImGuiCol_BorderShadow]           = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
		colors[ImGuiCol_FrameBg]                = ImVec4(0.160f, 0.160f, 0.160f, 1.000f);
		colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.200f, 0.200f, 0.200f, 1.000f);
		colors[ImGuiCol_FrameBgActive]          = ImVec4(0.280f, 0.280f, 0.280f, 1.000f);
		colors[ImGuiCol_TitleBg]                = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
		colors[ImGuiCol_TitleBgActive]          = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
		colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
		colors[ImGuiCol_MenuBarBg]              = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
		colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.160f, 0.160f, 0.160f, 1.000f);
		colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.277f, 0.277f, 0.277f, 1.000f);
		colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.300f, 0.300f, 0.300f, 1.000f);
		colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
		colors[ImGuiCol_CheckMark]              = ImVec4(1.000f, 1.000f, 1.000f, 1.000f);
		colors[ImGuiCol_SliderGrab]             = ImVec4(0.391f, 0.391f, 0.391f, 1.000f);
		colors[ImGuiCol_SliderGrabActive]       = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
		colors[ImGuiCol_Button]                 = ImVec4(1.000f, 1.000f, 1.000f, 0.000f);
		colors[ImGuiCol_ButtonHovered]          = ImVec4(1.000f, 1.000f, 1.000f, 0.156f);
		colors[ImGuiCol_ButtonActive]           = ImVec4(1.000f, 1.000f, 1.000f, 0.391f);
		colors[ImGuiCol_Header]                 = ImVec4(0.313f, 0.313f, 0.313f, 1.000f);
		colors[ImGuiCol_HeaderHovered]          = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
		colors[ImGuiCol_HeaderActive]           = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
		colors[ImGuiCol_Separator]              = colors[ImGuiCol_Border];
		colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.391f, 0.391f, 0.391f, 1.000f);
		colors[ImGuiCol_SeparatorActive]        = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
		colors[ImGuiCol_ResizeGrip]             = ImVec4(1.000f, 1.000f, 1.000f, 0.250f);
		colors[ImGuiCol_ResizeGripHovered]      = ImVec4(1.000f, 1.000f, 1.000f, 0.670f);
		colors[ImGuiCol_ResizeGripActive]       = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
		colors[ImGuiCol_Tab]                    = ImVec4(0.098f, 0.098f, 0.098f, 1.000f);
		colors[ImGuiCol_TabHovered]             = ImVec4(0.352f, 0.352f, 0.352f, 1.000f);
		colors[ImGuiCol_TabActive]              = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
		colors[ImGuiCol_TabUnfocused]           = ImVec4(0.098f, 0.098f, 0.098f, 1.000f);
		colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
		colors[ImGuiCol_DockingPreview]         = ImVec4(1.000f, 0.391f, 0.000f, 0.781f);
		colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.180f, 0.180f, 0.180f, 1.000f);
		colors[ImGuiCol_PlotLines]              = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
		colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
		colors[ImGuiCol_PlotHistogram]          = ImVec4(0.586f, 0.586f, 0.586f, 1.000f);
		colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
		colors[ImGuiCol_TextSelectedBg]         = ImVec4(1.000f, 1.000f, 1.000f, 0.156f);
		colors[ImGuiCol_DragDropTarget]         = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
		colors[ImGuiCol_NavHighlight]           = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
		colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
		colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);
		colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);

		style->ChildRounding = 4.0f;
		style->FrameBorderSize = 1.0f;
		style->FrameRounding = 2.0f;
		style->GrabMinSize = 7.0f;
		style->PopupRounding = 2.0f;
		style->ScrollbarRounding = 12.0f;
		style->ScrollbarSize = 13.0f;
		style->TabBorderSize = 1.0f;
		style->TabRounding = 0.0f;
		style->WindowRounding = 4.0f;
	}

	void MainWindow::InitImGui()
	{
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		m_imguiContext = ImGui::CreateContext();
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
		m_dockSpaceFlags = ImGuiDockNodeFlags_None;//ImGuiDockNodeFlags_AutoHideTabBar;

		ApplyDefaultStyle();

		SetTimer(m_windowHandle, 0, 5, [] (HWND hwnd, UINT uint, UINT_PTR uintPtr, DWORD dword) { InvalidateRect(hwnd, nullptr, FALSE); });
	}

	void MainWindow::RenderSimpleNodeEditor()
	{
		
	}

	LRESULT MainWindow::WindowMsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		// Handle window messages for ImGui like mouse and key inputs
		if (ImGui_ImplWin32_WndProcHandler(wnd, msg, wparam, lparam))
			return true;

		// If this is the creation message, we will set the class long to the instance pointer
		// of the main window instance which was provided in CreateWindowEx as the last parameter
		// so that we can get the instance everywhere from the window handle.
		if (msg == WM_NCCREATE)
		{
			auto createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
			SetWindowLongPtr(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
		}
		else
		{
			// Retrieve the window instance
			auto* window = reinterpret_cast<MainWindow*>(GetWindowLongPtr(wnd, GWLP_USERDATA));
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
		        PAINTSTRUCT ps;
		        HDC hdc = BeginPaint(wnd, &ps);
				
				beforeUiUpdate();

				// Now render the main
				GraphicsDevice::Get().GetAutoCreatedWindow()->Activate();
				GraphicsDevice::Get().GetAutoCreatedWindow()->Clear(ClearFlags::All);
				RenderImGui();
				GraphicsDevice::Get().GetAutoCreatedWindow()->Update();
				
		        EndPaint(wnd, &ps);
			}
			return 0;
		case WM_LBUTTONDOWN:
			OnMouseButtonDown(0, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		case WM_RBUTTONDOWN:
			OnMouseButtonDown(1, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		case WM_MBUTTONDOWN:
			OnMouseButtonDown(2, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		case WM_LBUTTONUP:
			OnMouseButtonUp(0, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		case WM_RBUTTONUP:
			OnMouseButtonUp(1, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		case WM_MBUTTONUP:
			OnMouseButtonUp(2, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		case WM_MOUSEMOVE:
			OnMouseMoved(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		case WM_DROPFILES:
			{
				UINT fileCount = DragQueryFileA((HDROP)wparam, 0xFFFFFFFF, nullptr, 0);
				for (UINT i = 0; i < fileCount; ++i)
				{
					UINT fileNameLen = DragQueryFileA((HDROP)wparam, i, nullptr, 0);

					CHAR* fileName = new CHAR[fileNameLen + 1];
					if (DragQueryFileA((HDROP)wparam, i, fileName, fileNameLen + 1) != 0)
					{
						OnFileDrop(fileName);
					}
					delete[] fileName;
				}

				DragFinish((HDROP)wparam);
			}
			return 0;
		case WM_SIZE:
			if (s_initialized)
			{
				GraphicsDevice::Get().GetAutoCreatedWindow()->Resize(LOWORD(lparam), HIWORD(lparam));
			}
			return 0;
		}

		return DefWindowProc(wnd, msg, wparam, lparam);
	}

	bool MainWindow::OpenAsset(const Path& assetPath)
	{
		auto extension = assetPath.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

		for (const auto& editor : m_editors)
		{
			if (!editor->CanLoadAsset(extension))
			{
				continue;
			}

			const bool result = editor->OpenAsset(assetPath);
			if (result)
			{
				m_uninitializedEditorInstances.emplace_back(assetPath.filename().string());
			}
			return result;
		}

		WLOG("No editor available for asset " << assetPath);
		return false;
	}

	void MainWindow::SetActiveEditorInstance(EditorInstance* instance)
	{
		m_activeEditorInstance = instance;
	}

	void MainWindow::EditorInstanceClosed(EditorInstance& instance)
	{
		if (&instance == m_activeEditorInstance)
		{
			m_activeEditorInstance = nullptr;
		}
	}

	void MainWindow::ShowAssetCreationContextMenu()
	{
		for (const auto& editor : m_editors)
		{
			if (!editor->CanCreateAssets())
			{
				continue;
			}

			editor->AddCreationContextMenuItems();
		}
	}
}
