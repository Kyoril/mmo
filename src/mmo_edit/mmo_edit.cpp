// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include <asio/io_context.hpp>
#include <asio/io_service.hpp>

#include "main_window.h"

#include "configuration.h"
#include "mysql_database.h"
#include "assets/asset_registry.h"
#include "shared/audio/audio.h"
#include "math/vector3.h"
#include "log/default_log_levels.h"

#ifdef _WIN32
#include "base/win_utility.h"
#include <crtdbg.h>
#include <fstream>
#include <sstream>
#include <mutex>
#endif

#ifdef MMO_WITH_FMOD
#include "fmod_audio/fmod_audio.h"
#else
#include "null_audio/null_audio.h"
#endif

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <utility>
#include <vector>

#include "preview_providers/preview_provider_manager.h"
#include "preview_providers/texture_preview_provider.h"
#include "preview_providers/material_preview_provider.h"
#include "preview_providers/mesh_preview_provider.h"
#include "preview_providers/particle_preview_provider.h"
#include "preview_providers/skeleton_preview_provider.h"
#include "preview_providers/interface_preview_providers.h"

#include "editor_windows/asset_window.h"
#include "editor_windows/log_window.h"
#include "editor_windows/spell_editor_window.h"
#include "editor_windows/spell_visualization_editor_window.h"
#include "editor_windows/map_editor_window.h"
#include "editor_windows/creature_editor_window.h"
#include "editor_windows/object_editor_window.h"
#include "editor_windows/class_editor_window.h"
#include "editor_windows/unit_class_editor_window.h"
#include "editor_windows/range_type_editor_window.h"
#include "editor_windows/aura_stacking_category_editor_window.h"
#include "editor_windows/lock_type_editor_window.h"
#include "editor_windows/item_editor_window.h"
#include "editor_windows/unit_loot_editor_window.h"
#include "editor_windows/faction_editor_window.h"
#include "editor_windows/faction_template_editor_window.h"
#include "editor_windows/model_editor_window.h"
#include "editor_windows/race_editor_window.h"
#include "editor_windows/proficiency_editor_window.h"
#include "editor_windows/skill_editor_window.h"
#include "editor_windows/pet_editor_window.h"
#include "editor_windows/chat_channel_editor_window.h"
#include "editor_windows/attribute_editor_window.h"
#include "editor_windows/emote_editor_window.h"
#include "editor_windows/item_subclass_editor_window.h"
#include "editor_windows/item_class_editor_window.h"
#include "editor_windows/trainer_editor_window.h"
#include "editor_windows/vendor_editor_window.h"
#include "editor_windows/quest_editor_window.h"
#include "editor_windows/zone_editor_window.h"
#include "editor_windows/sound_editor_window.h"
#include "editor_windows/surface_type_editor_window.h"
#include "editor_windows/loading_tip_editor_window.h"
#include "editor_windows/gossip_editor_window.h"
#include "editor_windows/item_display_editor_window.h"
#include "editor_windows/object_display_editor_window.h"
#include "editor_windows/condition_editor_window.h"
#include "editor_windows/variable_editor_window.h"
#include "editor_windows/trigger_editor_window.h"
#include "editor_windows/animation_editor_window.h"
#include "editor_windows/animation_profile_editor_window.h"
#include "editor_windows/talent_editor_window.h"
#include "editor_windows/combat_settings_editor_window.h"
#include "editor_windows/water_profile_editor_window.h"
#include "editor_windows/environment_profile_editor_window.h"
#include "editor_windows/data_navigator_window.h"

#include "import/texture_import.h"
#include "import/fbx_import.h"
#include "import/fbx_animation_import.h"

#include "locale_asset_utils.h"

#include "editors/mesh_editor/mesh_editor.h"
#include "editors/character_editor/character_editor.h"
#include "editors/material_editor/material_editor.h"
#include "editors/material_instance_editor/material_instance_editor.h"
#include "editors/texture_editor/texture_editor.h"
#include "editors/world_editor/world_editor.h"
#include "editors/world_editor/world_editor_instance.h"
#include "editors/world_editor/edit_modes/procedural_edit_mode.h"
#include "editors/world_editor/pcg/section_bake_job.h"
#include "editors/world_editor/pcg/graph_instance_builder.h"
#include "editors/world_editor/pcg/section_path_builder.h"
#include "editors/world_editor/pcg/text_parsing.h"
#include "terrain/terrain.h"
#include "editors/world_model_editor/world_model_editor.h"
#include "editors/world_model_editor/world_model_editor_instance.h"
#include "editors/prop_kit_editor/prop_kit_editor.h"
#include "editors/pcg_graph_editor/pcg_graph_editor.h"
#include "editors/pcg_control_type_editor/pcg_control_type_editor.h"
#include "prop_kit_job.h"
#include "entities_to_world_model_job.h"
#include "editors/mesh_editor/mesh_editor_instance.h"
#include "editors/color_curve_editor/color_curve_editor.h"
#include "editors/particle_system_editor/particle_system_editor.h"
#include "editors/global_shader_parameters_editor/global_shader_parameters_editor.h"
#include "editors/mesh_editor/mesh_collision_geometry.h"
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
		manager.AddPreviewProvider(std::make_unique<mmo::MeshPreviewProvider>(host));
		manager.AddPreviewProvider(std::make_unique<mmo::ParticlePreviewProvider>(host));
		manager.AddPreviewProvider(std::make_unique<mmo::SkeletonPreviewProvider>());
		manager.AddPreviewProvider(std::make_unique<mmo::LuaPreviewProvider>());
		manager.AddPreviewProvider(std::make_unique<mmo::XmlPreviewProvider>());
		manager.AddPreviewProvider(std::make_unique<mmo::TocPreviewProvider>());
		manager.AddPreviewProvider(std::make_unique<mmo::AudioPreviewProvider>());
	}

	/// Migrates locale asset references stored with their on-disk "Locales/Locale_<code>/" path
	/// to the locale-relative form, which is the only form that resolves in hpak release builds
	/// (the client mounts the active locale archive as an asset root). Changes are picked up by
	/// the next project save.
	void NormalizeLocaleAssetReferences(proto::Project& project)
	{
		size_t fixedCount = 0;

		for (auto& sound : *project.sounds.getTemplates().mutable_entry())
		{
			for (auto& file : *sound.mutable_files())
			{
				if (NormalizeLocaleAssetPath(file))
				{
					++fixedCount;
				}
			}
		}

		for (auto& visualization : *project.spellVisualizations.getTemplates().mutable_entry())
		{
			for (auto& kitsByEvent : *visualization.mutable_kits_by_event())
			{
				for (auto& kit : *kitsByEvent.second.mutable_kits())
				{
					for (auto& soundFile : *kit.mutable_sounds())
					{
						if (NormalizeLocaleAssetPath(soundFile))
						{
							++fixedCount;
						}
					}
				}
			}

			if (visualization.has_projectile())
			{
				for (auto& soundFile : *visualization.mutable_projectile()->mutable_sounds())
				{
					if (NormalizeLocaleAssetPath(soundFile))
					{
						++fixedCount;
					}
				}
			}

			for (auto& projectile : *visualization.mutable_projectiles())
			{
				for (auto& soundFile : *projectile.mutable_sounds())
				{
					if (NormalizeLocaleAssetPath(soundFile))
					{
						++fixedCount;
					}
				}
			}
		}

		if (fixedCount > 0)
		{
			ILOG("Migrated " << fixedCount << " locale-based asset path(s) to their locale-relative form");
		}
	}

	/// @brief Prepares an unattended command line job: the log goes to <name>.log (truncated) and, in
	///        debug builds, failed CRT assertions go to <name>_asserts.log instead of a message box
	///        nobody can answer. Each job runs alone in its process, so this is called once.
	/// @param name Base name of the log files, e.g. "pcg_rebuild".
	void RedirectUnattendedLog(const std::string& name)
	{
		static std::ofstream logFile;
		static std::mutex logMutex;
		if (logFile.is_open())
		{
			return;
		}

		logFile.open(name + ".log", std::ios::out | std::ios::trunc);
		g_DefaultLog.signal().connect([](const LogEntry& entry)
		{
			std::scoped_lock lock{ logMutex };
			logFile << entry.message << std::endl;
		});

#ifdef _DEBUG
		static HANDLE assertFile = CreateFileA((name + "_asserts.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, assertFile);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, assertFile);
#endif
	}

	/// @brief Opens a world for an unattended job.
	/// @param mainWindow The editor's main window.
	/// @param flag The command line flag, for the error message.
	/// @param worldPath Asset path of the .hwld file.
	/// @return The world editor instance, or null after logging an error.
	WorldEditorInstance* OpenWorldForJob(MainWindow& mainWindow, const char* flag, const char* worldPath)
	{
		if (!mainWindow.OpenAsset(worldPath) || !WorldEditorInstance::GetLastCreated())
		{
			ELOG(flag << ": failed to open " << worldPath);
			return nullptr;
		}

		return WorldEditorInstance::GetLastCreated();
	}

	/// @brief Parses a seed: the whole text is a decimal number without sign (strtoull would wrap a
	///        leading '-' around instead of refusing it).
	/// @param text The command line argument.
	/// @param seed Receives the seed.
	/// @return True if the text is a valid seed.
	bool ParseSeed(const char* text, uint64& seed)
	{
		if (text == nullptr || *text < '0' || *text > '9')
		{
			return false;
		}

		errno = 0;
		char* end = nullptr;
		seed = std::strtoull(text, &end, 10);
		return end != text && *end == '\0' && errno != ERANGE;
	}

	/// @brief Parses a point list "x,z;x,z;..." (each number as ParseFloatText reads it).
	/// @param text The command line argument.
	/// @param out Receives the points (cleared first).
	/// @param minCount Fewest points the list must have.
	/// @return True if every pair is valid and there are at least minCount points.
	bool ParsePointList(const String& text, std::vector<std::pair<float, float>>& out, const size_t minCount)
	{
		out.clear();
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t end = std::min(text.find(';', start), text.size());
			const String pair = text.substr(start, end - start);
			const size_t comma = pair.find(',');
			float x = 0.0f, z = 0.0f;
			if (comma == String::npos || !ParseFloatText(pair.substr(0, comma), x) || !ParseFloatText(pair.substr(comma + 1), z))
			{
				return false;
			}
			out.emplace_back(x, z);
			start = end + 1;
		}
		return out.size() >= minCount;
	}

	/// @brief Parses an oasis id: the whole text is a non-zero hexadecimal number.
	/// @param text The command line argument.
	/// @param id Receives the id.
	/// @return True if the text is a valid id.
	bool ParseOasisId(const char* text, uint64& id)
	{
		char* end = nullptr;
		id = std::strtoull(text, &end, 16);
		return end != text && *end == '\0' && id != 0;
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

	// Fix up any locale asset references still stored with their on-disk "Locales/..." path
	mmo::NormalizeLocaleAssetReferences(project);

	// Initialize the main window instance
	mmo::MainWindow mainWindow { config, project };
	mainWindow.AddEditorWindow(std::move(logWindow));

	// Setup preview provider manager
	mmo::PreviewProviderManager previewProviderManager;
	AddDefaultPreviewProviders(previewProviderManager, mainWindow);

	// Create FMOD audio system for sound preview in editor
#ifdef MMO_WITH_FMOD
	auto editorAudio = std::make_unique<mmo::FMODAudio>();
#else
	auto editorAudio = std::make_unique<mmo::NullAudio>();
#endif
	editorAudio->Create();
	ILOG("Editor audio system initialized");

	// Setup asset window
	auto assetWindow = std::make_unique<mmo::AssetWindow>("Asset Browser", previewProviderManager, mainWindow);
	mainWindow.AddEditorWindow(std::move(assetWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::RangeTypeEditorWindow>("Spell Range Type Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::AuraStackingCategoryEditorWindow>("Aura Stacking Category Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::LockTypeEditorWindow>("Lock Type Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::SpellEditorWindow>("Spell Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::SpellVisualizationEditorWindow>("Spell Visualization Editor", project, mainWindow, previewProviderManager, editorAudio.get()));
	mainWindow.AddEditorWindow(std::make_unique<mmo::QuestEditorWindow>("Quest Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::MapEditorWindow>("Map Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::CreatureEditorWindow>("Creature Editor", project, mainWindow, editorAudio.get()));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ObjectEditorWindow>("Object Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::FactionEditorWindow>("Faction Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::FactionTemplateEditorWindow>("Faction Template Editor", project, mainWindow));	mainWindow.AddEditorWindow(std::make_unique<mmo::ClassEditorWindow>("Class Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::UnitClassEditorWindow>("Unit Class Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::AttributeEditorWindow>("Attribute Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::RaceEditorWindow>("Race Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ProficiencyEditorWindow>("Proficiency Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::SkillEditorWindow>("Skill Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::PetEditorWindow>("Pet Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ChatChannelEditorWindow>("Chat Channel Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::EmoteEditorWindow>("Emote Editor", project, mainWindow, previewProviderManager));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ModelEditorWindow>("Model Data Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ItemEditorWindow>("Item Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ItemClassEditorWindow>("Item Class Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ItemSubclassEditorWindow>("Item Subclass Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ItemDisplayEditorWindow>("Item Display Editor", project, mainWindow, previewProviderManager, config));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ObjectDisplayEditorWindow>("Object Display Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::UnitLootEditorWindow>("Unit Loot Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::TrainerEditorWindow>("Trainer Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::VendorEditorWindow>("Vendor Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ZoneEditorWindow>("Zone Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::SoundEditorWindow>("Sound Editor", project, mainWindow, editorAudio.get()));
	mainWindow.AddEditorWindow(std::make_unique<mmo::SurfaceTypeEditorWindow>("Surface Type Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::WaterProfileEditorWindow>("Water Profile Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::EnvironmentProfileEditorWindow>("Environment Profile Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::LoadingTipEditorWindow>("Loading Tip Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::GossipEditorWindow>("Gossip Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::ConditionEditorWindow>("Condition Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::VariableEditorWindow>("Variable Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::TriggerEditorWindow>("Trigger Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::AnimationEditorWindow>("Animation Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::AnimationProfileEditorWindow>("Animation Profile Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::TalentEditorWindow>("Talent Editor", project, mainWindow));
	mainWindow.AddEditorWindow(std::make_unique<mmo::CombatSettingsEditorWindow>("Combat Settings", project, mainWindow));

	auto dataNavigatorWindow = std::make_unique<mmo::DataNavigatorWindow>("Data Navigator", project, mainWindow);
	
	// Set up the type-based window opening callback
	dataNavigatorWindow->SetOpenEditorWindowCallback([&mainWindow](std::type_index windowType) {
		// Find all editor windows and check their type
		for (int i = 0; i < mainWindow.GetWindowCount(); ++i)
		{
			mmo::EditorWindowBase* window = mainWindow.GetWindow(i);
			if (window) 
			{
				// Use runtime type information to check if this window matches the requested type
				if (std::type_index(typeid(*window)) == windowType)
				{
					window->SetVisible(true);
					break;
				}
			}
		}
		});
	mainWindow.AddEditorWindow(std::move(dataNavigatorWindow));

	mainWindow.AddImport(std::make_unique<mmo::TextureImport>());
	mainWindow.AddImport(std::make_unique<mmo::FbxImport>(mainWindow));

	mainWindow.AddEditor(std::make_unique<mmo::TextureEditor>(mainWindow));
	mainWindow.AddEditor(std::make_unique<mmo::MeshEditor>(mainWindow, previewProviderManager, project, editorAudio.get()));
	mainWindow.AddEditor(std::make_unique<mmo::CharacterEditor>(mainWindow));
	auto materialEditor = std::make_unique<mmo::MaterialEditor>(mainWindow, previewProviderManager, project);
	mmo::MaterialEditor* const materialEditorPtr = materialEditor.get();
	mainWindow.AddEditor(std::move(materialEditor));
	mainWindow.AddEditor(std::make_unique<mmo::MaterialInstanceEditor>(mainWindow, previewProviderManager, project));
	mainWindow.AddEditor(std::make_unique<mmo::WorldEditor>(mainWindow, project));
	mainWindow.AddEditor(std::make_unique<mmo::WorldModelEditor>(mainWindow, project));
	mainWindow.AddEditor(std::make_unique<mmo::PropKitEditor>(mainWindow, &previewProviderManager));
	mainWindow.AddEditor(std::make_unique<mmo::PcgGraphEditor>(mainWindow));
	mainWindow.AddEditor(std::make_unique<mmo::PcgControlTypeEditor>(mainWindow));
	mainWindow.AddEditor(std::make_unique<mmo::ColorCurveEditor>(mainWindow));
	mainWindow.AddEditor(std::make_unique<mmo::ParticleSystemEditor>(mainWindow));
	mainWindow.AddEditor(std::make_unique<mmo::GlobalShaderParametersEditor>(mainWindow));

	// Run the database service thread
	std::thread dbThread{ [&dbService]() { dbService.run(); } };

#ifdef _WIN32
	// Unattended jobs. `--rebuild-material <asset>` recompiles a material's shaders from its stored
	// graph, then exits. `--bake-terrain-lod Worlds/<n>/<n>.hwld [--force]` opens the world, bakes the
	// distant-terrain data of every page whose .tile changed since the last bake (every page with
	// --force), then exits. `--bake-worldmap Worlds/<n>/<n>.hwld` bakes the world map (zone grid and
	// map art pyramid, from the minimap tiles) into Textures/WorldMap/<n>/, then exits; minimap bakes
	// do this on their own when they finish. `--derive-rooms <asset.hwmo> [--derive-rooms ...]` derives the world
	// models' room volumes from their floors, fixes mislinked portals, saves them and exits.
	// `--stairs-to-ramp <asset.hmsh> [--stairs-to-ramp ...]` replaces the steps in the meshes'
	// collision with a ramp, saves them and exits.
	// `--rebake-collision <asset.hmsh> [--rebake-collision ...]` rebuilds the meshes' collision trees
	// from their stored collision recipes (editor-only CSRC chunk), saves them and exits.
	// `--build-collision <asset.hmsh> [--build-collision ...]` does the same, but gives meshes without a
	// recipe the default one (all render triangles) first, like Collision > Build for a fresh import.
	// `--resave-mesh <asset.hmsh> [--resave-mesh ...]` loads the meshes and saves them again with
	// their collision recipes, which bakes the load-time trim to four bone weights into the file.
	// `--set-submesh-material <asset.hmsh> <submesh> <material> [...]` assigns a material to a
	// submesh (by name, or index), saves the mesh with its collision recipe and exits.
	// `--set-mesh-sounds <asset.hmsh> "<id>:<x>,<y>,<z>[:<volume>[:<maxDistance>]];..." [...]` replaces
	// the mesh's looping sound emitters ("" removes them), saves it with its collision recipe and exits.
	// `--apply-prop-kit <job.json>` writes a prop kit asset, assigns kits to world model placements
	// and/or strips named mesh refs from a world model as the job describes, then exits (see
	// prop_kit_job.h; log in prop_kit_job.log, exit code 1 on load or write failures).
	{
		int argCount = 0;
		auto* const args = CommandLineToArgvA(GetCommandLine(), &argCount);

		// A job flag without its arguments fails the run instead of opening the editor, so a
		// scripted call cannot hang on an interactive window. Call it after the job's log is wired.
		const auto missingArgs = [&argCount](const int flagIndex, const int count, const char* usage)
		{
			if (flagIndex + count < argCount)
			{
				return false;
			}

			ELOG("missing arguments, usage: " << usage);
			PostQuitMessage(1);
			return true;
		};

		for (int i = 1; args && i < argCount; ++i)
		{
			if (std::string(args[i]) == "--rebuild-material")
			{
				// `--rebuild-material <asset> [--rebuild-material <asset> ...]`: recompile the named
				// materials from their stored graphs, re-save them, then exit (1 on any failure).
				static std::ofstream rebuildLog("material_rebuild.log", std::ios::out | std::ios::trunc);
				static std::mutex rebuildLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ rebuildLogMutex };
					rebuildLog << entry.message << std::endl;
				});

				std::vector<std::string> materials;
				for (int j = 1; j + 1 < argCount; ++j)
				{
					if (std::string(args[j]) == "--rebuild-material")
					{
						materials.emplace_back(args[j + 1]);
					}
				}

				const bool rebuilt = materialEditorPtr->RebuildMaterialsNow(materials);
				PostQuitMessage(rebuilt ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--rebake-collision" || std::string(args[i]) == "--build-collision" || std::string(args[i]) == "--build-collision-from")
			{
				static std::ofstream rebakeLog("collision_rebake.log", std::ios::out | std::ios::trunc);
				static std::mutex rebakeLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ rebakeLogMutex };
					rebakeLog << entry.message << std::endl;
				});
#ifdef _DEBUG
				// A broken mesh must fail the job, not park it behind an assert box nobody answers.
				static HANDLE assertFile = CreateFileA("collision_rebake_asserts.log", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
				_CrtSetReportFile(_CRT_ASSERT, assertFile);
				_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
				_CrtSetReportFile(_CRT_ERROR, assertFile);
#endif

				bool allRebaked = true;
				for (int j = 1; j + 1 < argCount; ++j)
				{
					if (std::string(args[j]) == "--rebake-collision")
					{
						allRebaked = mmo::RebakeMeshCollisionFile(args[j + 1]) && allRebaked;
					}
					else if (std::string(args[j]) == "--build-collision")
					{
						allRebaked = mmo::BuildMeshCollisionFile(args[j + 1]) && allRebaked;
					}
					else if (std::string(args[j]) == "--build-collision-from" && j + 2 < argCount)
					{
						// `--build-collision-from <asset> <submesh>[,<submesh>...]`
						std::vector<uint16> subMeshes;
						std::stringstream list(args[j + 2]);
						for (std::string index; std::getline(list, index, ',');)
						{
							subMeshes.push_back(static_cast<uint16>(std::stoi(index)));
						}
						allRebaked = mmo::BuildMeshCollisionFromSubMeshes(args[j + 1], subMeshes) && allRebaked;
					}
				}

				PostQuitMessage(allRebaked ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--derive-rooms")
			{
				static std::ofstream roomsLog("derive_rooms.log", std::ios::out | std::ios::trunc);
				static std::mutex roomsLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ roomsLogMutex };
					roomsLog << entry.message << std::endl;
				});

				bool allSaved = true;
				for (int j = 1; j + 1 < argCount; ++j)
				{
					if (std::string(args[j]) == "--derive-rooms")
					{
						allSaved = mmo::WorldModelEditorInstance::DeriveRoomsInFile(args[j + 1]) && allSaved;
					}
				}

				PostQuitMessage(allSaved ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--stairs-to-ramp")
			{
				static std::ofstream rampLog("stairs_to_ramp.log", std::ios::out | std::ios::trunc);
				static std::mutex rampLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ rampLogMutex };
					rampLog << entry.message << std::endl;
				});

				bool allSaved = true;
				for (int j = 1; j + 1 < argCount; ++j)
				{
					if (std::string(args[j]) == "--stairs-to-ramp")
					{
						allSaved = mmo::MeshEditorInstance::StairsToRampInFile(args[j + 1]) && allSaved;
					}
				}

				PostQuitMessage(allSaved ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--resave-mesh")
			{
				static std::ofstream resaveLog("resave_mesh.log", std::ios::out | std::ios::trunc);
				static std::mutex resaveLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ resaveLogMutex };
					resaveLog << entry.message << std::endl;
				});

				bool allSaved = true;
				for (int j = 1; j + 1 < argCount; ++j)
				{
					if (std::string(args[j]) == "--resave-mesh")
					{
						allSaved = mmo::MeshEditorInstance::ResaveMeshFile(args[j + 1]) && allSaved;
					}
				}

				PostQuitMessage(allSaved ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--set-submesh-material")
			{
				// `--set-submesh-material <asset.hmsh> <submesh name|index> <material> [...]`
				static std::ofstream materialLog("set_submesh_material.log", std::ios::out | std::ios::trunc);
				static std::mutex materialLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ materialLogMutex };
					materialLog << entry.message << std::endl;
				});

				bool allSaved = true;
				for (int j = 1; j < argCount; ++j)
				{
					if (std::string(args[j]) != "--set-submesh-material")
					{
						continue;
					}

					if (j + 3 >= argCount)
					{
						ELOG("--set-submesh-material needs <asset> <submesh> <material>");
						allSaved = false;
						break;
					}

					allSaved = mmo::MeshEditorInstance::SetSubMeshMaterialInFile(args[j + 1], args[j + 2], args[j + 3]) && allSaved;
					j += 3;
				}

				PostQuitMessage(allSaved ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--set-mesh-sounds")
			{
				// `--set-mesh-sounds <asset.hmsh> <spec> [...]`
				static std::ofstream soundsLog("set_mesh_sounds.log", std::ios::out | std::ios::trunc);
				static std::mutex soundsLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ soundsLogMutex };
					soundsLog << entry.message << std::endl;
				});

				bool allSaved = true;
				for (int j = 1; j < argCount; ++j)
				{
					if (std::string(args[j]) != "--set-mesh-sounds")
					{
						continue;
					}

					if (j + 2 >= argCount)
					{
						ELOG("--set-mesh-sounds needs <asset> <spec>");
						allSaved = false;
						break;
					}

					allSaved = mmo::MeshEditorInstance::SetSoundEmittersInFile(args[j + 1], args[j + 2]) && allSaved;
					j += 2;
				}

				PostQuitMessage(allSaved ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--apply-prop-kit")
			{
				static std::ofstream propKitLog("prop_kit_job.log", std::ios::out | std::ios::trunc);
				static std::mutex propKitLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ propKitLogMutex };
					propKitLog << entry.message << std::endl;
				});

				if (i + 1 >= argCount)
				{
					ELOG("--apply-prop-kit needs <job.json>");
					PostQuitMessage(1);
					break;
				}

				PostQuitMessage(mmo::ApplyPropKitJob(args[i + 1]) ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--entities-to-wmo")
			{
				// `--entities-to-wmo <job.json>` bundles placed mesh entities into one placed world model
				// (see entities_to_world_model_job.h; log in entities_to_wmo.log, exit code 1 on failure).
				static std::ofstream wmoJobLog("entities_to_wmo.log", std::ios::out | std::ios::trunc);
				static std::mutex wmoJobLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ wmoJobLogMutex };
					wmoJobLog << entry.message << std::endl;
				});

				if (i + 1 >= argCount)
				{
					ELOG("--entities-to-wmo needs <job.json>");
					PostQuitMessage(1);
					break;
				}

				PostQuitMessage(mmo::ApplyEntitiesToWorldModelJob(args[i + 1]) ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--import-texture")
			{
				// mmo_edit --import-texture <asset folder> <image>... [--normal | --grayscale] [--compress] [--height-map] [--mipmaps]
				static std::ofstream importLog("texture_import.log", std::ios::out | std::ios::trunc);
				static std::mutex importLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ importLogMutex };
					importLog << entry.message << std::endl;
				});

				if (i + 2 >= argCount)
				{
					ELOG("--import-texture needs <asset folder> <image>...");
					PostQuitMessage(1);
					break;
				}

				std::vector<std::filesystem::path> files;
				mmo::TextureUsage usage = mmo::TextureUsage::Color;
				bool compress = false;
				bool heightMap = false;
				bool mipMaps = false;
				for (int j = i + 2; j < argCount; ++j)
				{
					const std::string arg = args[j];
					if (arg == "--normal") usage = mmo::TextureUsage::NormalMap;
					else if (arg == "--grayscale") usage = mmo::TextureUsage::Grayscale;
					else if (arg == "--compress") compress = true;
					else if (arg == "--height-map") heightMap = true;
					else if (arg == "--mipmaps") mipMaps = true;
					else files.emplace_back(arg);
				}

				mmo::TextureImport importer;
				PostQuitMessage(importer.ImportUnattended(files, args[i + 1], usage, compress, heightMap, mipMaps) ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--import-fbx")
			{
				// mmo_edit --import-fbx <asset folder> <model>... [--scale <s> | --scale <x> <y> <z>]
				//   [--rotate <roll> <yaw> <pitch>] [--offset <x> <y> <z>] [--tangents] [--color-from-uv <channel>]
				// Same transform settings as the import dialog (rotation in degrees, yaw * pitch * roll).
				static std::ofstream importLog("fbx_import.log", std::ios::out | std::ios::trunc);
				static std::mutex importLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ importLogMutex };
					importLog << entry.message << std::endl;
				});

				if (i + 2 >= argCount)
				{
					ELOG("--import-fbx needs <asset folder> <model>...");
					PostQuitMessage(1);
					break;
				}

				const auto isNumber = [&](const int index)
				{
					char* end = nullptr;
					return index < argCount && std::strtof(args[index], &end) != HUGE_VALF && end && *end == '\0';
				};

				std::vector<std::filesystem::path> files;
				mmo::Vector3 offset = mmo::Vector3::Zero;
				mmo::Vector3 scale = mmo::Vector3::UnitScale;
				mmo::Quaternion rotation = mmo::Quaternion::Identity;
				bool tangents = false;
				int32 colorFromUv = -1;
				bool badArgs = false;
				for (int j = i + 2; j < argCount; ++j)
				{
					const std::string arg = args[j];
					if (arg == "--tangents")
					{
						tangents = true;
					}
					else if (arg == "--color-from-uv" && isNumber(j + 1))
					{
						colorFromUv = std::atoi(args[++j]);
					}
					else if (arg == "--scale" && isNumber(j + 1))
					{
						if (isNumber(j + 2) && isNumber(j + 3))
						{
							scale = mmo::Vector3(std::stof(args[j + 1]), std::stof(args[j + 2]), std::stof(args[j + 3]));
							j += 3;
						}
						else
						{
							const float s = std::stof(args[j + 1]);
							scale = mmo::Vector3(s, s, s);
							j += 1;
						}
					}
					else if ((arg == "--offset" || arg == "--rotate") && isNumber(j + 1) && isNumber(j + 2) && isNumber(j + 3))
					{
						const mmo::Vector3 v(std::stof(args[j + 1]), std::stof(args[j + 2]), std::stof(args[j + 3]));
						if (arg == "--offset")
						{
							offset = v;
						}
						else
						{
							const mmo::Quaternion qRoll(mmo::Degree(v.x), mmo::Vector3(1, 0, 0));
							const mmo::Quaternion qYaw(mmo::Degree(v.y), mmo::Vector3(0, 1, 0));
							const mmo::Quaternion qPitch(mmo::Degree(v.z), mmo::Vector3(0, 0, 1));
							rotation = qYaw * qPitch * qRoll;
							rotation.Normalize();
						}
						j += 3;
					}
					else if (arg.starts_with("--"))
					{
						ELOG("--import-fbx: unknown or incomplete option " << arg);
						badArgs = true;
					}
					else
					{
						files.emplace_back(arg);
					}
				}

				bool allImported = !badArgs && !files.empty();
				if (allImported)
				{
					mmo::FbxImport importer(mainWindow);
					for (const auto& file : files)
					{
						const bool imported = importer.ImportUnattended(file, args[i + 1], offset, scale, rotation, tangents, colorFromUv);
						ILOG((imported ? "Imported " : "Failed to import ") << file);
						allImported = imported && allImported;
					}
				}

				PostQuitMessage(allImported ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--import-fbx-animations")
			{
				// mmo_edit --import-fbx-animations <skeleton.skel> <model> [--scale <s>] [--clip <take>=<name>]...
				// Imports the model's animation takes into the skeleton asset. Without --clip every take keeps its
				// name; with it, only the listed takes are imported, renamed. A take matches by its full name or by
				// one of its '|' separated parts ("Minion|Idle01|BaseLayer" matches "Idle01").
				static std::ofstream importLog("fbx_animation_import.log", std::ios::out | std::ios::trunc);
				static std::mutex importLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ importLogMutex };
					importLog << entry.message << std::endl;
				});

				if (i + 2 >= argCount)
				{
					ELOG("--import-fbx-animations needs <skeleton.skel> <model>");
					PostQuitMessage(1);
					break;
				}

				float scale = 1.0f;
				std::vector<std::pair<std::string, std::string>> clips;
				bool badArgs = false;
				for (int j = i + 3; j < argCount; ++j)
				{
					const std::string arg = args[j];
					if (arg == "--scale" && j + 1 < argCount)
					{
						scale = std::stof(args[++j]);
					}
					else if (arg == "--clip" && j + 1 < argCount && std::string(args[j + 1]).find('=') != std::string::npos)
					{
						const std::string clip = args[++j];
						const size_t separator = clip.find('=');
						clips.emplace_back(clip.substr(0, separator), clip.substr(separator + 1));
					}
					else
					{
						ELOG("--import-fbx-animations: unknown or incomplete option " << arg);
						badArgs = true;
					}
				}

				const auto resolveName = [&clips](const std::string& takeName) -> std::string
				{
					std::vector<std::string> parts{ takeName };
					std::stringstream stream(takeName);
					for (std::string part; std::getline(stream, part, '|');)
					{
						parts.push_back(part);
					}

					if (clips.empty())
					{
						ILOG("Found take '" << takeName << "'");
						return takeName;
					}

					for (const auto& [source, target] : clips)
					{
						if (std::ranges::find(parts, source) != parts.end())
						{
							return target;
						}
					}

					ILOG("Skipping take '" << takeName << "'");
					return {};
				};

				const bool imported = !badArgs && mmo::ImportFbxAnimationsIntoAsset(args[i + 1], args[i + 2], resolveName, scale);
				PostQuitMessage(imported ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--capture-view")
			{
				// mmo_edit --capture-view <world.hwld> --view <x,y,z,yaw,pitch[,fov[,time]]> --out <file.png>
				//          [--view ... --out ...] [--size <w>x<h>] [--grass] [--fog] [--prepass] [--far-radius <pages>] [--warmup <frames>]
				// Renders each viewpoint of the world into a PNG, for comparing a zone against concept art.
				static std::ofstream captureLog("capture_view.log", std::ios::out | std::ios::trunc);
				static std::mutex captureLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ captureLogMutex };
					captureLog << entry.message << std::endl;
				});
#ifdef _DEBUG
				static HANDLE captureAssertFile = CreateFileA("capture_view_asserts.log", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
				_CrtSetReportFile(_CRT_ASSERT, captureAssertFile);
				_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
				_CrtSetReportFile(_CRT_ERROR, captureAssertFile);
#endif

				if (i + 1 >= argCount)
				{
					ELOG("--capture-view needs <world.hwld>");
					PostQuitMessage(1);
					break;
				}

				std::vector<mmo::EditorViewpoint> views;
				std::vector<std::string> outputs;
				uint32 width = 1920, height = 1080, warmup = 24;
				int32 farRadius = -1;
				bool grass = false;
				bool fog = false;
				bool prepass = false;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount; ++j)
				{
					const std::string arg = args[j];
					const bool hasValue = j + 1 < argCount;
					if (arg == "--view" && hasValue)
					{
						const auto viewpoint = mmo::EditorViewpoint::Parse(args[++j]);
						if (!viewpoint)
						{
							ELOG("--capture-view: cannot read viewpoint '" << args[j] << "'");
							argumentsValid = false;
							break;
						}
						views.push_back(*viewpoint);
					}
					else if (arg == "--out" && hasValue)
					{
						outputs.emplace_back(args[++j]);
					}
					else if (arg == "--size" && hasValue)
					{
						if (std::sscanf(args[++j], "%ux%u", &width, &height) != 2)
						{
							ELOG("--capture-view: --size expects <width>x<height>");
							argumentsValid = false;
							break;
						}
					}
					else if (arg == "--warmup" && hasValue)
					{
						warmup = static_cast<uint32>(std::atoi(args[++j]));
					}
					else if (arg == "--far-radius" && hasValue)
					{
						farRadius = std::atoi(args[++j]);
					}
					else if (arg == "--grass")
					{
						grass = true;
					}
					else if (arg == "--fog")
					{
						fog = true;
					}
					else if (arg == "--prepass")
					{
						prepass = true;
					}
				}

				if (argumentsValid && (views.empty() || views.size() != outputs.size()))
				{
					ELOG("--capture-view needs one --out per --view");
					argumentsValid = false;
				}

				bool captured = false;
				if (argumentsValid && mainWindow.OpenAsset(args[i + 1]) && mmo::WorldEditorInstance::GetLastCreated())
				{
					auto* world = mmo::WorldEditorInstance::GetLastCreated();
					if (farRadius >= 0)
					{
						world->SetFarTerrainRadius(static_cast<uint32>(farRadius));
					}
					world->SetFogEnabled(fog);
					if (prepass && world->GetDeferredRenderer())
					{
						world->GetDeferredRenderer()->SetDepthPrepassEnabled(true);
					}

					captured = true;
					for (size_t v = 0; v < views.size(); ++v)
					{
						captured = world->CaptureViewpoint(views[v], width, height, outputs[v], grass, warmup) && captured;
					}
				}
				else if (argumentsValid)
				{
					ELOG("--capture-view: failed to open " << args[i + 1]);
				}

				PostQuitMessage(captured ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--new-section")
			{
				// mmo_edit --new-section <Name> [--material <asset>] [--folder Editor/Pcg/Sections|Editor/Pcg/Biomes/<Biome>/Sections]
				// Creates a section world like "New Section..." (Editor/Pcg/Sections/<Name>/, or
				// <folder>/<Name>/ with --folder) and exits (0 = success).
				mmo::RedirectUnattendedLog("new_section");

				const char* usage = "--new-section <Name> [--material <asset>] [--folder Editor/Pcg/Sections|Editor/Pcg/Biomes/<Biome>/Sections]";
				if (missingArgs(i, 1, usage))
				{
					break;
				}

				std::string material = mmo::DefaultSectionTerrainMaterial;
				std::string folder;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount; ++j)
				{
					if (std::string(args[j]) == "--material" && j + 1 < argCount)
					{
						material = args[++j];
					}
					else if (std::string(args[j]) == "--folder" && j + 1 < argCount)
					{
						folder = args[++j];
					}
					else
					{
						ELOG("--new-section: unknown or incomplete option " << args[j] << ", usage: " << usage);
						argumentsValid = false;
					}
				}

				bool succeeded = false;
				if (argumentsValid)
				{
					std::string error;
					if (!mmo::CreateSectionWorld(folder, args[i + 1], material, error))
					{
						ELOG("--new-section: " << error);
					}
					else
					{
						ILOG("--new-section: created " << mmo::GetSectionWorldPath(folder, args[i + 1]));
						succeeded = true;
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--bake-section")
			{
				// mmo_edit --bake-section Editor/Pcg/Sections/<Name>/<Name>.hwld
				// Opens the section world, bakes it into Editor/Pcg/Sections/<Name>.hsec like the Bake
				// button (without saving the world first) and exits (0 = success).
				mmo::RedirectUnattendedLog("bake_section");

				if (missingArgs(i, 1, "--bake-section <world.hwld>"))
				{
					break;
				}

				bool succeeded = false;
				if (!mmo::AssetRegistry::HasFile(args[i + 1]))
				{
					ELOG("--bake-section: " << args[i + 1] << " does not exist");
				}
				else if (!mmo::IsSectionWorld(args[i + 1]))
				{
					ELOG("--bake-section: " << args[i + 1] << " is not a section world (no " << mmo::GetSectionDefinitionPath(args[i + 1]) << ")");
				}
				else if (mmo::WorldEditorInstance* world = mmo::OpenWorldForJob(mainWindow, "--bake-section", args[i + 1]))
				{
					std::string error;
					if (!mmo::BakeSectionWorld(*world, error))
					{
						ELOG("--bake-section: " << error);
					}
					else
					{
						ILOG("--bake-section: done");
						succeeded = true;
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--pcg-rebuild")
			{
				// mmo_edit --pcg-rebuild <world.hwld> [--oasis|--id <id hex>] [--background]
				// Opens the world, rebuilds one or all procedural controls (oases, section paths and graph
				// instances; a graph instance uses its saved graph), saves the world and exits (0 = success).
				// --background rebuilds the graph instance --id names on a worker thread, like the editor.
				mmo::RedirectUnattendedLog("pcg_rebuild");

				if (missingArgs(i, 1, "--pcg-rebuild <world.hwld> [--oasis|--id <id hex>] [--background]"))
				{
					break;
				}

				uint64 oasisId = 0;
				bool background = false;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount; ++j)
				{
					if ((std::string(args[j]) == "--oasis" || std::string(args[j]) == "--id") && j + 1 < argCount)
					{
						if (!mmo::ParseOasisId(args[++j], oasisId))
						{
							ELOG("--pcg-rebuild: --oasis/--id expects a non-zero hexadecimal id, got '" << args[j] << "'");
							argumentsValid = false;
						}
					}
					else if (std::string(args[j]) == "--background")
					{
						// A graph instance is rebuilt the way the editor does it in the background.
						background = true;
					}
					else
					{
						ELOG("--pcg-rebuild: unknown or incomplete option " << args[j]);
						argumentsValid = false;
					}
				}

				bool succeeded = false;
				if (!argumentsValid)
				{
					// Already logged.
				}
				else if (mmo::WorldEditorInstance* world = mmo::OpenWorldForJob(mainWindow, "--pcg-rebuild", args[i + 1]))
				{
					if (world->GetProceduralControlCount() == 0)
					{
						// Nothing to rebuild: leave the world untouched instead of rewriting it.
						ILOG("--pcg-rebuild: " << args[i + 1] << " has no procedural controls, nothing to do");
						succeeded = true;
					}
					else
					{
						std::string error;
						if (!world->RebuildProcedural(oasisId, error, background))
						{
							ELOG("--pcg-rebuild: " << error);
						}
						else if (!world->Save())
						{
							ELOG("--pcg-rebuild: saving the world failed");
						}
						else
						{
							ILOG("--pcg-rebuild: done");
							succeeded = true;
						}
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--pcg-new-oasis")
			{
				// mmo_edit --pcg-new-oasis <world.hwld> --at X Z [--radius R] [--seed S] [--name N]
				// Adds an oasis like the "New Oasis" button (8 point loop around X Z), builds it, saves
				// the world and logs the new id (0 = success).
				mmo::RedirectUnattendedLog("pcg_new_oasis");

				const char* usage = "--pcg-new-oasis <world.hwld> --at X Z [--radius R] [--seed S] [--name N]";
				if (missingArgs(i, 1, usage))
				{
					break;
				}

				float x = 0.0f, z = 0.0f, radius = 40.0f;
				uint64 seed = 1;
				std::string name;
				bool hasCentre = false;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount && argumentsValid; ++j)
				{
					const std::string option = args[j];
					if (option == "--at" && j + 2 < argCount)
					{
						argumentsValid = mmo::ParseFloatText(args[j + 1], x) && mmo::ParseFloatText(args[j + 2], z);
						hasCentre = argumentsValid;
						j += 2;
					}
					else if (option == "--radius" && j + 1 < argCount)
					{
						argumentsValid = mmo::ParseFloatText(args[++j], radius) && radius > 0.0f;
					}
					else if (option == "--seed" && j + 1 < argCount)
					{
						argumentsValid = mmo::ParseSeed(args[++j], seed);
					}
					else if (option == "--name" && j + 1 < argCount)
					{
						name = args[++j];
					}
					else
					{
						argumentsValid = false;
					}

					if (!argumentsValid)
					{
						ELOG("--pcg-new-oasis: invalid or incomplete option " << option << ", usage: " << usage);
					}
				}

				if (argumentsValid && !hasCentre)
				{
					ELOG("--pcg-new-oasis: --at X Z is required, usage: " << usage);
					argumentsValid = false;
				}

				bool succeeded = false;
				if (!argumentsValid)
				{
					// Already logged.
				}
				else if (mmo::WorldEditorInstance* world = mmo::OpenWorldForJob(mainWindow, "--pcg-new-oasis", args[i + 1]))
				{
					uint64 id = 0;
					std::string error;
					if (!world->CreateProceduralOasis(x, z, radius, seed, name, id, error))
					{
						// The world is not saved: a failed build leaves no record behind.
						ELOG("--pcg-new-oasis: " << error);
					}
					else if (!world->Save())
					{
						ELOG("--pcg-new-oasis: saving the world failed");
					}
					else
					{
						char idText[32];
						std::snprintf(idText, sizeof(idText), "%016llx", static_cast<unsigned long long>(id));
						ILOG("--pcg-new-oasis: created oasis " << idText);
						succeeded = true;
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--pcg-new-path")
			{
				// mmo_edit --pcg-new-path <world.hwld> --points x,z;x,z[;...] --section <asset> [--seed S] [--name N] [--closed]
				//          [--height-mode followSpline|followGround|smoothedGround|keepGround] [--smoothing W] [--mass] [--mirror]
				//          [--set name=value ...]
				// Adds a section path through the points (at terrain height), builds it, saves the world and
				// logs the new id and its layout (0 = success). A missing or unreadable section asset is refused
				// before anything is added or saved. --closed makes it a section loop (at least three points,
				// height mode SmoothedGround unless --height-mode says otherwise, floor side inside). --mass turns
				// the mass layer on, --mirror the facade variety (mirrored copies, 20 % height jitter); --set sets
				// any path parameter by name (e.g. --set massHeight=70), applied last.
				mmo::RedirectUnattendedLog("pcg_new_path");

				const char* usage = "--pcg-new-path <world.hwld> --points x,z;x,z[;...] --section <asset> [--seed S] [--name N] [--closed] "
					"[--height-mode followSpline|followGround|smoothedGround|keepGround] [--smoothing W] [--mass] [--mirror] [--set name=value ...]";
				if (missingArgs(i, 1, usage))
				{
					break;
				}

				std::vector<std::pair<float, float>> points;
				std::string section;
				std::string name;
				mmo::NewSectionPathOptions options;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount && argumentsValid; ++j)
				{
					const std::string option = args[j];
					if (option == "--points" && j + 1 < argCount)
					{
						argumentsValid = mmo::ParsePointList(args[++j], points, 2);
					}
					else if (option == "--section" && j + 1 < argCount)
					{
						section = args[++j];
						argumentsValid = !section.empty();
					}
					else if (option == "--seed" && j + 1 < argCount)
					{
						argumentsValid = mmo::ParseSeed(args[++j], options.seed);
					}
					else if (option == "--name" && j + 1 < argCount)
					{
						name = args[++j];
					}
					else if (option == "--closed")
					{
						options.closed = true;
					}
					else if (option == "--height-mode" && j + 1 < argCount)
					{
						mmo::pcg::SectionPathParams probe;
						std::string error;
						argumentsValid = mmo::pcg::SetSectionPathParam(probe, "heightMode", args[++j], error);
						options.heightMode = probe.heightMode;
					}
					else if (option == "--smoothing" && j + 1 < argCount)
					{
						float smoothing = 0.0f;
						argumentsValid = mmo::ParseFloatText(args[++j], smoothing) && smoothing >= 0.0f;
						options.groundSmoothing = smoothing;
					}
					else if (option == "--mass")
					{
						options.mass = true;
					}
					else if (option == "--mirror")
					{
						options.mirror = true;
					}
					else if (option == "--set" && j + 1 < argCount)
					{
						// Checked here so a typo is refused before the world is opened.
						const std::string setting = args[++j];
						const size_t equals = setting.find('=');
						mmo::pcg::SectionPathParams probe;
						std::string error;
						argumentsValid = equals != std::string::npos &&
							mmo::pcg::SetSectionPathParam(probe, setting.substr(0, equals), setting.substr(equals + 1), error);
						if (argumentsValid)
						{
							options.settings.emplace_back(setting.substr(0, equals), setting.substr(equals + 1));
						}
						else if (!error.empty())
						{
							ELOG("--pcg-new-path: " << error);
						}
					}
					else
					{
						argumentsValid = false;
					}

					if (!argumentsValid)
					{
						ELOG("--pcg-new-path: invalid or incomplete option " << option << ", usage: " << usage);
					}
				}

				if (argumentsValid && (points.empty() || section.empty()))
				{
					ELOG("--pcg-new-path: --points (at least two x,z pairs) and --section are required, usage: " << usage);
					argumentsValid = false;
				}
				if (argumentsValid && options.closed && points.size() < 3)
				{
					ELOG("--pcg-new-path: --closed needs at least three x,z pairs, usage: " << usage);
					argumentsValid = false;
				}

				bool succeeded = false;
				if (!argumentsValid)
				{
					// Already logged.
				}
				else if (mmo::WorldEditorInstance* world = mmo::OpenWorldForJob(mainWindow, "--pcg-new-path", args[i + 1]))
				{
					uint64 id = 0;
					std::string error;
					if (!world->CreateProceduralSectionPath(points, section, options, name, id, error))
					{
						// The world is not saved: a refused or failed run leaves no record behind.
						ELOG("--pcg-new-path: " << error);
					}
					else if (!world->Save())
					{
						ELOG("--pcg-new-path: saving the world failed");
					}
					else
					{
						char idText[32];
						std::snprintf(idText, sizeof(idText), "%016llx", static_cast<unsigned long long>(id));
						ILOG("--pcg-new-path: created section " << (options.closed ? "loop " : "path ") << idText);
						succeeded = true;
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--pcg-new-control")
			{
				// mmo_edit --pcg-new-control <world.hwld> --type <typePath> --points x,z[;x,z...] [--name N] [--set prop=value ...]
				//     [--half-extents x,z] [--yaw <deg>]
				// Places a control of a control type (.hctl): a loop through at least three points, a spline through
				// at least two, a volume, rectangle or point at exactly one (heights from the terrain). --set overrides a
				// property of the type (typed by its definition; colours as r,g,b). --half-extents (volume, rectangle) and
				// --yaw (volume, rectangle, point) replace the default size and rotation. Saves the world and logs the new
				// id (0 = success). A missing or invalid type, points that do not fit its shape, a bad --set or a transform
				// the shape does not have are refused before anything is added or saved.
				mmo::RedirectUnattendedLog("pcg_new_control");

				const char* usage = "--pcg-new-control <world.hwld> --type <typePath> --points x,z[;x,z...] [--name N] [--set prop=value ...] "
					"[--half-extents x,z] [--yaw <deg>]";
				if (missingArgs(i, 1, usage))
				{
					break;
				}

				std::vector<std::pair<float, float>> points;
				std::string typePath;
				std::string name;
				std::vector<std::pair<std::string, std::string>> settings;
				mmo::NewPlacedControlTransform transform;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount && argumentsValid; ++j)
				{
					const std::string option = args[j];
					if (option == "--points" && j + 1 < argCount)
					{
						argumentsValid = mmo::ParsePointList(args[++j], points, 1);
					}
					else if (option == "--half-extents" && j + 1 < argCount)
					{
						// One "x,z" pair; the shape and the minimum are checked once the type is read.
						std::vector<std::pair<float, float>> extents;
						argumentsValid = mmo::ParsePointList(args[++j], extents, 1) && extents.size() == 1;
						if (argumentsValid)
						{
							transform.halfExtents = extents.front();
						}
					}
					else if (option == "--yaw" && j + 1 < argCount)
					{
						float yaw = 0.0f;
						argumentsValid = mmo::ParseFloatText(args[++j], yaw);
						if (argumentsValid)
						{
							transform.yawDegrees = yaw;
						}
					}
					else if (option == "--type" && j + 1 < argCount)
					{
						typePath = args[++j];
						argumentsValid = !typePath.empty();
					}
					else if (option == "--name" && j + 1 < argCount)
					{
						name = args[++j];
					}
					else if (option == "--set" && j + 1 < argCount)
					{
						// Checked against the type once the world is open (the type is read there).
						const std::string setting = args[++j];
						const size_t equals = setting.find('=');
						argumentsValid = equals != std::string::npos && equals > 0;
						if (argumentsValid)
						{
							settings.emplace_back(setting.substr(0, equals), setting.substr(equals + 1));
						}
					}
					else
					{
						argumentsValid = false;
					}

					if (!argumentsValid)
					{
						ELOG("--pcg-new-control: invalid or incomplete option " << option << ", usage: " << usage);
					}
				}

				if (argumentsValid && (points.empty() || typePath.empty()))
				{
					ELOG("--pcg-new-control: --type and --points are required, usage: " << usage);
					argumentsValid = false;
				}

				bool succeeded = false;
				if (!argumentsValid)
				{
					// Already logged.
				}
				else if (mmo::WorldEditorInstance* world = mmo::OpenWorldForJob(mainWindow, "--pcg-new-control", args[i + 1]))
				{
					uint64 id = 0;
					std::string error;
					if (!world->CreateProceduralControl(typePath, points, name, settings, id, error, transform))
					{
						// The world is not saved: a refused run leaves no record behind.
						ELOG("--pcg-new-control: " << error);
					}
					else if (!world->Save())
					{
						ELOG("--pcg-new-control: saving the world failed");
					}
					else
					{
						char idText[32];
						std::snprintf(idText, sizeof(idText), "%016llx", static_cast<unsigned long long>(id));
						ILOG("--pcg-new-control: created control " << idText);
						succeeded = true;
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--pcg-transform-control")
			{
				// mmo_edit --pcg-transform-control <world.hwld> --id <hex> [--move dx,dz] [--rotate <deg>] [--scale sx,sz]
				// Moves, rotates (around up, about the control's pivot) and scales (along world x/z, about the pivot) a
				// placed control of any shape as a whole, in that order, like the whole-control gizmo. Loops and splines
				// change their points (ids, widths and heights stay), volumes, rectangles and points their position,
				// rotation and half extents. Saves the world (0 = success); an unknown id or a bad value saves nothing (1).
				mmo::RedirectUnattendedLog("pcg_transform_control");

				const char* usage = "--pcg-transform-control <world.hwld> --id <hex> [--move dx,dz] [--rotate <deg>] [--scale sx,sz]";
				if (missingArgs(i, 1, usage))
				{
					break;
				}

				uint64 controlId = 0;
				mmo::PlacedControlTransformEdit edit;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount && argumentsValid; ++j)
				{
					const std::string option = args[j];
					if (option == "--id" && j + 1 < argCount)
					{
						argumentsValid = mmo::ParseOasisId(args[++j], controlId);
					}
					else if ((option == "--move" || option == "--scale") && j + 1 < argCount)
					{
						std::vector<std::pair<float, float>> values;
						argumentsValid = mmo::ParsePointList(args[++j], values, 1) && values.size() == 1;
						if (argumentsValid)
						{
							(option == "--move" ? edit.move : edit.scale) = values.front();
						}
					}
					else if (option == "--rotate" && j + 1 < argCount)
					{
						float degrees = 0.0f;
						argumentsValid = mmo::ParseFloatText(args[++j], degrees);
						if (argumentsValid)
						{
							edit.rotateDegrees = degrees;
						}
					}
					else
					{
						argumentsValid = false;
					}

					if (!argumentsValid)
					{
						ELOG("--pcg-transform-control: invalid or incomplete option " << option << ", usage: " << usage);
					}
				}

				if (argumentsValid && controlId == 0)
				{
					ELOG("--pcg-transform-control: --id is required, usage: " << usage);
					argumentsValid = false;
				}

				bool succeeded = false;
				if (!argumentsValid)
				{
					// Already logged.
				}
				else if (mmo::WorldEditorInstance* world = mmo::OpenWorldForJob(mainWindow, "--pcg-transform-control", args[i + 1]))
				{
					std::string error;
					if (!world->TransformProceduralControl(controlId, edit, error))
					{
						// The world is not saved: a refused run changes nothing.
						ELOG("--pcg-transform-control: " << error);
					}
					else if (!world->Save())
					{
						ELOG("--pcg-transform-control: saving the world failed");
					}
					else
					{
						ILOG("--pcg-transform-control: done");
						succeeded = true;
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--pcg-new-graph-instance")
			{
				// mmo_edit --pcg-new-graph-instance <world.hwld> --graph <path> --points x,z;x,z;x,z[;...] [--seed S] [--name N]
				//          [--set param=value ...]
				// Adds a graph instance over the closed area through the points (sharp corners) with the graph, seed
				// (default and 0: derived from the instance's id)
				// and parameter overrides (typed by the graph's parameters; --set borderFade=M sets the instance's
				// terrain border fade in metres instead, default 8). Does not build it (use --pcg-rebuild);
				// saves the world and logs the new id (0 = success). An area overlapping another graph instance's
				// area ("overlaps"), an unreadable graph or a bad --set are refused before anything is added or saved.
				mmo::RedirectUnattendedLog("pcg_new_graph_instance");

				const char* usage = "--pcg-new-graph-instance <world.hwld> --graph <path> --points x,z;x,z;x,z[;...] [--seed S] [--name N] [--set param=value ...]";
				if (missingArgs(i, 1, usage))
				{
					break;
				}

				std::vector<std::pair<float, float>> points;
				std::string graphPath;
				std::string name;
				// 0: derived from the new instance's id (as the editor's button does); --seed overrides it.
				uint64 seed = 0;
				std::vector<std::pair<std::string, std::string>> settings;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount && argumentsValid; ++j)
				{
					const std::string option = args[j];
					if (option == "--points" && j + 1 < argCount)
					{
						argumentsValid = mmo::ParsePointList(args[++j], points, 3);
					}
					else if (option == "--graph" && j + 1 < argCount)
					{
						graphPath = args[++j];
						argumentsValid = !graphPath.empty();
					}
					else if (option == "--seed" && j + 1 < argCount)
					{
						argumentsValid = mmo::ParseSeed(args[++j], seed);
					}
					else if (option == "--name" && j + 1 < argCount)
					{
						name = args[++j];
					}
					else if (option == "--set" && j + 1 < argCount)
					{
						// Checked against the graph's parameters once the world is open.
						const std::string setting = args[++j];
						const size_t equals = setting.find('=');
						argumentsValid = equals != std::string::npos && equals > 0;
						if (argumentsValid)
						{
							settings.emplace_back(setting.substr(0, equals), setting.substr(equals + 1));
						}
					}
					else
					{
						argumentsValid = false;
					}

					if (!argumentsValid)
					{
						ELOG("--pcg-new-graph-instance: invalid or incomplete option " << option << ", usage: " << usage);
					}
				}

				if (argumentsValid && (points.empty() || graphPath.empty()))
				{
					ELOG("--pcg-new-graph-instance: --graph and --points (at least three x,z pairs) are required, usage: " << usage);
					argumentsValid = false;
				}

				bool succeeded = false;
				if (!argumentsValid)
				{
					// Already logged.
				}
				else if (mmo::WorldEditorInstance* world = mmo::OpenWorldForJob(mainWindow, "--pcg-new-graph-instance", args[i + 1]))
				{
					uint64 id = 0;
					std::string error;
					if (!world->CreateProceduralGraphInstance(graphPath, points, seed, name, settings, id, error))
					{
						// The world is not saved: a refused run leaves no record behind.
						ELOG("--pcg-new-graph-instance: " << error);
					}
					else if (!world->Save())
					{
						ELOG("--pcg-new-graph-instance: saving the world failed");
					}
					else
					{
						char idText[32];
						std::snprintf(idText, sizeof(idText), "%016llx", static_cast<unsigned long long>(id));
						ILOG("--pcg-new-graph-instance: created graph instance " << idText);
						succeeded = true;
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--pcg-eval-graph")
			{
				// mmo_edit --pcg-eval-graph <world.hwld> --id <id hex> --out <file.json>
				// Evaluates a graph instance's saved graph without applying it and writes the evaluation dump
				// (WriteEvalDump, with timing) to the file. Like a rebuild, it evaluates on the base terrain (the instance's
				// own terrain contribution undone, BaseTerrainSampler). Logs validation issues, node errors, how many spawned
				// points lie inside the area and how many were clipped, and the terrain height (GetSmoothHeightAt)
				// under the first three placements next to their evaluated height. Never saves the world. Exit 0 if
				// the graph validated and no node failed (the dump is written in either case).
				mmo::RedirectUnattendedLog("pcg_eval_graph");

				const char* usage = "--pcg-eval-graph <world.hwld> --id <id hex> --out <file.json>";
				if (missingArgs(i, 1, usage))
				{
					break;
				}

				uint64 instanceId = 0;
				std::string outPath;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount && argumentsValid; ++j)
				{
					const std::string option = args[j];
					if (option == "--id" && j + 1 < argCount)
					{
						argumentsValid = mmo::ParseOasisId(args[++j], instanceId);
					}
					else if (option == "--out" && j + 1 < argCount)
					{
						outPath = args[++j];
						argumentsValid = !outPath.empty();
					}
					else
					{
						argumentsValid = false;
					}

					if (!argumentsValid)
					{
						ELOG("--pcg-eval-graph: invalid or incomplete option " << option << ", usage: " << usage);
					}
				}

				if (argumentsValid && (instanceId == 0 || outPath.empty()))
				{
					ELOG("--pcg-eval-graph: --id and --out are required, usage: " << usage);
					argumentsValid = false;
				}

				bool succeeded = false;
				if (!argumentsValid)
				{
					// Already logged.
				}
				else if (mmo::WorldEditorInstance* world = mmo::OpenWorldForJob(mainWindow, "--pcg-eval-graph", args[i + 1]))
				{
					mmo::GraphInstanceEvaluation evaluation;
					std::string error;
					if (!world->EvaluateProceduralGraphInstance(instanceId, evaluation, error))
					{
						ELOG("--pcg-eval-graph: " << error);
					}
					else
					{
						const std::string dump = mmo::pcg::graph::WriteEvalDump(evaluation.graph, evaluation.result, true);
						std::ofstream outFile(outPath, std::ios::out | std::ios::trunc | std::ios::binary);
						outFile << dump;
						outFile.close();
						const bool written = static_cast<bool>(outFile);
						if (!written)
						{
							ELOG("--pcg-eval-graph: could not write " << outPath);
						}

						for (const std::string& warning : evaluation.warnings)
						{
							WLOG("--pcg-eval-graph: warning: " << warning);
						}

						bool nodeFailed = false;
						for (const auto& [nodeId, nodeResult] : evaluation.result.nodes)
						{
							if (!nodeResult.output.error.empty())
							{
								ELOG("--pcg-eval-graph: node " << nodeId << " failed: " << nodeResult.output.error);
								nodeFailed = true;
							}
						}

						if (!evaluation.result.issues.empty())
						{
							ELOG("--pcg-eval-graph: validation issues: " << mmo::DescribeGraphIssues(evaluation.graph, evaluation.result.issues));
						}
						else
						{
							const size_t inside = evaluation.collected.placements.size();
							ILOG("--pcg-eval-graph: spawned points " << inside + evaluation.collected.clipped << ", inside the area " << inside
								<< ", clipped " << evaluation.collected.clipped);

							// The evaluated heights next to the terrain's own answer (the sampler must agree with it) and, when
							// the instance owns terrain heights, the base height the evaluation ran on (its own contribution
							// undone, as a rebuild sees the terrain).
							mmo::terrain::Terrain* terrain = world->GetTerrain();
							const mmo::ProceduralEditMode* mode = world->GetProceduralEditMode();
							const mmo::pcg::GraphInstanceRecord* record = mode ? mode->FindGraphInstance(instanceId) : nullptr;
							std::unique_ptr<mmo::EditorTerrainSampler> live;
							std::unique_ptr<mmo::BaseTerrainSampler> base;
							if (terrain && record)
							{
								live = std::make_unique<mmo::EditorTerrainSampler>(*terrain, nullptr);
								base = mmo::MakeBaseTerrainSampler(*world, *record, *live);
							}
							if (base)
							{
								ILOG("--pcg-eval-graph: evaluated on the base terrain (the instance's contribution of "
									<< record->content.terrain.outer.size() << " outer height(s), " << record->content.terrain.splat.size()
									<< " splat pixel(s), " << record->content.terrain.waterQuads.size() << " water quad(s) and "
									<< record->content.terrain.waterHeights.size() << " water height(s) undone)");
							}
							for (size_t p = 0; terrain && p < inside && p < 3; ++p)
							{
								const mmo::Vector3& position = evaluation.collected.placements[p].position;
								float baseHeight = 0.0f;
								const bool hasBase = base && base->Height(position.x, position.z, baseHeight);
								char line[192];
								std::snprintf(line, sizeof(line), "height check %zu: x %.3f z %.3f evaluated y %.4f GetSmoothHeightAt %.4f", p,
									static_cast<double>(position.x), static_cast<double>(position.z), static_cast<double>(position.y),
									static_cast<double>(terrain->GetSmoothHeightAt(position.x, position.z)));
								char baseText[48] = "";
								if (hasBase)
								{
									std::snprintf(baseText, sizeof(baseText), " base %.4f", static_cast<double>(baseHeight));
								}
								ILOG("--pcg-eval-graph: " << line << baseText);
							}
						}

						if (written)
						{
							ILOG("--pcg-eval-graph: wrote " << outPath);
						}
						succeeded = written && evaluation.result.ok && evaluation.result.issues.empty() && !nodeFailed;
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--pcg-delete")
			{
				// mmo_edit --pcg-delete <world.hwld> --oasis|--id <id hex>
				// Deletes a procedural control (oasis, section path, graph instance or placed control) and
				// everything it generated like the "Delete" button, saves the world and exits (0 = success).
				// Deleting a placed control and creating it again (--pcg-new-control) moves it.
				mmo::RedirectUnattendedLog("pcg_delete");

				const char* usage = "--pcg-delete <world.hwld> --oasis|--id <id hex>";
				if (missingArgs(i, 1, usage))
				{
					break;
				}

				uint64 oasisId = 0;
				bool argumentsValid = true;
				for (int j = i + 2; j < argCount; ++j)
				{
					if ((std::string(args[j]) == "--oasis" || std::string(args[j]) == "--id") && j + 1 < argCount)
					{
						if (!mmo::ParseOasisId(args[++j], oasisId))
						{
							ELOG("--pcg-delete: --oasis/--id expects a non-zero hexadecimal id, got '" << args[j] << "'");
							argumentsValid = false;
						}
					}
					else
					{
						ELOG("--pcg-delete: unknown or incomplete option " << args[j] << ", usage: " << usage);
						argumentsValid = false;
					}
				}

				if (argumentsValid && oasisId == 0)
				{
					ELOG("--pcg-delete: --oasis or --id is required, usage: " << usage);
					argumentsValid = false;
				}

				bool succeeded = false;
				if (!argumentsValid)
				{
					// Already logged.
				}
				else if (mmo::WorldEditorInstance* world = mmo::OpenWorldForJob(mainWindow, "--pcg-delete", args[i + 1]))
				{
					std::string error;
					if (!world->DeleteProceduralControl(oasisId, error))
					{
						ELOG("--pcg-delete: " << error);
					}
					else if (!world->Save())
					{
						ELOG("--pcg-delete: saving the world failed");
					}
					else
					{
						ILOG("--pcg-delete: done");
						succeeded = true;
					}
				}

				PostQuitMessage(succeeded ? 0 : 1);
				break;
			}

			if (std::string(args[i]) == "--bake-terrain-lod")
			{
				// Nobody watches an unattended run: keep its log, and let failed debug assertions
				// land in that log instead of a message box nobody can answer.
				static std::ofstream bakeLog("terrain_lod_bake.log", std::ios::out | std::ios::trunc);
				static std::mutex bakeLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ bakeLogMutex };
					bakeLog << entry.message << std::endl;
				});
#ifdef _DEBUG
				static HANDLE assertFile = CreateFileA("terrain_lod_bake_asserts.log", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
				_CrtSetReportFile(_CRT_ASSERT, assertFile);
				_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
				_CrtSetReportFile(_CRT_ERROR, assertFile);
#endif

				if (missingArgs(i, 1, "--bake-terrain-lod <world>"))
				{
					break;
				}

				// Bake right away instead of from the viewport's paint handler, so the job also runs
				// with the window minimised (a minimised window never paints).
				if (mainWindow.OpenAsset(args[i + 1]) && mmo::WorldEditorInstance::GetLastCreated())
				{
					bool force = false;
					for (int j = 1; j < argCount; ++j)
					{
						force = force || std::string(args[j]) == "--force";
					}

					// Exit code 1 when any page failed, for scripted use.
					const bool baked = mmo::WorldEditorInstance::GetLastCreated()->GenerateTerrainLod(!force);
					PostQuitMessage(baked ? 0 : 1);
				}
				else
				{
					ELOG("--bake-terrain-lod: failed to open " << args[i + 1]);
					PostQuitMessage(1);
				}
				break;
			}

			if (std::string(args[i]) == "--bake-worldmap")
			{
				static std::ofstream bakeLog("worldmap_bake.log", std::ios::out | std::ios::trunc);
				static std::mutex bakeLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ bakeLogMutex };
					bakeLog << entry.message << std::endl;
				});

				if (missingArgs(i, 1, "--bake-worldmap <world>"))
				{
					break;
				}

				// Exit code 1 when the bake failed, for scripted use.
				if (mainWindow.OpenAsset(args[i + 1]) && mmo::WorldEditorInstance::GetLastCreated())
				{
					PostQuitMessage(mmo::WorldEditorInstance::GetLastCreated()->GenerateWorldMap() ? 0 : 1);
				}
				else
				{
					ELOG("--bake-worldmap: failed to open " << args[i + 1]);
					PostQuitMessage(1);
				}
				break;
			}

			if (std::string(args[i]) == "--bake-minimaps")
			{
				// Unattended like --bake-terrain-lod: keep the log, route assertions into a file.
				static std::ofstream bakeLog("minimap_bake.log", std::ios::out | std::ios::trunc);
				static std::mutex bakeLogMutex;
				mmo::g_DefaultLog.signal().connect([](const mmo::LogEntry& entry)
				{
					std::scoped_lock lock{ bakeLogMutex };
					bakeLog << entry.message << std::endl;
				});
#ifdef _DEBUG
				static HANDLE assertFile = CreateFileA("minimap_bake_asserts.log", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
				_CrtSetReportFile(_CRT_ASSERT, assertFile);
				_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
				_CrtSetReportFile(_CRT_ERROR, assertFile);
#endif

				if (missingArgs(i, 1, "--bake-minimaps <world>"))
				{
					break;
				}

				if (mainWindow.OpenAsset(args[i + 1]) && mmo::WorldEditorInstance::GetLastCreated())
				{
					bool onlyStale = false;
					mmo::MinimapBaker::Region region;
					for (int j = 1; j < argCount; ++j)
					{
						onlyStale = onlyStale || std::string(args[j]) == "--only-stale";

						// --region minX,minZ,maxX,maxZ (inclusive page coordinates) rebakes just an area.
						if (std::string(args[j]) == "--region" && j + 1 < argCount)
						{
							unsigned minX = 0, minZ = 0, maxX = 0, maxZ = 0;
							if (std::sscanf(args[j + 1], "%u,%u,%u,%u", &minX, &minZ, &maxX, &maxZ) == 4)
							{
								region = mmo::MinimapBaker::Region{ minX, minZ, maxX, maxZ };
							}
							else
							{
								ELOG("--region expects minX,minZ,maxX,maxZ");
							}
						}
					}

					// Exit code 1 when any page failed, for scripted use.
					const bool baked = mmo::WorldEditorInstance::GetLastCreated()->GenerateMinimaps(onlyStale, region);
					PostQuitMessage(baked ? 0 : 1);
				}
				else
				{
					ELOG("--bake-minimaps: failed to open " << args[i + 1]);
					PostQuitMessage(1);
				}
				break;
			}
		}
		GlobalFree(args);
	}
#endif

#ifdef _WIN32
	// Run the message loop
	MSG msg = { nullptr };
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		ioService.poll_one();
		
		// Update audio system
		if (editorAudio)
		{
			editorAudio->Update(mmo::Vector3::Zero, 0.0f);
		}
	}
#endif

	// Cleanup audio system
	if (editorAudio)
	{
		editorAudio->Destroy();
	}

	dbService.stop();
	dbThread.join();

#ifdef _WIN32
	// PostQuitMessage's code, e.g. the --bake-terrain-lod result.
	return static_cast<int>(msg.wParam);
#else
	return 0;
#endif
}
