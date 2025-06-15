// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_windows/editor_window_base.h"
#include "import/fbx_import.h"
#include "editor_host.h"

#include "base/non_copyable.h"

#ifdef _WIN32
#	include <Windows.h>
#endif

#include <string>

#include "imgui_node_editor_internal.inl"
#include "editors/editor_base.h"
#include "import/import_base.h"


struct ImGuiContext;

namespace mmo
{
	namespace proto
	{
		class Project;
	}

	class AsyncDatabase;
	class Configuration;
	
	
	/// This class manages the main window of the application.
	class MainWindow final 
		: public NonCopyable
		, public EditorHost
	{
	public:
		explicit MainWindow(Configuration& config,
			proto::Project& project);
		~MainWindow();

	private:
		/// Ensures that the window class has been created by creating it if needed.
		static void EnsureWindowClassCreated();

		/// Creates the internal window handle.
		void CreateWindowHandle();

		/// @brief 
		/// @param window 
		void HandleEditorWindow(EditorWindowBase& window);

		void HandleMainMenu();
		void ShowCreatureEditor();
		void HandleToolBar();

		/// Initialize ImGui.
		void InitImGui();

		/// Render ImGui.
		void RenderImGui();

		void ImGuiDefaultDockLayout();

		void ShutdownImGui() const;

		bool OnFileDrop(std::string filename);

		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y);

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y);

		void OnMouseMoved(uint16 x, uint16 y);
		
		void ApplyDefaultStyle();

	public:
		void AddEditorWindow(std::unique_ptr<EditorWindowBase> editorWindow) override;

		EditorWindowBase* GetWindow(size_t index) const;

		size_t GetWindowCount() const { return m_editorWindows.size(); }

		void RemoveEditorWindow(const String& name) override;

		void AddImport(std::unique_ptr<ImportBase> import);

		void AddEditor(std::unique_ptr<EditorBase> editor);
		
	private:
#ifdef _WIN32
		/// Static window message callback procedure. Simply tries to route the message to the
		/// window instance.
		static LRESULT CALLBACK WindowMsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);

		/// Instanced window callback procedure.
		LRESULT CALLBACK MsgProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);
#endif
        
	public:
		void SetCurrentPath(const Path& selectedPath) override { m_selectedPath = selectedPath; }

		const Path& GetCurrentPath() const override { return m_selectedPath; }
		
		bool OpenAsset(const Path& assetPath) override;
		
		void SetActiveEditorInstance(EditorInstance* instance) override;
		
		void EditorInstanceClosed(EditorInstance& instance) override;

		void ShowAssetCreationContextMenu() override;

		void ShowAssetActionContextMenu(const String& asset) override;

		void InvalidateAssetPreview(const String& asset) override;

	private:
		Configuration& m_config;
#ifdef _WIN32
		HWND m_windowHandle;
#endif
        
		ImGuiDockNodeFlags m_dockSpaceFlags;
		bool m_applyDefaultLayout = true;
		ImGuiContext* m_imguiContext;
		bool m_fileLoaded;
		std::string m_modelName;
		std::vector<std::unique_ptr<EditorWindowBase>> m_editorWindows;
		std::vector<std::unique_ptr<ImportBase>> m_imports;
		std::vector<std::unique_ptr<EditorBase>> m_editors;
		Path m_selectedPath;
		std::vector<String> m_uninitializedEditorInstances;
		EditorInstance* m_activeEditorInstance { nullptr };
		ImFont* m_defaultFont { nullptr };

		proto::Project& m_project;
	};
}
