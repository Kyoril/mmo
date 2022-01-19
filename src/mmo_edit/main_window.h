// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "viewport_window.h"
#include "worlds_window.h"
#include "editor_windows/editor_window_base.h"
#include "import/fbx_import.h"

#include "data/project.h"
#include "base/non_copyable.h"

#ifdef _WIN32
#	include <Windows.h>
#endif

#include <string>



namespace mmo
{
	class Configuration;
	
	
	/// This class manages the main window of the application.
	class MainWindow final 
		: public NonCopyable
	{
	public:
		explicit MainWindow(Configuration& config);
		~MainWindow();

	private:
		/// Ensures that the window class has been created by creating it if needed.
		static void EnsureWindowClassCreated();

		/// Creates the internal window handle.
		void CreateWindowHandle();

		/// @brief 
		/// @param window 
		void HandleEditorWindow(EditorWindowBase& window);

		void HandleMainMenu(bool& showSaveDialog);

		/// Initialize ImGui.
		void InitImGui();

		void RenderSimpleNodeEditor();

		/// Render ImGui.
		void RenderImGui();

		void ImGuiDefaultDockLayout();

		void ShutdownImGui() const;

		bool OnFileDrop(std::string filename);

		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y);

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y);

		void OnMouseMoved(uint16 x, uint16 y);

		void RenderSaveDialog();

	public:
		void AddEditorWindow(std::unique_ptr<EditorWindowBase> editorWindow);

		void RemoveEditorWindow(const String& name);
		
	private:
		/// Static window message callback procedure. Simply tries to route the message to the
		/// window instance.
		static LRESULT CALLBACK WindowMsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);

		/// Instanced window callback procedure.
		LRESULT CALLBACK MsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);

	private:
		Configuration& m_config;
		HWND m_windowHandle;
		ImGuiDockNodeFlags m_dockSpaceFlags;
		bool m_applyDefaultLayout = true;
		ImGuiContext* m_imguiContext;
		ViewportWindow m_viewportWindow;
		FbxImport m_importer;
		int16 m_lastMouseX, m_lastMouseY;
		bool m_leftButtonPressed;
		bool m_rightButtonPressed;
		bool m_fileLoaded;
		bool m_projectLoaded;
		Project m_project;
		WorldsWindow m_worldsWindow;
		std::string m_modelName;
		std::vector<std::unique_ptr<EditorWindowBase>> m_editorWindows;
	};
}
