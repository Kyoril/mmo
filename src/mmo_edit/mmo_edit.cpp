// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include <asio/io_context.hpp>
#include <asio/io_service.hpp>

#include "main_window.h"

#include "base/win_utility.h"
#include "configuration.h"
#include "mysql_database.h"
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

	// This is the main ioService object
	asio::io_service ioService;

	// The database service object and keep-alive object
	asio::io_service dbService;

	// Keep the database service alive / busy until this object is alive
	auto dbWork = std::make_shared<asio::io_context::work>(dbService);

	auto database = std::make_unique<mmo::MySQLDatabase>(mmo::mysql::DatabaseInfo{
		config.mysqlHost,
			config.mysqlPort,
			config.mysqlUser,
			config.mysqlPassword,
			config.mysqlDatabase
	});
	if (!database->Load())
	{
		ELOG("Could not load the database");
		return 1;
	}

	const auto async = [&dbService](mmo::Action action) { dbService.post(std::move(action)); };
	const auto sync = [&ioService](mmo::Action action) { ioService.post(std::move(action)); };
	mmo::AsyncDatabase asyncDatabase{ *database, async, sync };
	
	// Create log window before main window so that we already subscribe to log events as early as possible
	auto logWindow = std::make_unique<mmo::LogWindow>();

	// Initialize the main window instance
	mmo::MainWindow mainWindow { config, asyncDatabase };
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

	// Run the database service thread
	std::thread dbThread{ [&dbService]() { dbService.run(); } };

#ifdef _WIN32
	// Run the message loop
	MSG msg = { nullptr };
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		ioService.poll_one();
	}
#endif

	dbService.stop();
	dbThread.join();

	// Successfully terminated the editor
	return 0;
}
