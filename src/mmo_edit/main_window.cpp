// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "main_window.h"

#include <imgui_internal.h>

#include "configuration.h"
#include "icon_font.h"
#include "editor_fonts.h"
#include "assets/asset_registry.h"
#include "base/macros.h"
#include "base/utilities.h"
#include "graphics/graphics_device.h"
#include "graphics/global_shader_parameters.h"
#include "log/default_log_levels.h"
#include "database.h"
#include "editor_windows/asset_window.h"
#include "proto_data/project.h"
#include "simple_file_format/sff_write.h"

#ifdef _WIN32
#	include <windowsx.h>

#	include "backends/imgui_impl_dx11.h"
#	include "backends/imgui_impl_win32.h"
#	include "graphics_d3d11/graphics_device_d3d11.h"
#	include "graphics_d3d11/render_texture_d3d11.h"

#include <dwmapi.h>

#pragma comment(lib, "Dwmapi.lib")

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace mmo
{
	static const char* s_mainWindowClassName = "MainWindow";
	static bool s_initialized = false;

	static bool s_showCreatureEditor = false;
	static bool s_showSpellEditor = false;

	namespace
	{
		// Draws text rotated 90 degrees counter-clockwise (reading bottom-to-top). 'topLeft' is the
		// top-left corner of the resulting vertical column, which is (text height) wide and
		// (text width) tall. Used for labels on the vertical collapse rails.
		void AddTextVertical(ImDrawList* drawList, const ImVec2& topLeft, ImU32 color, const char* text)
		{
			const ImVec2 textSize = ImGui::CalcTextSize(text);

			// Draw the glyphs horizontally anchored at topLeft (within the window's clip rect, which
			// spans the full width, so they are not culled), then rotate the generated vertices 90
			// degrees CCW about topLeft so the label reads bottom-to-top in a vertical column.
			const int vtxStart = drawList->VtxBuffer.Size;
			drawList->AddText(topLeft, color, text);
			const int vtxEnd = drawList->VtxBuffer.Size;
			for (int i = vtxStart; i < vtxEnd; ++i)
			{
				ImDrawVert& v = drawList->VtxBuffer[i];
				const float relX = v.pos.x - topLeft.x; // [0, textSize.x]
				const float relY = v.pos.y - topLeft.y; // [0, textSize.y]
				v.pos.x = topLeft.x + relY;
				v.pos.y = topLeft.y + (textSize.x - relX);
			}
		}
	}

	MainWindow::MainWindow(Configuration& config, proto::Project& project)
		: m_config(config)
#if _WIN32
		, m_windowHandle(nullptr)
#endif
		, m_imguiContext(nullptr)
		, m_fileLoaded(false)
		, m_project(project)
	{
		// Create the native platform window
		CreateWindowHandle();

		// Initialize the graphics device
		GraphicsDeviceDesc desc;
		desc.vsync = true;
#if _WIN32
        desc.customWindowHandle = m_windowHandle;
		auto& device = GraphicsDevice::CreateD3D11(desc);
#else
        auto& device = GraphicsDevice::CreateNull(desc);
#endif
		// Initialize imgui
		InitImGui();

		// Try to initialize the asset registry
		if (!config.assetRegistryPath.empty())
		{
			// Initialize the asset registry using the given path
			mmo::AssetRegistry::Initialize(config.assetRegistryPath, {});

			AssetRegistry::AddArchivePackage(std::filesystem::path(config.projectPath).parent_path() / "nav");

			// Load the project-wide global shader parameter registry (missing file = empty registry).
			GlobalShaderParameters::Get().LoadFromAsset(GlobalShaderParametersAssetPath);
		}
		else
		{
			WLOG("Unable to initialize asset registry: No asset registry path provided!");
		}

				
		// Setup the viewport render texture
		s_initialized = true;

		// Log success
		ILOG("MMO Edit initialized");
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
#ifdef _WIN32
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
#endif
	}

	void MainWindow::CreateWindowHandle()
	{
#ifdef _WIN32
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

		// Enable Mica
		DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW; // 2 = Mica (on Windows 11)
		DwmSetWindowAttribute(m_windowHandle, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

		BOOL supportDarkMode = TRUE;
		DwmSetWindowAttribute(m_windowHandle, DWMWA_USE_IMMERSIVE_DARK_MODE, &supportDarkMode, sizeof(supportDarkMode));

		DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
		DwmSetWindowAttribute(m_windowHandle, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

		ShowWindow(m_windowHandle, SW_SHOWNORMAL);
		UpdateWindow(m_windowHandle);
#endif
	}

	void MainWindow::HandleEditorWindow(EditorWindowBase& window)
	{
		bool visible = window.IsVisible();
		if (!visible)
		{
			return;
		}

		// Collapsed tool panels are not drawn here; they live on an edge rail instead.
		if (window.IsCollapsible() && window.IsCollapsed())
		{
			return;
		}

		ImGuiWindowFlags flags = ImGuiWindowFlags_None;

		if (!window.IsResizable()) flags |= ImGuiWindowFlags_NoResize;
		if (!window.IsDockable()) flags |= ImGuiWindowFlags_NoDocking;

		// When a panel was just expanded from the rail, send it back to the dock node it came from.
		if (window.IsCollapsible() && window.ConsumeRestoreDock() && window.GetLastDockId() != 0)
		{
			ImGui::SetNextWindowDockID(window.GetLastDockId(), ImGuiCond_Always);
		}

		if (ImGui::Begin(window.GetName().c_str(), &visible, flags))
		{
			if (window.IsCollapsible())
			{
				// Show the collapse button in this window's dock tab bar (next to the close button)
				// and react to clicks. The button itself lives in ImGui's docking code.
				ImGui::SetWindowDockCollapseButtonEnabled(true);

				if (ImGui::IsWindowDockCollapseRequested())
				{
					window.SetCollapsed(true);
					ImGui::MarkIniSettingsDirty();
				}

				// Remember where we are docked so we can restore it after a collapse round-trip.
				const ImGuiID dockId = ImGui::GetWindowDockID();
				if (dockId != 0)
				{
					window.SetLastDockId(dockId);
				}
			}

			window.Draw();
		}
		ImGui::End();

		window.SetVisible(visible);
	}

	void MainWindow::HandleMainMenu()
	{
		if (ImGui::BeginMenuBar())
		{
			// File menu
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exit", nullptr))
				{
#ifdef _WIN32
					// Terminate the application
					PostQuitMessage(0);
#endif
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
				
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}
	}
	
	void MainWindow::ShowCreatureEditor()
	{
		static int currentItem = -1;

		if (!s_showCreatureEditor) return;

		if (ImGui::Begin("Creatures", &s_showCreatureEditor))
		{
			ImGui::Columns(2, nullptr, true);
			ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 350.0f);
			
			ImGui::NextColumn();
			
			ImGui::BeginChild("creatureDetails", ImVec2(0, 0));

			ImGui::EndChild();

			ImGui::End();
		}
	}

	void MainWindow::HandleToolBar()
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetCurrentWindow()->MenuBarHeight()));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 50));
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags window_flags = 0
			| ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoSavedSettings
			;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.082f, 0.082f, 0.082f, 1.0f));
		ImGui::Begin("TOOLBAR", nullptr, window_flags);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();

		// Uniform toolbar button metrics so the bar reads as a single, deliberate strip.
		const float toolbarHeight = ImGui::GetWindowSize().y;
		const float buttonHeight = 34.0f;

		// Vertically center the row of buttons within the toolbar.
		ImGui::SetCursorPosY((toolbarHeight - buttonHeight) * 0.5f);

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, ImGui::GetStyle().ItemSpacing.y));

		// Helper that draws a consistent icon + label toolbar button.
		auto toolbarButton = [&](const char* icon, const char* label, const char* tooltip) -> bool
		{
			const String text = String(icon) + "  " + label;
			const bool pressed = ImGui::Button(text.c_str(), ImVec2(0.0f, buttonHeight));
			if (tooltip && ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", tooltip);
			}
			ImGui::SameLine();
			return pressed;
		};

		auto toolbarSeparator = [&]()
		{
			ImGui::SetCursorPosY((toolbarHeight - buttonHeight) * 0.5f + 4.0f);
			ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::SetCursorPosY((toolbarHeight - buttonHeight) * 0.5f);
		};

		// Accent the primary "Save All" action so the most common action stands out.
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.337f, 0.624f, 0.824f, 0.250f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.337f, 0.624f, 0.824f, 0.500f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.337f, 0.624f, 0.824f, 0.750f));
		if (toolbarButton(ICON_FA_FLOPPY_O, "Save All", "Save the project and all open editors"))
		{
			m_project.save(m_config.projectPath);

			// Save all open editors as well
			for (auto& editor : m_editors)
			{
				editor->Save();
			}
		}
		ImGui::PopStyleColor(3);

		if (toolbarButton(ICON_FA_UPLOAD, "Export to Client", "Export shared game data to the client data folder"))
		{
			ExportToClient();
		}

		toolbarSeparator();

		for(auto& window : m_editorWindows)
		{
			if (!window->HasToolbarButton())
			{
				continue;
			}

			const String& icon = window->GetToolbarButtonIcon();
			const String label = icon.empty()
				? window->GetToolbarButtonText()
				: icon + "  " + window->GetToolbarButtonText();

			if (ImGui::Button(label.c_str(), ImVec2(0.0f, buttonHeight)))
			{
				window->Open();
			}

			ImGui::SameLine();
		}

		ImGui::PopStyleVar(2);

		ImGui::End();
	}

	void MainWindow::HandleWelcomeScreen(unsigned int dockspaceId)
	{
		// Only show the welcome panel when the central document area has no editor docked into it.
		ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId);
		if (!centralNode || !centralNode->IsEmpty())
		{
			return;
		}

		ImGui::SetNextWindowPos(centralNode->Pos);
		ImGui::SetNextWindowSize(centralNode->Size);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
			| ImGuiWindowFlags_NoNavFocus;

		// While the user is dragging a window (i.e. trying to dock it), make the welcome overlay
		// transparent to input so the central dock node behind it remains a valid drop target.
		// Otherwise the overlay would be the hovered window and, being non-dockable, would suppress
		// the central docking preview.
		if (ImGui::GetCurrentContext()->MovingWindow != nullptr)
		{
			flags |= ImGuiWindowFlags_NoInputs;
		}

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.085f, 0.085f, 0.090f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		if (ImGui::Begin("##WelcomeScreen", nullptr, flags))
		{
			const ImVec2 region = ImGui::GetContentRegionAvail();

			// Roughly vertically center the hero block.
			const float blockHeight = 240.0f;
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (region.y - blockHeight) * 0.5f);

			const ImU32 accent = ImGui::GetColorU32(ImVec4(0.337f, 0.624f, 0.824f, 1.0f));
			const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);

			// Helper: draw a line of text horizontally centered in the window.
			auto centeredText = [&](const char* text, ImFont* font, ImU32 color)
			{
				if (font) ImGui::PushFont(font);
				const float textWidth = ImGui::CalcTextSize(text).x;
				ImGui::SetCursorPosX((region.x - textWidth) * 0.5f);
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::TextUnformatted(text);
				ImGui::PopStyleColor();
				if (font) ImGui::PopFont();
			};

			centeredText(ICON_FA_CUBES, GetEditorTitleFont(), accent);
			ImGui::Spacing();
			centeredText("MMO Edit", GetEditorTitleFont(), ImGui::GetColorU32(ImGuiCol_Text));
			ImGui::Spacing();
			centeredText("Select an asset from the Data Navigator to begin editing.", nullptr, dim);

			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::Spacing();

			// Centered row of quick-action buttons.
			const ImVec2 buttonSize(190.0f, 40.0f);
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float totalWidth = buttonSize.x * 3.0f + spacing * 2.0f;
			ImGui::SetCursorPosX((region.x - totalWidth) * 0.5f);

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 0.0f));

			if (ImGui::Button(ICON_FA_DATABASE "  Data Navigator", buttonSize))
			{
				for (const auto& window : m_editorWindows)
				{
					if (window->HasToolbarButton())
					{
						window->Open();
					}
				}
			}
			ImGui::SameLine();

			if (ImGui::Button(ICON_FA_FLOPPY_O "  Save All", buttonSize))
			{
				m_project.save(m_config.projectPath);
				for (auto& editor : m_editors)
				{
					editor->Save();
				}
			}
			ImGui::SameLine();

			if (ImGui::Button(ICON_FA_UPLOAD "  Export to Client", buttonSize))
			{
				ExportToClient();
			}

			ImGui::PopStyleVar();
		}
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}

	void MainWindow::ReadPanelCollapseSetting(const char* line)
	{
		// Format: "Panel Name=0|1" (panel names may contain spaces, so split on the last '=').
		const String text = line;
		const auto eq = text.rfind('=');
		if (eq == String::npos)
		{
			return;
		}

		const String name = text.substr(0, eq);
		const bool collapsed = (eq + 1 < text.size()) && text[eq + 1] == '1';

		for (const auto& window : m_editorWindows)
		{
			if (window->IsCollapsible() && window->GetName() == name)
			{
				window->SetCollapsed(collapsed);
				break;
			}
		}
	}

	void MainWindow::WritePanelCollapseSettings(ImGuiTextBuffer* buffer)
	{
		buffer->append("[EditorPanels][State]\n");
		for (const auto& window : m_editorWindows)
		{
			if (window->IsCollapsible())
			{
				buffer->appendf("%s=%d\n", window->GetName().c_str(), window->IsCollapsed() ? 1 : 0);
			}
		}
		buffer->append("\n");
	}

	void MainWindow::HandleCollapsibleRails(const ImVec2& origin, const ImVec2& avail, float leftWidth, float rightWidth, float bottomHeight)
	{
		const float bodyHeight = avail.y - bottomHeight;

		// The rails are drawn directly into the current (dockspace) window's gutters that we reserved
		// by insetting the dock space. Rendering inline (rather than as separate windows) keeps them
		// reliably on top of the empty dock background and avoids viewport/z-order issues.
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 railBg = ImGui::GetColorU32(ImVec4(0.140f, 0.140f, 0.150f, 1.0f));
		const ImU32 iconCol = ImGui::GetColorU32(ImVec4(0.337f, 0.624f, 0.824f, 1.0f));

		// The panel whose rail button is hovered this frame (drives the hover flyout below).
		EditorWindowBase* hoveredRailPanel = nullptr;
		bool expandedThisFrame = false;

		// Draws a vertical rail button (icon on top, name rotated 90 degrees below). Returns true on click.
		auto verticalRailButton = [&](EditorWindowBase& window, float railWidth) -> bool
		{
			const String icon = window.GetPanelIcon().empty() ? String(ICON_FA_BARS) : window.GetPanelIcon();
			const ImVec2 nameSize = ImGui::CalcTextSize(window.GetName().c_str());
			const ImVec2 iconSize = ImGui::CalcTextSize(icon.c_str());
			const float iconH = ImGui::GetTextLineHeight();
			const float btnW = railWidth - 8.0f;
			const float btnH = 6.0f + iconH + 8.0f + nameSize.x + 8.0f;

			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::Button((String("##rail_") + window.GetName()).c_str(), ImVec2(btnW, btnH));
			const bool hovered = ImGui::IsItemHovered();
			if (hovered)
			{
				hoveredRailPanel = &window;
			}

			const ImU32 textCol = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);

			// Icon centered near the top of the button.
			dl->AddText(ImVec2(p0.x + (btnW - iconSize.x) * 0.5f, p0.y + 6.0f), iconCol, icon.c_str());

			// Panel name rendered vertically (rotated) below the icon.
			const float colTop = p0.y + 6.0f + iconH + 8.0f;
			const float colX = p0.x + (btnW - nameSize.y) * 0.5f;
			AddTextVertical(dl, ImVec2(colX, colTop), textCol, window.GetName().c_str());

			return clicked;
		};

		// Renders a vertical rail at the given x. Toggles panel state on click.
		auto drawVerticalRail = [&](float railX, float railWidth, DockDirection edge)
		{
			dl->AddRectFilled(ImVec2(railX, origin.y), ImVec2(railX + railWidth, origin.y + bodyHeight), railBg);

			float y = origin.y + 6.0f;
			for (const auto& window : m_editorWindows)
			{
				if (!window->IsVisible() || !window->IsCollapsible() || !window->IsCollapsed()) continue;
				if (window->GetDefaultDockDirection() != edge) continue;

				ImGui::SetCursorScreenPos(ImVec2(railX + 4.0f, y));
				if (verticalRailButton(*window, railWidth))
				{
					window->SetCollapsed(false);
					ImGui::MarkIniSettingsDirty();
					expandedThisFrame = true;
				}
				y = ImGui::GetItemRectMax().y + 6.0f;
			}
		};

		if (leftWidth > 0.0f)
		{
			drawVerticalRail(origin.x, leftWidth, DockDirection::Left);
		}

		if (rightWidth > 0.0f)
		{
			drawVerticalRail(origin.x + avail.x - rightWidth, rightWidth, DockDirection::Right);
		}

		// Bottom rail (horizontal icon + label buttons).
		if (bottomHeight > 0.0f)
		{
			const float railY = origin.y + avail.y - bottomHeight;
			dl->AddRectFilled(ImVec2(origin.x, railY), ImVec2(origin.x + avail.x, railY + bottomHeight), railBg);

			float x = origin.x + 6.0f;
			for (const auto& window : m_editorWindows)
			{
				if (!window->IsVisible() || !window->IsCollapsible() || !window->IsCollapsed()) continue;

				const DockDirection dir = window->GetDefaultDockDirection();
				if (dir == DockDirection::Left || dir == DockDirection::Right) continue;

				ImGui::SetCursorScreenPos(ImVec2(x, railY + 6.0f));
				const String icon = window->GetPanelIcon().empty() ? String(ICON_FA_BARS) : window->GetPanelIcon();
				const String label = icon + "  " + window->GetName() + "##rail_" + window->GetName();
				if (ImGui::Button(label.c_str(), ImVec2(0.0f, bottomHeight - 12.0f)))
				{
					window->SetCollapsed(false);
					ImGui::MarkIniSettingsDirty();
					expandedThisFrame = true;
				}
				if (ImGui::IsItemHovered())
				{
					hoveredRailPanel = window.get();
				}
				x = ImGui::GetItemRectMax().x + 6.0f;
			}
		}

		// Hover flyout: while a rail button (or the flyout itself) is hovered, render the collapsed
		// panel's content in a temporary floating window next to the rail. Clicking the rail button
		// pins it (expands permanently); moving the mouse away dismisses the flyout.
		if (expandedThisFrame)
		{
			m_flyoutPanel = nullptr;
		}
		else
		{
			if (hoveredRailPanel)
			{
				m_flyoutPanel = hoveredRailPanel;
			}

			bool flyoutResizing = false;
			if (m_flyoutPanel && m_flyoutPanel->IsVisible() && m_flyoutPanel->IsCollapsed())
			{
				const DockDirection edge = m_flyoutPanel->GetDefaultDockDirection();
				const float defaultSize = m_flyoutPanel->GetDefaultDockSize();

				// The flyout grows only along the axis pointing away from its rail (width for side
				// panels, height for top/bottom panels); the other axis spans the rail. We track that
				// variable extent on the panel so a user resize sticks across frames and sessions.
				const bool sideDocked = (edge == DockDirection::Left || edge == DockDirection::Right);
				float varSize = m_flyoutPanel->GetFlyoutSize();
				if (varSize <= 0.0f)
				{
					varSize = defaultSize;
				}

				const float minSize = sideDocked ? 120.0f : 100.0f;
				const float maxSize = sideDocked ? avail.x * 0.8f : avail.y * 0.85f;
				varSize = ImClamp(varSize, minSize, maxSize);

				ImVec2 flyoutPos, flyoutSize;
				if (edge == DockDirection::Left)
				{
					flyoutSize = ImVec2(varSize, bodyHeight);
					flyoutPos = ImVec2(origin.x + leftWidth, origin.y);
				}
				else if (edge == DockDirection::Right)
				{
					flyoutSize = ImVec2(varSize, bodyHeight);
					flyoutPos = ImVec2(origin.x + avail.x - rightWidth - flyoutSize.x, origin.y);
				}
				else
				{
					flyoutSize = ImVec2(avail.x, varSize);
					flyoutPos = ImVec2(origin.x, origin.y + avail.y - bottomHeight - flyoutSize.y);
				}

				ImGui::SetNextWindowPos(flyoutPos);
				ImGui::SetNextWindowSize(flyoutSize);
				ImGui::SetNextWindowDockID(0, ImGuiCond_Always); // keep the flyout floating, never docked
				ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);

				// Provide the host window ourselves and then let the panel render its content. Some
				// panels (e.g. Log) draw their content directly and rely on the caller's Begin/End,
				// while others (Data Navigator, Asset Browser) Begin their own window; wrapping the
				// call in Begin/End mirrors HandleEditorWindow and works for both styles.
				//
				// We pin the position and size ourselves every frame and disable ImGui's built-in
				// window resize, then provide our own grip on the single edge that faces the central
				// area. This keeps the popup from offering resize on edges that make no sense (the
				// rail-facing edges) and stops ImGui's resize from being clobbered by our pinning.
				ImGuiWindowFlags flyoutFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
				if (ImGui::Begin(m_flyoutPanel->GetName().c_str(), nullptr, flyoutFlags))
				{
					m_flyoutPanel->Draw();
				}
				ImGui::End();

				// Resize grip on the single edge that faces the central area (right edge for a left
				// rail, left edge for a right rail, top edge otherwise). Handled manually with mouse
				// hit-testing rather than a window/InvisibleButton: an InvisibleButton inside the
				// flyout would be clipped by its title bar, and a separate overlay window draws its
				// own border and fights the flyout for z-order. The highlight is drawn on the
				// foreground draw list so it always sits above the flyout. The rail-facing edges
				// intentionally offer no resize.
				if (m_flyoutPanel->IsResizable())
				{
					const float thickness = 6.0f;
					ImVec2 gripMin, gripMax;
					ImGuiMouseCursor gripCursor;
					if (edge == DockDirection::Left)
					{
						gripMin = ImVec2(flyoutPos.x + flyoutSize.x - thickness, flyoutPos.y);
						gripMax = ImVec2(flyoutPos.x + flyoutSize.x, flyoutPos.y + flyoutSize.y);
						gripCursor = ImGuiMouseCursor_ResizeEW;
					}
					else if (edge == DockDirection::Right)
					{
						gripMin = ImVec2(flyoutPos.x, flyoutPos.y);
						gripMax = ImVec2(flyoutPos.x + thickness, flyoutPos.y + flyoutSize.y);
						gripCursor = ImGuiMouseCursor_ResizeEW;
					}
					else
					{
						gripMin = ImVec2(flyoutPos.x, flyoutPos.y);
						gripMax = ImVec2(flyoutPos.x + flyoutSize.x, flyoutPos.y + thickness);
						gripCursor = ImGuiMouseCursor_ResizeNS;
					}

					const bool gripHovered = ImGui::IsMouseHoveringRect(gripMin, gripMax, false);
					if (gripHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						m_flyoutResizeActive = true;
					}
					if (m_flyoutResizeActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
					{
						m_flyoutResizeActive = false;
					}

					if (gripHovered || m_flyoutResizeActive)
					{
						ImGui::SetMouseCursor(gripCursor);
						const ImU32 col = ImGui::GetColorU32(m_flyoutResizeActive ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered);
						ImGui::GetForegroundDrawList()->AddRectFilled(gripMin, gripMax, col);
					}

					if (m_flyoutResizeActive)
					{
						const ImVec2 delta = ImGui::GetIO().MouseDelta;
						float newSize = varSize;
						if (edge == DockDirection::Left)
						{
							newSize += delta.x;
						}
						else if (edge == DockDirection::Right)
						{
							newSize -= delta.x;
						}
						else
						{
							newSize -= delta.y; // bottom-docked flyout grows upward
						}

						m_flyoutPanel->SetFlyoutSize(ImClamp(newSize, minSize, maxSize));
						flyoutResizing = true;
					}
				}
				else
				{
					m_flyoutResizeActive = false;
				}

				// Keep the flyout open while the mouse is over the flyout OR anywhere over the rail strip
				// it belongs to. The strip and flyout are contiguous, so this leaves no dead zone (such
				// as the rail's padding) where the popup would flicker out as the cursor crosses over.
				ImVec2 railMin, railMax;
				if (edge == DockDirection::Left)
				{
					railMin = ImVec2(origin.x, origin.y);
					railMax = ImVec2(origin.x + leftWidth, origin.y + bodyHeight);
				}
				else if (edge == DockDirection::Right)
				{
					railMin = ImVec2(origin.x + avail.x - rightWidth, origin.y);
					railMax = ImVec2(origin.x + avail.x, origin.y + bodyHeight);
				}
				else
				{
					railMin = ImVec2(origin.x, origin.y + avail.y - bottomHeight);
					railMax = ImVec2(origin.x + avail.x, origin.y + avail.y);
				}

				const bool overFlyout = ImGui::IsMouseHoveringRect(flyoutPos, ImVec2(flyoutPos.x + flyoutSize.x, flyoutPos.y + flyoutSize.y), false);
				const bool overRail = ImGui::IsMouseHoveringRect(railMin, railMax, false);
				// Don't dismiss while the user is dragging the resize grip; the cursor may travel past
				// the flyout/rail rects during the drag and would otherwise snap the popup shut.
				if (!overFlyout && !overRail && !flyoutResizing)
				{
					m_flyoutPanel = nullptr;
				}
			}
			else
			{
				m_flyoutPanel = nullptr;
			}
		}
	}

	void MainWindow::ExportToClient()
	{
		if (m_config.clientDataPath.empty())
		{
			ELOG("Client data export path is not configured. Set 'clientDataPath' in the editor config file.");
			return;
		}

		namespace fs = std::filesystem;

		std::error_code ec;
		fs::create_directories(m_config.clientDataPath, ec);
		if (ec)
		{
			ELOG("Failed to create client data directory '" << m_config.clientDataPath << "': " << ec.message());
			return;
		}

		ILOG("Exporting client data to '" << m_config.clientDataPath << "'...");

		// Managers that exist in both server proto_data and client_data.
		// Server-only fields are unknown to the client proto schema and will be silently
		// ignored by the client's protobuf parser at load time.
		static const char* const sharedManagers[] = {
			"spells",
			"ranges",
			"maps",
			"zones",
			"spell_categories",
			"model_data",
			"races",
			"classes",
			"factions",
			"faction_templates",
			"item_displays",
			"object_displays",
			"animations",
			"talents",
			"talent_tabs",
			"spell_visualizations",
			"area_triggers",
			"proficiencies",
			"item_classes",
			"item_subclasses",
			"chat_channels",
		};

		const fs::path srcDir = m_config.projectPath;
		const fs::path dstDir = m_config.clientDataPath;

		// Copy .data files and record which ones succeeded
		std::vector<const char*> exported;
		for (const char* name : sharedManagers)
		{
			const std::string fileName = std::string(name) + ".data";
			const fs::path src = srcDir / fileName;
			const fs::path dst = dstDir / fileName;

			if (!fs::exists(src))
			{
				WLOG("Client export: '" << src.string() << "' not found - skipping");
				continue;
			}

			fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				WLOG("Client export: failed to copy '" << name << "': " << ec.message());
				continue;
			}

			exported.push_back(name);
		}

		// Write project.txt manifest for the client output directory
		const std::string projectFilePath = (dstDir / "project.txt").string();
		std::ofstream projectFile(projectFilePath);
		if (!projectFile)
		{
			ELOG("Client export: could not write project.txt at '" << projectFilePath << "'");
			return;
		}

		sff::write::File<char> sffFile(projectFile, sff::write::MultiLine);
		sffFile.addKey("version", 1);
		sffFile.writer.newLine();

		for (const char* name : exported)
		{
			sff::write::Table<char> table(sffFile, name, sff::write::Comma);
			table.addKey("file", std::string(name) + ".data");
			table.Finish();
		}

		ILOG("Client data export complete: " << exported.size() << " manager(s) copied to '" << m_config.clientDataPath << "'.");
	}

	void MainWindow::RenderImGui()
	{
#ifdef _WIN32
		// Start the Dear ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
#else
        
#endif
        
		ImGui::NewFrame();

		if (m_defaultFont) ImGui::PushFont(m_defaultFont);

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		// Get the viewport and make the dockspace window fullscreen
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize + ImVec2(0, 50));
		ImGui::SetNextWindowViewport(viewport->ID);

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (m_dockSpaceFlags & ImGuiDockNodeFlags_PassthruCentralNode) window_flags |= ImGuiWindowFlags_NoBackground;

		// Begin the dockspace window with disabled padding
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 50.0f));
		ImGui::Begin("DockSpace", nullptr, window_flags);
		ImGui::PopStyleVar(3);
		{
			const auto dockspace_id = ImGui::GetID("MyDockSpace");

			// Determine which edges currently host collapsed panels so we can reserve a thin rail
			// for them and inset the dock space accordingly.
			constexpr float railThickness = 34.0f;
			bool hasLeft = false, hasRight = false, hasBottom = false;
			for (const auto& window : m_editorWindows)
			{
				if (!window->IsVisible() || !window->IsCollapsible() || !window->IsCollapsed())
				{
					continue;
				}

				switch (window->GetDefaultDockDirection())
				{
				case DockDirection::Left:  hasLeft = true; break;
				case DockDirection::Right: hasRight = true; break;
				default:                   hasBottom = true; break;
				}
			}

			const float leftRail = hasLeft ? railThickness : 0.0f;
			const float rightRail = hasRight ? railThickness : 0.0f;
			const float bottomRail = hasBottom ? railThickness : 0.0f;

			// Inset the dock space so docked content never sits underneath a rail.
			const ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
			const ImVec2 contentAvail = ImGui::GetContentRegionAvail();
			ImGui::SetCursorScreenPos(ImVec2(contentOrigin.x + leftRail, contentOrigin.y));
			ImGui::DockSpace(dockspace_id, ImVec2(contentAvail.x - leftRail - rightRail, contentAvail.y - bottomRail), m_dockSpaceFlags);

			// The main menu
			HandleMainMenu();

			HandleToolBar();

			ShowCreatureEditor();

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

			// Draw the importer modules
			for (const auto& importer : m_imports)
			{
				importer->Draw();
			}

			// Show a friendly welcome panel whenever the central document area is empty.
			HandleWelcomeScreen(dockspace_id);

			// Draw the rails that host any collapsed panels (in the gutters we reserved above).
			HandleCollapsibleRails(contentOrigin, contentAvail, leftRail, rightRail, bottomRail);

			const ImGuiID dockSpaceId = ImGui::GetID("MyDockSpace");

			// Dock newly opened editor instances (documents) into the central node so they fill the
			// main document area. Docking into the root split node would leave them floating now that
			// the layout has an explicit central node.
			ImGuiID documentNode = dockSpaceId;
			if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockSpaceId))
			{
				documentNode = central->ID;
			}

			// Only dock an instance once its ImGui window has actually been created by
			// editor->Draw() this frame. If the asset was opened from the welcome screen
			// or any other path that runs *after* editor->Draw() (so ImGui::Begin hasn't
			// been called for it yet), FindWindowByName returns null and we keep the entry
			// to retry next frame — otherwise the window would float permanently because
			// ImGuiWindowFlags_NoSavedSettings prevents the settings-based fallback.
			bool anyDocked = false;
			std::erase_if(m_uninitializedEditorInstances, [documentNode, &anyDocked](const String& name)
			{
				if (ImGui::FindWindowByName(name.c_str()) == nullptr)
					return false;
				ImGui::DockBuilderDockWindow(name.c_str(), documentNode);
				anyDocked = true;
				return true;
			});
			if (anyDocked)
				ImGui::DockBuilderFinish(dockSpaceId);

			// Initialize the layout
			if (m_applyDefaultLayout)
			{
				// Set the default dock layout
				ImGuiDefaultDockLayout();
			}
		}
		ImGui::End();

		if (m_defaultFont) ImGui::PopFont();

		// Rendering
		ImGui::Render();
#ifdef _WIN32
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#endif
        
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

		auto bottomId =  ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, 400.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);
		auto topId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Up, 400.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);
		auto leftId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, 200.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);
		auto rightId = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, 200.0f / ImGui::GetMainViewport()->Size.y, nullptr, &dockMainId);

		// Flag the remaining middle node as the central (document) node so opened editor instances
		// dock into it and the welcome screen can detect when it is empty.
		if (ImGuiDockNode* centralNode = ImGui::DockBuilderGetNode(dockMainId))
		{
			centralNode->SetLocalFlags(centralNode->LocalFlags | ImGuiDockNodeFlags_CentralNode);
		}

		for(const auto& window : m_editorWindows)
		{
			if (!window->IsDockable()) continue;

			ImGuiID targetNode = dockMainId;
			switch(window->GetDefaultDockDirection())
			{
			case DockDirection::Bottom:
				targetNode = bottomId;
				break;
			case DockDirection::Left:
				targetNode = leftId;
				break;
			case DockDirection::Right:
				targetNode = rightId;
				break;
			case DockDirection::Top:
				targetNode = topId;
				break;
			default:
				targetNode = dockMainId;
				break;
			}

			ImGui::DockBuilderDockWindow(window->GetName().c_str(), targetNode);

			// Record the default dock node so a panel that starts collapsed (and is therefore
			// never drawn to capture its live dock id) can still be restored to the correct
			// dock location on its first expand instead of popping out as a floating window.
			if (window->IsCollapsible())
			{
				window->SetLastDockId(targetNode);
			}
		}
		
		ImGui::DockBuilderFinish(dockSpaceId);

		// Finish default layout
		m_applyDefaultLayout = false;
	}

	void MainWindow::ShutdownImGui() const
	{
		ASSERT(m_imguiContext);

#ifdef _WIN32
		// Shutdown d3d11 and win32 implementation of imgui
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
#else
        // TODO: Glfw
#endif
        
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

	EditorWindowBase* MainWindow::GetWindow(size_t index) const
	{
		ASSERT(index < m_editorWindows.size());
		return m_editorWindows[index].get();
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

		// UE5-like professional dark theme with blue accents
		colors[ImGuiCol_Text]                   = ImVec4(0.900f, 0.900f, 0.900f, 1.000f);
		colors[ImGuiCol_TextDisabled]           = ImVec4(0.500f, 0.500f, 0.500f, 1.000f);
		colors[ImGuiCol_WindowBg]               = ImVec4(0.100f, 0.100f, 0.100f, 1.000f);
		colors[ImGuiCol_ChildBg]                = ImVec4(0.120f, 0.120f, 0.120f, 0.000f);
		colors[ImGuiCol_PopupBg]                = ImVec4(0.110f, 0.110f, 0.110f, 1.000f);
		colors[ImGuiCol_Border]                 = ImVec4(0.150f, 0.150f, 0.150f, 1.000f);
		colors[ImGuiCol_BorderShadow]           = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
		colors[ImGuiCol_FrameBg]                = ImVec4(0.132f, 0.132f, 0.132f, 1.000f);
		colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.200f, 0.200f, 0.200f, 1.000f);
		colors[ImGuiCol_FrameBgActive]          = ImVec4(0.280f, 0.280f, 0.280f, 1.000f);
		colors[ImGuiCol_TitleBg]                = ImVec4(0.080f, 0.080f, 0.080f, 1.000f);
		colors[ImGuiCol_TitleBgActive]          = ImVec4(0.082f, 0.082f, 0.082f, 1.000f);
		colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.080f, 0.080f, 0.080f, 1.000f);
		colors[ImGuiCol_MenuBarBg]              = ImVec4(0.102f, 0.102f, 0.102f, 1.000f);
		colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.060f, 0.060f, 0.060f, 1.000f);
		colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.220f, 0.220f, 0.220f, 1.000f);
		colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.300f, 0.300f, 0.300f, 1.000f);
		colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_CheckMark]              = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_SliderGrab]             = ImVec4(0.337f, 0.624f, 0.824f, 0.780f);
		colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_Button]                 = ImVec4(0.150f, 0.150f, 0.150f, 1.000f);
		colors[ImGuiCol_ButtonHovered]          = ImVec4(0.337f, 0.624f, 0.824f, 0.156f);
		colors[ImGuiCol_ButtonActive]           = ImVec4(0.337f, 0.624f, 0.824f, 0.391f);
		colors[ImGuiCol_Header]                 = ImVec4(0.150f, 0.150f, 0.150f, 1.000f);
		colors[ImGuiCol_HeaderHovered]          = ImVec4(0.337f, 0.624f, 0.824f, 0.156f);
		colors[ImGuiCol_HeaderActive]           = ImVec4(0.337f, 0.624f, 0.824f, 0.391f);
		colors[ImGuiCol_Separator]              = colors[ImGuiCol_Border];
		colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.337f, 0.624f, 0.824f, 0.780f);
		colors[ImGuiCol_SeparatorActive]        = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_ResizeGrip]             = ImVec4(0.337f, 0.624f, 0.824f, 0.250f);
		colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.337f, 0.624f, 0.824f, 0.670f);
		colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_Tab]                    = ImVec4(0.082f, 0.082f, 0.082f, 1.000f);
		colors[ImGuiCol_TabHovered]             = ImVec4(0.337f, 0.624f, 0.824f, 0.156f);
		colors[ImGuiCol_TabActive]              = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
		colors[ImGuiCol_TabUnfocused]           = ImVec4(0.082f, 0.082f, 0.082f, 1.000f);
		colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.150f, 0.150f, 0.150f, 1.000f);
		colors[ImGuiCol_DockingPreview]         = ImVec4(0.337f, 0.624f, 0.824f, 0.781f);
		colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.080f, 0.080f, 0.080f, 1.000f);
		colors[ImGuiCol_PlotLines]              = ImVec4(0.610f, 0.610f, 0.610f, 1.000f);
		colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_PlotHistogram]          = ImVec4(0.337f, 0.624f, 0.824f, 0.780f);
		colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.337f, 0.624f, 0.824f, 0.278f);
		colors[ImGuiCol_DragDropTarget]         = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_NavHighlight]           = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(0.337f, 0.624f, 0.824f, 1.000f);
		colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);
		colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);

		// A subtle lighter border gives panels gentle definition without looking boxy.
		colors[ImGuiCol_Border]                 = ImVec4(0.220f, 0.220f, 0.235f, 0.500f);
		colors[ImGuiCol_Separator]              = ImVec4(0.220f, 0.220f, 0.235f, 0.500f);

		// Style adjustments for a more professional appearance
		style->ChildRounding = 4.0f;
		style->FrameBorderSize = 1.0f;
		style->FrameRounding = 3.0f;
		style->GrabMinSize = 8.0f;
		style->GrabRounding = 3.0f;
		style->PopupRounding = 3.0f;
		style->PopupBorderSize = 1.0f;
		style->ScrollbarRounding = 12.0f;
		style->ScrollbarSize = 13.0f;
		style->TabBorderSize = 0.0f;
		style->TabRounding = 4.0f;
		style->WindowRounding = 4.0f;
		style->WindowBorderSize = 1.0f;

		// Hide the per-window collapse arrow for a cleaner, Unreal-like docked look.
		style->WindowMenuButtonPosition = ImGuiDir_None;

		// Spacing adjustments for a cleaner, slightly roomier look
		style->ItemSpacing = ImVec2(8.0f, 6.0f);
		style->ItemInnerSpacing = ImVec2(6.0f, 6.0f);
		style->FramePadding = ImVec2(9.0f, 5.0f);
		style->CellPadding = ImVec2(6.0f, 4.0f);
		style->IndentSpacing = 18.0f;
		style->WindowPadding = ImVec2(10.0f, 10.0f);
		style->WindowTitleAlign = ImVec2(0.0f, 0.5f);
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

		// Register a settings handler so collapsed-panel state is persisted in imgui.ini alongside
		// the dock layout. Must be registered before the first NewFrame (which loads the ini).
		ImGuiSettingsHandler panelHandler;
		panelHandler.TypeName = "EditorPanels";
		panelHandler.TypeHash = ImHashStr("EditorPanels");
		panelHandler.UserData = this;
		panelHandler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* /*name*/) -> void*
		{
			return (void*)1; // single "State" section; non-null marks it open
		};
		panelHandler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, void* /*entry*/, const char* line)
		{
			static_cast<MainWindow*>(handler->UserData)->ReadPanelCollapseSetting(line);
		};
		panelHandler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
		{
			static_cast<MainWindow*>(handler->UserData)->WritePanelCollapseSettings(buf);
		};
		ImGui::GetCurrentContext()->SettingsHandlers.push_back(panelHandler);

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();

		// Load the editor fonts. We build three sizes of Roboto (body, header, title) and merge the
		// FontAwesome icon glyphs into each of them so icons can be embedded inline in any UI text.
		const auto editorDir = Path(m_config.assetRegistryPath) / "Editor";
		const auto fontPath = editorDir / "Roboto-Regular.ttf";
		const auto iconFontPath = editorDir / "FontAwesome.ttf";

		// The glyph range must outlive font atlas building, so keep it static.
		static const ImWchar s_iconRange[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };

		ImFontConfig iconConfig;
		iconConfig.MergeMode = true;
		iconConfig.PixelSnapH = true;
		// Pull the icons in a touch so they sit nicely on the text baseline.
		iconConfig.GlyphOffset = ImVec2(0.0f, 1.0f);

		auto loadFontWithIcons = [&](float size, float iconSize) -> ImFont*
		{
			ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), size, nullptr, io.Fonts->GetGlyphRangesDefault());
			iconConfig.GlyphMinAdvanceX = iconSize; // keep icons roughly monospaced for alignment
			io.Fonts->AddFontFromFileTTF(iconFontPath.string().c_str(), iconSize, &iconConfig, s_iconRange);
			return font;
		};

		m_defaultFont = loadFontWithIcons(16.0f, 14.0f);
		m_headerFont  = loadFontWithIcons(18.0f, 16.0f);
		m_titleFont   = loadFontWithIcons(26.0f, 22.0f);

		SetEditorFonts(m_defaultFont, m_headerFont, m_titleFont);

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

#ifdef _WIN32
		// Setup Platform/Renderer bindings
		ImGui_ImplWin32_Init(m_windowHandle);

		// This is hacky :/ Get the d3d11 device and immediate device context required for rendering
		auto& d3d11Dev = reinterpret_cast<GraphicsDeviceD3D11&>(GraphicsDevice::Get());
		ID3D11Device& device = d3d11Dev;
		ID3D11DeviceContext& immContext = d3d11Dev;
		ImGui_ImplDX11_Init(&device, &immContext);
#else
        
#endif
        
		// Build the font atlas (Roboto + merged FontAwesome icons in three sizes).
		io.Fonts->Build();

		// Dockspace flags
		m_dockSpaceFlags = ImGuiDockNodeFlags_None;

		ApplyDefaultStyle();
	}

#ifdef _WIN32
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
			struct CreateParams
			{
				BOOL bEnableNonClientDpiScaling;
				BOOL bChildWindowDpiIsolation;
			};

			// Enable per-monitor DPI scaling for caption, menu, and top-level
			// scroll bars.
			//
			// Non-client area (scroll bars, caption bar, etc.) does not DPI scale
			// automatically on Windows 8.1. In Windows 10 (1607) support was added
			// for this via a call to EnableNonClientDpiScaling. Windows 10 (1703)
			// supports this automatically when the DPI_AWARENESS_CONTEXT is
			// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2.
			//
			// Here we are detecting if a BOOL was set to enable non-client DPI scaling
			// via the call to CreateWindow that resulted in this window. Doing this
			// detection is only necessary in the context of this sample.
			const auto createStruct = reinterpret_cast<const CREATESTRUCT*>(lparam);
			const auto createParams = static_cast<const CreateParams*>(createStruct->lpCreateParams);
			if (createParams->bEnableNonClientDpiScaling)
			{
				EnableNonClientDpiScaling(wnd);
			}

			// Store a flag on the window to note that it'll run its child in a different awareness
			if (createParams->bChildWindowDpiIsolation)
			{
				SetProp(wnd, TEXT("PROP_ISOLATION"), reinterpret_cast<HANDLE>(TRUE));
			}
			
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

	LRESULT MainWindow::MsgProc(const HWND wnd, const UINT msg, const WPARAM wparam, const LPARAM lparam)
	{
		switch (msg)
		{
		case WM_CLOSE:
			DestroyWindow(wnd);
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_DPICHANGED:
		{
			const auto newWindowRect = reinterpret_cast<RECT*>(lparam);
			SetWindowPos(wnd,
				nullptr,
				newWindowRect->left,
				newWindowRect->top,
				newWindowRect->right - newWindowRect->left,
				newWindowRect->bottom - newWindowRect->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			break;
		}
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

				InvalidateRect(wnd, nullptr, FALSE);
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
#endif

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

	bool MainWindow::OpenAssetAtWorldLocation(const Path& assetPath, const float worldX, const float worldZ)
	{
		auto extension = assetPath.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

		for (const auto& editor : m_editors)
		{
			if (!editor->CanLoadAsset(extension))
			{
				continue;
			}

			// Inform the editor about the requested camera target before opening so it can apply it
			// to the (possibly newly created) instance.
			editor->SetPendingCameraTarget(assetPath, Vector3(worldX, 0.0f, worldZ));

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

	void MainWindow::ShowAssetActionContextMenu(const String& asset)
	{
		bool hasActions = false;

		for (const auto& editor : m_editors)
		{
			if (!editor->CanLoadAsset(std::filesystem::path(asset).extension().string()))
			{
				continue;
			}

			if (!hasActions)
			{
				ImGui::Separator();
				hasActions = true;
			}

			editor->AddAssetActions(asset);
		}
	}

	void MainWindow::InvalidateAssetPreview(const String& asset)
	{
	}

	void MainWindow::NavigateToAsset(const std::string& assetPath)
	{
		for (auto& window : m_editorWindows)
		{
			if (auto* assetWindow = dynamic_cast<AssetWindow*>(window.get()))
			{
				assetWindow->NavigateToAsset(assetPath);
				return;
			}
		}
	}
}
