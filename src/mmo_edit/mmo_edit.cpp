// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "main_window.h"

#include "base/win_utility.h"
#include "configuration.h"
#include "assets/asset_registry.h"

#include "preview_providers/preview_provider_manager.h"
#include "preview_providers/texture_preview_provider.h"

#include "editor_windows/asset_window.h"
#include "editor_windows/log_window.h"

#include "import/texture_import.h"
#include "import/fbx_import.h"

#include "editors/model_editor/model_editor.h"
#include "editors/material_editor/material_editor.h"
#include "editors/texture_editor/texture_editor.h"
#include "editors/world_editor/world_editor.h"
#include "editors/world_editor/world_editor.h"

#ifdef _DEBUG
#	include "log/default_log_levels.h"
#	include <mutex>
#endif

namespace mmo
{
	void AddDefaultPreviewProviders(PreviewProviderManager& manager)
	{
		manager.AddPreviewProvider(std::make_unique<mmo::TexturePreviewProvider>());
	}
}

#ifdef _WIN32
/// Procedural entry point on windows platforms.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main(int argc, char* arg[])
#endif
{
	// Setup log to print each log entry to the debug output on windows
#ifdef _DEBUG
	std::mutex logMutex;
	mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry & entry) {
		std::scoped_lock lock{ logMutex };
		OutputDebugStringA((entry.message + "\n").c_str());
	});
#endif

	// Load configuration file
	mmo::Configuration config;
	config.Load("./config/model_editor.cfg");

	// Create log window before main window so that we already subscribe to log events as early as possible
	auto logWindow = std::make_unique<mmo::LogWindow>();

	// Initialize the main window instance
	mmo::MainWindow mainWindow { config };
	mainWindow.AddEditorWindow(std::move(logWindow));

	// Setup preview provider manager
	mmo::PreviewProviderManager previewProviderManager;
	AddDefaultPreviewProviders(previewProviderManager);

	// Setup asset window
	auto assetWindow = std::make_unique<mmo::AssetWindow>("Asset Browser", previewProviderManager, mainWindow);
	mainWindow.AddEditorWindow(std::move(assetWindow));

	mainWindow.AddImport(std::make_unique<mmo::TextureImport>());
	mainWindow.AddImport(std::make_unique<mmo::FbxImport>());

	mainWindow.AddEditor(std::make_unique<mmo::ModelEditor>(mainWindow));
	mainWindow.AddEditor(std::make_unique<mmo::MaterialEditor>(mainWindow));
	mainWindow.AddEditor(std::make_unique<mmo::WorldEditor>(mainWindow));

#ifdef _WIN32
	// Run the message loop
	MSG msg = { nullptr };
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
#endif

	// Successfully terminated the editor
	return 0;
}
