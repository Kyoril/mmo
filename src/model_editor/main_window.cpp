// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "main_window.h"

#include "base/macros.h"
#include "log/default_log_levels.h"
#include "graphics/graphics_device.h"
#include "assets/asset_registry.h"
#include "configuration.h"

#include "base/filesystem.h"
#include "base/utilities.h"

#ifdef _WIN32
#	include "imgui_impl_win32.h"
#	include "imgui_impl_dx11.h"
#	include "imgui_internal.h"
#	include "graphics/d3d11/graphics_device_d3d11.h"
#	include "graphics/d3d11/render_texture_d3d11.h"

#	include <windowsx.h>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif


namespace mmo
{
	static constexpr char* s_mainWindowClassName = "MainWindow";
	static bool s_initialized = false;


	MainWindow::MainWindow(Configuration& config)
		: m_config(config)
		, m_windowHandle(nullptr)
		, m_imguiContext(nullptr)
		, m_lastMouseX(0)
		, m_lastMouseY(0)
		, m_leftButtonPressed(false)
		, m_rightButtonPressed(false)
		, m_fileLoaded(false)
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
		ILOG("Model Editor initialized");
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

		m_windowHandle = CreateWindowEx(0, TEXT(s_mainWindowClassName), TEXT("MMO Model Editor"), WS_OVERLAPPEDWINDOW,
			x, y, w, h, nullptr, nullptr, GetModuleHandle(nullptr), this);

		// We accept file drops
		DragAcceptFiles(m_windowHandle, TRUE);

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
			if (ImGui::BeginMenuBar())
			{
				// File menu
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("Save Mesh", nullptr, false, m_fileLoaded))
					{
						// TODO: export mesh / open export dialog
						DLOG("TODO: Save mesh...");

						auto filePtr = AssetRegistry::CreateNewFile("Models/Test.mesh");
						if (filePtr == nullptr)
						{
							ELOG("Unable to save mesh!");
							return;
						}

						filePtr->write("Hello world\n", 11);
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
					m_logWindow.DrawViewMenuItem();
					m_viewportWindow.DrawViewMenuItem();

					ImGui::EndMenu();
				}

				ImGui::EndMenuBar();
			}

			// Draw the viewport window
			m_viewportWindow.Draw();

			// Render log window
			m_logWindow.Draw();

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
		const auto dockLogId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 300.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);
		
		ImGui::DockBuilderDockWindow("Viewport", dockMainId);
		ImGui::DockBuilderDockWindow("Log", dockLogId);
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

	bool MainWindow::OnFileDrop(std::string filename)
	{
		m_leftButtonPressed = false;
		m_rightButtonPressed = false;

		const std::filesystem::path p { filename };
		if (_strcmpi(p.extension().string().c_str(), ".fbx") == 0)
		{
			ILOG("Importing fbx file " << filename << "...");
			if (!m_importer.LoadScene(filename.c_str()))
			{
				ELOG("Failed to load fbx file " << filename);
				return false;
			}

			// TODO: Change this, but for now we will create a vertex and index buffer from the first mesh that was found
			const auto& meshes = m_importer.GetMeshEntries();
			if (!meshes.empty())
			{
				const auto& mesh = meshes.front();

				std::vector<POS_COL_VERTEX> vertices;
				vertices.resize(mesh.vertices.size());

				for (size_t i = 0; i < mesh.vertices.size(); ++i)
				{
					vertices[i].pos[0] = mesh.vertices[i].position.x;
					vertices[i].pos[1] = mesh.vertices[i].position.y;
					vertices[i].pos[2] = mesh.vertices[i].position.z;
					vertices[i].color = 0xFFAEAEAE;
				}

				auto vertBuf = GraphicsDevice::Get().CreateVertexBuffer(mesh.vertices.size(), sizeof(POS_COL_VERTEX), false, &vertices[0]);

				std::vector<uint16> indices;
				indices.resize(mesh.indices.size());

				for (size_t i = 0; i < mesh.indices.size(); ++i)
				{
					indices[i] = static_cast<uint16>(mesh.indices[i]);
				}

				auto indexBuf = GraphicsDevice::Get().CreateIndexBuffer(mesh.indices.size(), IndexBufferSize::Index_16, &indices[0]);
				m_viewportWindow.SetMesh(std::move(vertBuf), std::move(indexBuf));

				m_fileLoaded = true;
			}
		}
		else
		{
			ELOG("Unsupported file extension '" << p.extension().string() << "'");
		}
		
		
		return true;
	}

	void MainWindow::OnMouseButtonDown(uint32 button, uint16 x, uint16 y)
	{
		m_lastMouseX = x;
		m_lastMouseY = y;

		if (button == 0)
		{
			m_leftButtonPressed = true;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = true;
		}
	}

	void MainWindow::OnMouseButtonUp(uint32 button, uint16 x, uint16 y)
	{
		if (button == 0)
		{
			m_leftButtonPressed = false;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = false;
		}
	}

	void MainWindow::OnMouseMoved(uint16 x, uint16 y)
	{
		// Calculate mouse move delta
		const int16 deltaX = x - m_lastMouseX;
		const int16 deltaY = y - m_lastMouseY;

		if (m_rightButtonPressed)
		{
			m_viewportWindow.MoveCamera(Vector3(static_cast<float>(deltaX) / 96.0f, static_cast<float>(deltaY) / 96.0f, 0.0f));
		}
		else if (m_leftButtonPressed)
		{
			m_viewportWindow.MoveCameraTarget(Vector3(static_cast<float>(deltaX) / 96.0f, static_cast<float>(deltaY) / 96.0f, 0.0f));
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
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
				// Render game viewport contents
				m_viewportWindow.Render();

				// Now render the main
				GraphicsDevice::Get().GetAutoCreatedWindow()->Activate();
				GraphicsDevice::Get().GetAutoCreatedWindow()->Clear(ClearFlags::All);
				RenderImGui();
				GraphicsDevice::Get().GetAutoCreatedWindow()->Update();
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
}
