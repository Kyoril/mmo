#pragma once

#include <filesystem>
#include <memory>

#include "editor_windows/editor_window_base.h"

using Path = std::filesystem::path;

namespace mmo
{
	class EditorInstance;

	class EditorHost
	{
	public:
		signal<void(const Path&)> assetImported;
		signal<void()> beforeUiUpdate;

	public:
		virtual ~EditorHost() = default;

	public:
		virtual void SetCurrentPath(const Path& selectedPath) = 0;

		virtual const Path& GetCurrentPath() const noexcept = 0;
		
		virtual bool OpenAsset(const Path& assetPath) = 0;
		
		virtual void AddEditorWindow(std::unique_ptr<EditorWindowBase> editorWindow) = 0;

		virtual void RemoveEditorWindow(const String& name) = 0;

		virtual void SetActiveEditorInstance(EditorInstance* instance) = 0;

		virtual void EditorInstanceClosed(EditorInstance& instance) = 0;

		virtual void InvalidateAssetPreview(const String& asset) = 0;

		// TODO: Redesign the way of showing this context menu. Make it less "show" and more "get what to show and build the menu somewhere else where it's needed"
		virtual void ShowAssetCreationContextMenu() = 0;
		virtual void ShowAssetActionContextMenu(const String& asset) = 0;
	};
}