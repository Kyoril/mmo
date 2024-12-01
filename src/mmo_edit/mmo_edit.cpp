// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include <asio/io_context.hpp>
#include <asio/io_service.hpp>

#include "main_window.h"

#include "configuration.h"
#include "mysql_database.h"
#include "assets/asset_registry.h"

#include "preview_providers/preview_provider_manager.h"
#include "preview_providers/texture_preview_provider.h"
#include "preview_providers/material_preview_provider.h"

#include "editor_windows/asset_window.h"
#include "editor_windows/log_window.h"
#include "editor_windows/spell_editor_window.h"
#include "editor_windows/map_editor_window.h"
#include "editor_windows/creature_editor_window.h"
#include "editor_windows/class_editor_window.h"
#include "editor_windows/range_type_editor_window.h"
#include "editor_windows/item_editor_window.h"
#include "editor_windows/unit_loot_editor_window.h"

#include "import/texture_import.h"
#include "import/fbx_import.h"

#include "editors/mesh_editor/mesh_editor.h"
#include "editors/material_editor/material_editor.h"
#include "editors/material_instance_editor/material_instance_editor.h"
#include "editors/texture_editor/texture_editor.h"
#include "editors/world_editor/world_editor.h"
#include "editors/world_model_editor/world_model_editor.h"
#include "editor_windows/faction_editor_window.h"
#include "editor_windows/faction_template_editor_window.h"
#include "editor_windows/model_editor_window.h"
#include "editor_windows/race_editor_window.h"
#include "editor_windows/trainer_editor_window.h"
#include "editor_windows/vendor_editor_window.h"
#include "editor_windows/quest_editor_window.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"

#ifdef _DEBUG
#	include <mutex>
#endif

namespace mmo
{
	void AddDefaultPreviewProviders(PreviewProviderManager& manager, EditorHost& host)
	{
		manager.AddPreviewProvider(std::make_unique<mmo::TexturePreviewProvider>());
		manager.AddPreviewProvider(std::make_unique<mmo::MaterialPreviewProvider>(host));
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

	// Create log window before main window so that we already subscribe to log events as early as possible
	auto logWindow = std::make_unique<mmo::LogWindow>();

	// Load the project
	mmo::proto::Project project;
	if (!project.load(config.projectPath))
	{
#ifdef _WIN32
		MessageBox(nullptr, TEXT("Failed to load project"), TEXT("Error"), MB_OK | MB_ICONERROR);
#endif
		ELOG("Failed to load project!");
		return 1;
	}

	// Initialize the main window instance
	mmo::MainWindow mainWindow { config, project };
	mainWindow.AddEditorWindow(std::move(logWindow));

	// Setup preview provider manager
	mmo::PreviewProviderManager previewProviderManager;
	AddDefaultPreviewProviders(previewProviderManager, mainWindow);

	// Setup asset window
	auto assetWindow = std::make_unique<mmo::AssetWindow>("Asset Browser", previewProviderManager, mainWindow);
	mainWindow.AddEditorWindow(std::move(assetWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::RangeTypeEditorWindow>("Spell Range Type Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::SpellEditorWindow>("Spell Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::QuestEditorWindow>("Quest Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::MapEditorWindow>("Map Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::CreatureEditorWindow>("Creature Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::FactionEditorWindow>("Faction Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::FactionTemplateEditorWindow>("Faction Template Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ClassEditorWindow>("Class Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::RaceEditorWindow>("Race Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ModelEditorWindow>("Model Data Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ItemEditorWindow>("Item Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::UnitLootEditorWindow>("Unit Loot Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::TrainerEditorWindow>("Trainer Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::VendorEditorWindow>("Vendor Editor", project, mainWindow));

	mainWindow.AddImport(std::make_unique<mmo::TextureImport>());
	mainWindow.AddImport(std::make_unique<mmo::FbxImport>(mainWindow));

	mainWindow.AddEditor(std::make_unique<mmo::TextureEditor>(mainWindow));
	mainWindow.AddEditor(std::make_unique<mmo::MeshEditor>(mainWindow));
	mainWindow.AddEditor(std::make_unique<mmo::MaterialEditor>(mainWindow, previewProviderManager));
	mainWindow.AddEditor(std::make_unique<mmo::MaterialInstanceEditor>(mainWindow, previewProviderManager));
	mainWindow.AddEditor(std::make_unique<mmo::WorldEditor>(mainWindow, project));
	mainWindow.AddEditor(std::make_unique<mmo::WorldModelEditor>(mainWindow, project));

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
