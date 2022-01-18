// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "main_window.h"

#include "log/default_log_levels.h"
#include "graphics/graphics_device.h"
#include "base/filesystem.h"
#include "base/win_utility.h"
#include "configuration.h"
#include "asset_preview_provider.h"
#include "texture_preview_provider.h"

#include <mutex>

namespace mmo
{
	void AddDefaultPreviewProviders(PreviewProviderManager& manager)
	{
		manager.AddPreviewProvider(std::make_unique<mmo::TexturePreviewProvider>());
	}
}

/// Procedural entry point on windows platforms.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
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

	// Initialize the main window instance
	mmo::MainWindow mainWindow { config };

	// Setup preview provider manager
	mmo::PreviewProviderManager previewProviderManager;
	AddDefaultPreviewProviders(previewProviderManager);

	// Setup asset window
	auto assetWindow = std::make_unique<mmo::AssetWindow>("Asset Browser", previewProviderManager);
	mainWindow.AddEditorWindow(std::move(assetWindow));

	// Run the message loop
	MSG msg = { nullptr };
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// Successfully terminated the editor
	return 0;
}
