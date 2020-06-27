// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "log_window.h"

#include "base/non_copyable.h"

#include "imgui.h"

#include <Windows.h>

#include <vector>
#include <string>


namespace mmo
{
	/// This class manages the main window of the application.
	class MainWindow final : public NonCopyable
	{
	public:
		explicit MainWindow();

	private:
		/// Ensures that the window class has been created by creating it if needed.
		void EnsureWindowClassCreated();
		/// Creates the internal window handle.
		void CreateWindowHandle();
		/// Initialize ImGui.
		void InitImGui();
		/// Render ImGui.
		void RenderImGui();

		void ImGuiDefaultDockLayout();

		void ShutdownImGui();

		bool OnFileDrop(std::string filename);

	private:
		/// Static window message callback procedure. Simply tries to route the message to the
		/// window instance.
		static LRESULT CALLBACK WindowMsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);
		/// Instanced window callback procedure.
		LRESULT CALLBACK MsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);

	private:
		HWND m_windowHandle;
		/// The dock space flags.
		ImGuiDockNodeFlags m_dockSpaceFlags;

		bool m_applyDefaultLayout = true;
		bool m_showViewport = true;

		ImVec2 m_lastAvailViewportSize;

		ImGuiContext* m_imguiContext;

		LogWindow m_logWindow;

		ImVec2 m_viewportPosition;
	};
}
