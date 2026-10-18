// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_editor_instance.h"
#include "selection_raycaster.h"

#include <DetourDebugDraw.h>
#include <imgui_internal.h>

#include <set>
#include "editor_host.h"
#include "icon_font.h"
#include "scene_graph/prop_kit_manager.h"
#include "scene_graph/prop_kit_serializer.h"
#include "selected_kit_prop.h"
#include "editors/world_model_editor/panels/world_model_properties_panel.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "binary_io/writer.h"
#include "math/matrix4.h"
#include "editor_windows/asset_picker_widget.h"
#include "environment_preview.h"
#include "world_editor.h"
#include "paging/world_page_loader.h"
#include "deferred_shading/color_grading.h"
#include "assets/asset_registry.h"
#include "editors/material_editor/node_editor/node_layout.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/world_model_manager.h"
#include "selected_map_entity.h"
#include "stream_sink.h"
#include "game/object_type_id.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "game_common/world_entity_loader.h"
#include "game_common/world_foliage.h"
#include "game_common/world_fog_volumes.h"
#include "deferred_shading/fog_volume_selection.h"
#include "math/sphere.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "scene_graph/instanced_foliage.h"
#include "scene_graph/foliage.h"
#include "scene_graph/foliage_layer.h"
#include "terrain/constants.h"
#include "nav_build/common.h"
#include "scene_graph/mesh_manager.h"
#include "terrain/page.h"
#include "terrain/tile.h"
#include "edit_modes/navigation_edit_mode.h"
#include "stream_sink.h"
#include "tex_v1_0/header_save.h"
#include "graphics/render_texture.h"

#include "stb_dxt.h"
#include "terrain_lod_baker.h"
#include "terrain/far_terrain.h"
#include "tex/png_writer.h"
#include "graphics/material_instance.h"
#include "base/clock.h"

namespace mmo
{
	namespace
	{
		/// Answers the water volume system's queries from the editor's terrain.
		class EditorTerrainWaterQuery final : public IWaterQuery
		{
		public:
			explicit EditorTerrainWaterQuery(const terrain::Terrain& terrain)
				: m_terrain(terrain)
			{
			}

			[[nodiscard]] bool HasWaterAt(const float x, const float z) const override
			{
				return m_terrain.HasWaterAtWorldPos(x, z);
			}

			[[nodiscard]] float GetWaterHeightAt(const float x, const float z) const override
			{
				return m_terrain.GetWaterHeightAtWorldPos(x, z);
			}

			[[nodiscard]] uint32 GetWaterTypeAt(const float x, const float z) const override
			{
				return static_cast<uint32>(m_terrain.GetWaterTypeAtWorldPos(x, z));
			}

		private:
			const terrain::Terrain& m_terrain;
		};

		/// Normalizes a kit asset path for cache keys and comparisons: registry paths use forward
		/// slashes; case is handled by the case-insensitive maps and KitPathsEqual.
		String MakeKitKey(String path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return path;
		}

		/// Whether two kit asset paths name the same kit (slash style and case ignored).
		bool KitPathsEqual(const String& a, const String& b)
		{
			const String keyA = MakeKitKey(a);
			const String keyB = MakeKitKey(b);
			return keyA.size() == keyB.size() && std::equal(keyA.begin(), keyA.end(), keyB.begin(), [](const char x, const char y)
			{
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		/// Builds the world transform of a kit prop placed on a building, composed the same way the
		/// world model instance realizes it (see ComposeWorldTransform).
		Matrix4 MakeKitPropWorldTransform(const SceneNode& placementNode, const WorldModelMeshRef& prop)
		{
			const Vector3 parentPosition = placementNode.GetDerivedPosition();
			const Quaternion parentOrientation = placementNode.GetDerivedOrientation();
			const Vector3 parentScale = placementNode.GetDerivedScale();

			Matrix4 transform;
			transform.MakeTransform(
				parentPosition + parentOrientation * (parentScale * prop.position),
				parentScale * prop.scale,
				parentOrientation * prop.rotation);
			return transform;
		}
	}
}

namespace mmo
{
	static const ChunkMagic versionChunk = MakeChunkMagic('MVER');
	static const ChunkMagic meshChunk = MakeChunkMagic('MESH');
	static const ChunkMagic entityChunk = MakeChunkMagic('MENT');
	static const ChunkMagic terrainChunk = MakeChunkMagic('RRET');

	static ChunkMagic WorldEntityVersionChunk = MakeChunkMagic('WVER');
	static ChunkMagic WorldEntityMesh = MakeChunkMagic('WMSH');

	struct MapEntityChunkContent
	{
		uint32 uniqueId;
		uint32 meshNameIndex;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale;
	};

	String MapEntity::GetAssetName() const
	{
		if (m_entityType == MapEntityType::WorldModel)
		{
			return m_assetName;
		}
		else if (m_entity && m_entity->GetMesh())
		{
			return String(m_entity->GetMesh()->GetName());
		}
		return String();
	}

	WorldEditorInstance::WorldEditorInstance(EditorHost &host, WorldEditor &editor, Path asset)
		: EditorInstance(host, std::move(asset)), m_editor(editor), m_wireFrame(false)
	{

		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-35.0f), Vector3::UnitX));

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_worldGrid->SetQueryFlags(SceneQueryFlags_None);
		m_worldGrid->SetVisible(false);

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &WorldEditorInstance::Render);

		// Ensure the work queue is always busy
		m_work = std::make_unique<asio::io_service::work>(m_workQueue);

		// Setup background loading thread
		auto &workQueue = m_workQueue;
		auto &dispatcher = m_dispatcher;
		m_backgroundLoader = std::thread([&workQueue]()
										 { workQueue.run(); });

		const auto addWork = [&workQueue](const WorldPageLoader::Work &work)
		{
			workQueue.post(work);
		};
		const auto synchronize = [&dispatcher](const WorldPageLoader::Work &work)
		{
			dispatcher.post(work);
		};

		const PagePosition pos = GetPagePositionFromCamera();
		m_visibleSection = std::make_unique<LoadedPageSection>(pos, 1, *this);
		m_pageLoader = std::make_unique<WorldPageLoader>(*m_visibleSection, addWork, synchronize);

		m_raySceneQuery = m_scene.CreateRayQuery(Ray(Vector3::Zero, Vector3::UnitZ));
		m_raySceneQuery->SetQueryMask(SceneQueryFlags_Entity);
		m_debugBoundingBox = m_scene.CreateManualRenderObject("__DebugAABB__");
		m_debugBoundingBox->SetCastShadows(false);
		m_scene.GetRootSceneNode().AttachObject(*m_debugBoundingBox);

		PagePosition worldSize(64, 64);
		m_memoryPointOfView = std::make_unique<PagePOVPartitioner>(
			worldSize,
			2,
			pos,
			*m_pageLoader);

		m_transformWidget = std::make_unique<TransformWidget>(m_selection, m_scene, *m_camera);
		m_transformWidget->SetTransformMode(TransformMode::Translate);
		m_transformWidget->copySelection += [this]()
		{
			if (m_selection.IsEmpty())
			{
				return;
			}

			// Copy selection
			for (const auto &selected : m_selection.GetSelectedObjects())
			{
				selected->Duplicate();
			}
		};

		// Let terrain pages resolve their surface material from the authored water profiles, so
		// the editor viewport shows the same water the client will. A page carrying an explicit
		// material name still overrides this.
		// The resolver is static and shared by every open world editor, so it captures the project,
		// which outlives them all, rather than this: with two worlds open, closing the one that
		// installed it last would otherwise leave streaming pages calling into a destroyed instance.
		terrain::Page::SetWaterMaterialResolver([&project = m_editor.GetProject()](const terrain::WaterType waterType) -> String
			{
				const auto* profile = project.waterProfiles.getById(static_cast<uint32>(waterType));
				if (profile == nullptr || !profile->has_surface_material())
				{
					return String();
				}

				return profile->surface_material();
			});

		// Setup terrain
		m_terrain = std::make_unique<terrain::Terrain>(m_scene, m_camera, 64, 64);
		m_terrain->SetTileSceneQueryFlags(SceneQueryFlags_Tile);
		m_terrain->SetWireframeMaterial(MaterialManager::Get().Load("Editor/Wireframe.hmat"));
		m_terrain->SetLodEnabled(false);

		// Underwater preview. Flying the camera below a water surface shows the same post-process a
		// diving player sees, resolved from the same water profiles, so tuning a profile needs no
		// client. Resolved every frame, so profile edits show up live.
		m_waterQuery = std::make_unique<EditorTerrainWaterQuery>(*m_terrain);
		m_waterVolume = std::make_unique<WaterVolumeSystem>(*m_waterQuery);
		m_waterVolume->SetProfileResolver([this](const uint32 waterType)
			{
				const auto* profile = m_editor.GetProject().waterProfiles.getById(waterType);
				return profile != nullptr ? ToWaterProfileValues(*profile) : WaterProfileValues();
			});

		// Replace all \ with /
		String baseFileName = (m_assetPath.parent_path() / m_assetPath.filename().replace_extension() / "Terrain").string();
		std::transform(baseFileName.begin(), baseFileName.end(), baseFileName.begin(), [](char c)
					   { return c == '\\' ? '/' : c; });
		m_terrain->SetBaseFileName(baseFileName);

		// Let the grid lie on the ground instead of cutting through it. The grid lives in
		// scene_graph, which the terrain is built on top of, so it cannot reach the terrain
		// itself -- this instance owns both and is the one place that can join them. Reading
		// m_terrain inside the call rather than capturing it keeps this correct even though the
		// grid was constructed first and m_hasTerrain is only known once the map has loaded.
		m_worldGrid->SetHeightProvider([this](const float x, const float z, float& outHeight)
			{
				return m_hasTerrain && m_terrain && m_terrain->TryGetSmoothHeightAt(x, z, outHeight);
			});

		// Authored foliage (trees) is rendered through hardware-instanced cells, exactly like the
		// game client, so the editor can preview the world WYSIWYG. Trees are authored map content
		// (placed via the foliage edit mode) and are therefore always visible.
		m_foliage = std::make_unique<InstancedFoliage>(m_scene, GraphicsDevice::Get(), static_cast<float>(terrain::constants::PageSize) / 4.0f);
		m_foliage->SetVisible(true);

		// Procedural grass mirrors the client's dense vegetation. It is controlled by the
		// "Show Foliage" world setting and hidden by default so it does not obstruct editing.
		SetupGrass();

		m_debugNode = m_scene.GetRootSceneNode().CreateChildSceneNode();
		m_debugEntity = m_scene.CreateEntity("TerrainDebug", "Editor/Joint.hmsh");
		m_debugNode->AttachObject(*m_debugEntity);
		m_debugEntity->SetVisible(false);

		// Setup sky component for day/night cycle control
		m_skyComponent = std::make_unique<SkyComponent>(m_scene);
		m_skyComponent->SetTimeSpeed(0.0f); // Start with time paused in editor
		m_environmentProfiles = std::make_unique<EnvironmentProfileCache<proto::EnvironmentProfileManager>>(m_editor.GetProject().environmentProfiles);

		// Setup edit modes
		m_terrainEditMode = std::make_unique<TerrainEditMode>(*this, *m_terrain, m_editor.GetProject().zones, *m_camera,
			m_editor.GetTerrainClipboard(), m_assetPath.string());
		m_entityEditMode = std::make_unique<EntityEditMode>(*this);

		// Create scene outline window
		m_sceneOutlineWindow = std::make_unique<SceneOutlineWindow>(m_selection, m_scene);

		// Create entity factory
		m_entityFactory = std::make_unique<EntityFactory>(m_scene, m_editor, m_mapEntities, m_sceneOutlineWindow.get());

		// Create details panel
		m_detailsPanel = std::make_unique<DetailsPanel>(
			m_selection,
			*this,
			[this]()
			{ Save(); });

		// Create world settings panel
		m_worldSettingsPanel = std::make_unique<WorldSettingsPanel>(
			*m_terrain,
			*m_worldGrid,
			m_hasTerrain,
			m_editMode,
			m_terrainEditMode.get(),
			[this](WorldEditMode *mode)
			{ SetEditMode(mode); },
			m_showFoliage,
			m_showWater,
			[this](bool visible)
			{
				if (m_grass)
				{
					m_grass->SetVisible(visible);
					if (visible)
					{
						m_grass->RebuildAll();
					}
				}
			},
			[this](bool)
			{ UpdateWaterVisibility(); });

		// Create spawn palette panel
		m_spawnPalettePanel = std::make_unique<SpawnPalettePanel>();

		// Create spawn list panel (dockable browser of all placed spawns)
		m_spawnListPanel = std::make_unique<SpawnListPanel>();

		// Initialize deferred renderer BEFORE viewport panel since viewport panel takes a reference to it
		m_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), m_scene, 640, 480);

		// Create viewport panel
		m_viewportPanel = std::make_unique<ViewportPanel>(
			*m_deferredRenderer,
			*m_worldGrid,
			*m_transformWidget,
			m_gridSnapSettings,
			m_selection,
			*m_sceneOutlineWindow,
			m_hovering,
			m_leftButtonPressed,
			m_rightButtonPressed,
			m_cameraSpeed,
			m_lastAvailViewportSize,
			m_lastContentRectMin,
			[this]()
			{ Render(); },
			[this]()
			{ GenerateMinimaps(); },
			[this]()
			{ GenerateTerrainLod(true); });

		// Load and set transform mode icons
		static auto translateIcon = TextureManager::Get().CreateOrRetrieve("Editor/translate.htex");
		static auto rotateIcon = TextureManager::Get().CreateOrRetrieve("Editor/rotate.htex");
		static auto scaleIcon = TextureManager::Get().CreateOrRetrieve("Editor/scale.htex");
		ViewportPanel::SetTransformIcons(translateIcon.get(), rotateIcon.get(), scaleIcon.get());

		// Set up delete callback
		m_sceneOutlineWindow->SetDeleteCallback([this](uint64 id)
												{
			// Find and remove the entity with this ID
			for (auto it = m_mapEntities.begin(); it != m_mapEntities.end(); ++it) {
				if ((*it)->GetUniqueId() == id) {
					(*it)->remove(*it->get());
					break;
				}
			}
			m_selection.Clear();
			m_sceneOutlineWindow->Update(); });

		// Set up rename callback
		m_sceneOutlineWindow->SetRenameCallback([this](uint64 id, const std::string &newName)
												{
			// Find the entity with this ID and rename it
			for (auto it = m_mapEntities.begin(); it != m_mapEntities.end(); ++it) {
				if ((*it)->GetUniqueId() == id) {
					(*it)->SetDisplayName(newName);
					break;
				}
			}
			m_sceneOutlineWindow->Update(); });

		// Set up category change callback
		m_sceneOutlineWindow->SetCategoryChangeCallback([this](uint64 id, const std::string &newCategory)
														{
			// Find the entity with this ID and change its category
			for (auto it = m_mapEntities.begin(); it != m_mapEntities.end(); ++it) {
				if ((*it)->GetUniqueId() == id) {
					(*it)->SetCategory(newCategory);
					break;
				}
			}
			m_sceneOutlineWindow->Update(); });

		// Supply a duplication callback factory so scene-outline selections can duplicate via Alt+transform.
		m_sceneOutlineWindow->SetEntityDuplicationFactory([this](MapEntity& entity) -> std::function<void(Selectable&)>
		{
			return MakeDuplicationCallback(entity);
		});

		m_spawnEditMode = std::make_unique<SpawnEditMode>(*this, m_editor.GetProject().maps, m_editor.GetProject().units, m_editor.GetProject().objects);
		m_skyEditMode = std::make_unique<SkyEditMode>(*this, *m_skyComponent);
		m_areaTriggerEditMode = std::make_unique<AreaTriggerEditMode>(*this, m_editor.GetProject().maps, m_editor.GetProject().areaTriggers);
		m_fogVolumeEditMode = std::make_unique<FogVolumeEditMode>(*this);
		m_atlasEditMode = std::make_unique<AtlasEditMode>(*this, std::filesystem::path(m_editor.GetProject().getLastPath()),
			[this]() -> const proto::MapEntry* { return m_spawnEditMode ? m_spawnEditMode->GetMapEntry() : nullptr; });
		m_waterEditMode = std::make_unique<WaterEditMode>(*this, *m_terrain, *m_camera);
		m_terrainEditMode->SetWaterEditMode(m_waterEditMode.get());

		// Brush-based authoring for the instanced foliage created above (m_foliage).
		m_foliageEditMode = std::make_unique<FoliageEditMode>(*this, *m_foliage, m_terrain.get(), *m_camera);

		m_editMode = nullptr;

		m_selectionRaycaster = std::make_unique<SelectionRaycaster>(
			*m_camera,
			*m_raySceneQuery,
			m_selection,
			*m_debugBoundingBox,
			nullptr, // terrain - will be set after terrain is created
			m_editor,
			m_spawnEditMode.get(),
			this); // spawn edit mode - will be set later

		// Update selection raycaster with terrain pointer
		m_selectionRaycaster->SetTerrain(m_terrain.get());

		// Add navigation edit mode
		m_navigationEditMode = std::make_unique<NavigationEditMode>(*this);

		// TODO: Load map file
		std::unique_ptr<std::istream> streamPtr = AssetRegistry::OpenFile(GetAssetPath().string());
		if (!streamPtr)
		{
			ELOG("Failed to load world file '" << GetAssetPath() << "'");
			return;
		}

		AddChunkHandler(*versionChunk, true, *this, &WorldEditorInstance::ReadMVERChunk);

		io::StreamSource source{*streamPtr};
		io::Reader reader{source};
		if (!Read(reader))
		{
			ELOG("Failed to read world file '" << GetAssetPath() << "'!");
			return;
		}

		ILOG("Successfully read world file!");

		// Local fog volumes are optional -- most maps have none, so a missing .hfog file is not an error.
		{
			Path fogPath = m_assetPath;
			fogPath.replace_extension(".hfog");
			String fogFileName = fogPath.string();
			std::transform(fogFileName.begin(), fogFileName.end(), fogFileName.begin(), [](char c) { return c == '\\' ? '/' : c; });

			if (std::unique_ptr<std::istream> fogStreamPtr = AssetRegistry::OpenFile(fogFileName))
			{
				io::StreamSource fogSource{*fogStreamPtr};
				io::Reader fogReader{fogSource};

				WorldFogVolumeDeserializer fogDeserializer{m_fogVolumes};
				if (!fogDeserializer.Read(fogReader))
				{
					WLOG("Failed to read fog volumes from '" << fogFileName << "', continuing without local fog volumes");
					m_fogVolumes.clear();
				}
			}
			DLOG("Loaded " << m_fogVolumes.size() << " local fog volume(s) for map " << fogFileName);
		}

		CreateFarTerrain();

		m_worldSettingsPanel->SetExtraContent([this]() { DrawViewSettings(); });

		s_lastCreated = this;
	}

	WorldEditorInstance::~WorldEditorInstance()
	{
		if (s_lastCreated == this)
		{
			s_lastCreated = nullptr;
		}

		// Stop background loading thread
		m_work.reset();
		m_workQueue.stop();
		m_dispatcher.stop();
		m_backgroundLoader.join();

		m_transformWidget.reset();
		m_kitCollisionOverlay.reset();
		m_mapEntities.clear();
		// Destroy entity/world-model instances before clearing the scene — their
		// destructors call DetachObject/DestroySceneNode which require live scene nodes.
		m_entityFactory.reset();
		// The foliage edit mode holds a reference to m_foliage and owns brush scene objects;
		// release it before the foliage it references and before Scene::Clear().
		m_foliageEditMode.reset();
		// Foliage owns scene nodes and render chunks; destroy it before Scene::Clear().
		m_foliage.reset();
		// Procedural grass also owns scene nodes and render chunks; destroy before Scene::Clear().
		m_grass.reset();
		// The distant terrain hangs its stand-ins below the terrain's scene node.
		m_farTerrain.reset();
		m_worldGrid.reset();
		// Destroy the terrain before clearing the scene. Terrain pages own water render
		// objects and page scene nodes that live in the scene; Scene::Clear() would destroy
		// those first, leaving the pages with dangling pointers that crash in Page::Unload()
		// when the terrain is finally destroyed as a member after this destructor body.
		m_terrain.reset();
		m_scene.Clear();
	}

	void WorldEditorInstance::Render()
	{
		m_dispatcher.poll();


		// Keep water visibility in sync with the active edit mode (water is forced visible while editing water).
		UpdateWaterVisibility();

		// Stream procedural grass chunks around the camera, mirroring the client (only when shown).
		if (m_grass && m_grass->IsVisible() && m_camera)
		{
			m_grass->Update(*m_camera);
		}

		const float deltaTimeSeconds = ImGui::GetCurrentContext()->IO.DeltaTime;

		// Never fire keyboard shortcuts while an ImGui text-input widget has keyboard focus.
		if (!ImGui::GetIO().WantTextInput)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_F))
			{
				FocusSelection();
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Z))
			{
				if (!m_selection.IsEmpty())
				{
					m_selection.GetSelectedObjects().back()->Translate(
						-m_selection.GetSelectedObjects().back()->GetPosition());
				}
				m_cameraVelocity = Vector3::Zero;
			}
		}

		if (m_leftButtonPressed || m_rightButtonPressed)
		{
			Vector3 direction = Vector3::Zero;
			if (ImGui::IsKeyDown(ImGuiKey_W))
			{
				direction.z = -1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_S))
			{
				direction.z = 1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_A))
			{
				direction.x = -1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_D))
			{
				direction.x = 1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_Q))
			{
				direction.y = -1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_E))
			{
				direction.y = 1.0f;
			}

			if (direction != Vector3::Zero)
			{
				m_cameraVelocity = direction.NormalizedCopy() * m_cameraSpeed;
			}
		}

		// Update sky component for day/night cycle
		if (m_skyComponent)
		{
			m_skyComponent->SetPosition(m_camera->GetDerivedPosition());
			m_skyComponent->Update(deltaTimeSeconds, 0);
			UpdateEnvironment(deltaTimeSeconds);
		}

		m_cameraAnchor->Translate(m_cameraVelocity * deltaTimeSeconds, TransformSpace::Local);
		m_cameraVelocity *= powf(0.025f, deltaTimeSeconds);

		// Terrain deformation
		if (m_hovering && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (m_editMode)
				m_editMode->OnMouseHold(deltaTimeSeconds);
		}
		else if (m_editMode)
		{
			// The button may still be held while the cursor is outside the viewport. Tell the
			// mode so it does not treat the re-entry point as the far end of a continuous drag.
			m_editMode->OnStrokeInterrupted();
		}

		const auto pos = GetPagePositionFromCamera();
		m_memoryPointOfView->UpdateCenter(pos);
		m_visibleSection->UpdateCenter(pos);

		UpdateFarTerrain();

		UpdateUnderwaterPreview(deltaTimeSeconds);

		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f)
			return;

		auto &gx = GraphicsDevice::Get();

		gx.Reset();

		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);
		m_camera->SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);
		m_camera->InvalidateView();
		m_deferredRenderer->Render(m_scene, *m_camera);
		m_transformWidget->Update(m_camera);
	}

	void WorldEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockspaceId = ImGui::GetID("##model_dockspace_");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();
		const String worldSettingsId = "World Settings##" + GetAssetPath().string();
		const String sceneOutlineId = "Scene Outline##" + GetAssetPath().string();
		const String spawnPaletteId = "Spawn Palette##" + GetAssetPath().string();
		const String spawnListId = "Spawns##" + GetAssetPath().string();

		HandleKeyboardShortcuts();

		// Pick up kits saved by the prop kit editor since the last frame.
		PollKitCache();

		WorldEditMode *availableModes[] = {
			m_entityEditMode.get(),
			m_foliageEditMode.get(),
			m_terrainEditMode.get(),
			m_spawnEditMode.get(),
			m_areaTriggerEditMode.get(),
			m_fogVolumeEditMode.get(),
			m_atlasEditMode.get(),
			m_navigationEditMode.get(),
			m_skyEditMode.get()};
		m_detailsPanel->Draw(
			detailsId,
			m_editMode,
			availableModes,
			std::size(availableModes),
			[this](WorldEditMode *mode)
			{ SetEditMode(mode); });

		m_worldSettingsPanel->Draw(worldSettingsId);
		m_spawnPalettePanel->Draw(spawnPaletteId, m_spawnEditMode.get());

		// The placed-spawn browser is only populated while spawn editing is the active mode, because
		// spawns are only instantiated in the scene during that mode (and selection needs those entities).
		m_spawnListPanel->Draw(spawnListId, (m_editMode == m_spawnEditMode.get()) ? m_spawnEditMode.get() : nullptr);

		m_viewportPanel->Draw(viewportId, m_editMode, m_spawnEditMode && m_editMode == m_spawnEditMode.get() && m_spawnEditMode->IsWaypointEditActive());
		DrawSceneOutlinePanel(sceneOutlineId);

		const String minimapId = "World Map##" + GetAssetPath().string();
		DrawMinimapPanel(minimapId);

		// Kit prop removals requested this frame (Delete key, details panel) are applied once the
		// selection is no longer being iterated.
		ApplyPendingKitPropRemovals();

		UpdateKitCollisionOverlay();

		DrawKitModals();

		if (m_initDockLayout)
		{
			InitializeDockLayout(dockspaceId, viewportId, detailsId, worldSettingsId, spawnPaletteId, spawnListId);
		}

		ImGui::PopID();
	}

	void WorldEditorInstance::HandleKeyboardShortcuts()
	{
		// Do not fire any editor shortcuts while an ImGui text-input widget (e.g. a
		// value field in the details panel) has keyboard focus — the user is typing.
		if (ImGui::GetIO().WantTextInput)
		{
			return;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_LeftAlt, false))
		{
			m_transformWidget->SetCopyMode(true);
		}

		if (ImGui::IsKeyPressed(ImGuiKey_1, false))
		{
			m_transformWidget->SetTransformMode(TransformMode::Translate);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_2, false))
		{
			m_transformWidget->SetTransformMode(TransformMode::Rotate);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_3, false))
		{
			m_transformWidget->SetTransformMode(TransformMode::Scale);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_4, false))
		{
			m_transformWidget->SetUseLocalTransform(!m_transformWidget->IsUsingLocalTransform());
		}

		// The terrain wireframe is a view toggle you reach for constantly while sculpting, and
		// digging it out of the world settings panel every time is the kind of friction that stops
		// you using it at all.
		if (ImGui::IsKeyPressed(ImGuiKey_G, false) && m_hasTerrain && m_terrain)
		{
			m_terrain->SetWireframeVisible(!m_terrain->IsWireframeVisible());
		}

		// Hotkeys to change active edit mode
		if (ImGui::IsKeyDown(ImGuiKey_F1))
		{
			SetEditMode(nullptr);
		}
		else if (ImGui::IsKeyDown(ImGuiKey_F2))
		{
			SetEditMode(m_entityEditMode.get());
		}
		else if (ImGui::IsKeyDown(ImGuiKey_F3) && m_hasTerrain)
		{
			SetEditMode(m_terrainEditMode.get());
		}
		else if (ImGui::IsKeyDown(ImGuiKey_F4))
		{
			SetEditMode(m_spawnEditMode.get());
		}
		else if (ImGui::IsKeyDown(ImGuiKey_F5))
		{
			SetEditMode(m_skyEditMode.get());
		}
		else if (ImGui::IsKeyDown(ImGuiKey_F6) && m_hasTerrain)
		{
			SetEditMode(m_terrainEditMode.get());
			m_terrainEditMode->SetTerrainEditType(TerrainEditType::Water);
		}

		// Patrol submode — Escape exits
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			if (m_editMode == m_spawnEditMode.get() && m_spawnEditMode->IsWaypointEditActive())
				m_spawnEditMode->SetWaypointEditActive(false);
		}
	}

	void WorldEditorInstance::InitializeDockLayout(ImGuiID dockspaceId, const String &viewportId, const String &detailsId, const String &worldSettingsId, const String &spawnPaletteId, const String &spawnListId)
	{
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar); // Add empty node
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

		auto mainId = dockspaceId;
		ImGuiID sideId;
		ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 400.0f / ImGui::GetMainViewport()->Size.x, &sideId, &mainId);

		// Split the side panel to create space for the scene outline above the details panel
		ImGuiID sideTopId;
		ImGui::DockBuilderSplitNode(sideId, ImGuiDir_Up, 0.3f, &sideTopId, &sideId);

		// Create the scene outline ID string
		const String sceneOutlineId = "Scene Outline##" + GetAssetPath().string();
		const String minimapId = "World Map##" + GetAssetPath().string();

		ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
		ImGui::DockBuilderDockWindow(sceneOutlineId.c_str(), sideTopId);
		// The minimap teleport panel shares the side dock region with the details/settings panels.
		ImGui::DockBuilderDockWindow(minimapId.c_str(), sideId);
		// The placed-spawn browser shares the top-side dock node with the Scene Outline, so they
		// become tabs in the same region.
		ImGui::DockBuilderDockWindow(spawnListId.c_str(), sideTopId);
		ImGui::DockBuilderDockWindow(detailsId.c_str(), sideId);
		ImGui::DockBuilderDockWindow(worldSettingsId.c_str(), sideId);
		ImGui::DockBuilderDockWindow(spawnPaletteId.c_str(), sideId);

		ImGui::DockBuilderFinish(dockspaceId);
		m_initDockLayout = false;

		auto *wnd = ImGui::FindWindowByName(viewportId.c_str());
		if (wnd)
		{
			ImGuiDockNode *node = wnd->DockNode;
			node->WantHiddenTabBarToggle = true;
		}
	}

	void WorldEditorInstance::OnMouseButtonDown(const uint32 button, const uint16 x, const uint16 y)
	{
		m_lastMouseX = x;
		m_lastMouseY = y;

		if (m_hovering)
		{
			const auto mousePos = ImGui::GetMousePos();
			m_transformWidget->OnMousePressed(button, (mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x, (mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);

			if (button == 0 && m_editMode)
			{
				m_editMode->OnMouseDown(
					(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
					(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
			}
		}
	}

	void WorldEditorInstance::OnMouseButtonUp(const uint32 button, const uint16 x, const uint16 y)
	{
		const bool widgetWasActive = m_transformWidget->IsActive();

		if (button == 0)
		{
			m_leftButtonPressed = false;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = false;
		}

		const auto mousePos = ImGui::GetMousePos();
		m_transformWidget->OnMouseReleased(button, (mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x, (mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);

		if (m_hovering && button == 0)
		{
			if (m_editMode)
			{
				m_editMode->OnMouseUp((mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x, (mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);

				// A deform stroke moves the very ground a draping grid is lying on, and the grid
				// bakes those heights into its geometry. Re-read them once the stroke ends rather
				// than during it: a rebuild samples the surface tens of thousands of times, which
				// is not something to do on every frame of a drag.
				if (m_editMode == m_terrainEditMode.get() && m_worldGrid->IsFollowingTerrain())
				{
					m_worldGrid->InvalidateHeights();
				}
			}

			// TODO: Move this into the edit modes handling of OnMouseUp
			if (m_editMode == m_entityEditMode.get())
			{
				if (!widgetWasActive && button == 0 && m_hovering)
				{
					const float viewportX = (mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x;
					const float viewportY = (mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y;
					const bool allowMultiSelect = ImGui::IsKeyPressed(ImGuiKey_LeftShift);

					// Inside a selected building, a click on one of its kit props selects that prop.
					if (!TrySelectKitProp(viewportX, viewportY, allowMultiSelect))
					{
						m_selectionRaycaster->PerformEntitySelection(viewportX, viewportY, allowMultiSelect);
					}
				}
			}
			else if (m_editMode == m_spawnEditMode.get())
			{
				if (!widgetWasActive && button == 0 && m_hovering)
				{
					// Skip spawn selection raycasting while waypoint editing is active;
					// clicks are consumed by the waypoint add / drag logic instead.
					if (!m_spawnEditMode->IsWaypointEditActive())
					{
						m_selectionRaycaster->PerformSpawnSelection(
							(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
							(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
					}
				}
			}
			else if (m_editMode == m_fogVolumeEditMode.get())
			{
				if (!widgetWasActive)
				{
					// Fog volumes are picked analytically against their oriented box / ellipsoid rather
					// than through the scene query: the query only knows the wireframe's world AABB,
					// which for a rotated volume covers a lot of empty space.
					const uint32 pickedId = m_fogVolumeEditMode->PickVolume(
						(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
						(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
					if (pickedId != 0)
					{
						SelectFogVolume(pickedId);
					}
					else
					{
						ClearSelection();
					}
				}
			}
			else if (m_editMode == m_terrainEditMode.get())
			{
				if (m_terrainEditMode->GetTerrainEditType() == TerrainEditType::Select)
				{
					m_selectionRaycaster->PerformTerrainSelection(
						(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
						(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
				}
			}
		}
	}

	void WorldEditorInstance::OnMouseMoved(const uint16 x, const uint16 y)
	{
		if (!m_transformWidget->IsActive())
		{
			const float deltaTimeSeconds = ImGui::GetCurrentContext()->IO.DeltaTime;

			// Calculate mouse move delta
			const int16 deltaX = static_cast<int16>(x) - m_lastMouseX;
			const int16 deltaY = static_cast<int16>(y) - m_lastMouseY;

			// Skip camera rotation when the spawn editor is actively dragging a waypoint.
			const bool isDraggingWaypoint = (m_editMode == m_spawnEditMode.get() && m_spawnEditMode->IsDraggingWaypoint()) ||
				(m_editMode == m_atlasEditMode.get() && m_atlasEditMode->IsDragging());

			// TODO: Move this into edit modes handling of OnMouseMoved
			if (m_rightButtonPressed || (m_leftButtonPressed && !isDraggingWaypoint && m_editMode != m_foliageEditMode.get() && (m_editMode != m_terrainEditMode.get() || (m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Deform &&
																										   m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Region &&
																										   m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Paint &&
																										   m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Area &&
																										   m_terrainEditMode->GetTerrainEditType() != TerrainEditType::VertexShading &&
																										   m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Holes &&
																										   m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Water))))
			{
				m_cameraAnchor->Yaw(-Degree(deltaX), TransformSpace::World);
				m_cameraAnchor->Pitch(-Degree(deltaY), TransformSpace::Local);
			}

			m_lastMouseX = x;
			m_lastMouseY = y;
		}

		const auto mousePos = ImGui::GetMousePos();
		m_transformWidget->OnMouseMoved(
			(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
			(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);

		if (m_editMode)
		{
			m_editMode->OnMouseMoved(
				(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
				(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
		}

		if (m_editMode == m_terrainEditMode.get())
		{
			if (m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Select)
			{
				OnTerrainMouseMoved(
					(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
					(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
			}
		}
	}

	bool WorldEditorInstance::Save()
	{
		// Build mesh name index map (only for mesh entities)
		std::map<String, uint32> entityNames;
		for (const auto &mapEntity : m_mapEntities)
		{
			if (mapEntity->IsMeshEntity() && mapEntity->GetEntity() && mapEntity->GetEntity()->GetMesh())
			{
				const auto meshName = String(mapEntity->GetEntity()->GetMesh()->GetName());
				if (entityNames.contains(meshName))
				{
					continue;
				}

				entityNames.emplace(meshName, entityNames.size());
			}
		}

		// Open file for writing
		std::unique_ptr<std::ostream> streamPtr = AssetRegistry::CreateNewFile(GetAssetPath().string());
		if (!streamPtr)
		{
			ELOG("Failed to save file '" << GetAssetPath() << "': Unable to open file for writing!");
			return false;
		}

		io::StreamSink sink{*streamPtr};
		io::Writer writer{sink};

		// Start writing file
		writer
			<< io::write<uint32>(*versionChunk)
			<< io::write<uint32>(sizeof(uint32))
			<< io::write<uint32>(3);

		uint32 meshSize = 0;
		std::vector<const String *> sortedNames(entityNames.size());
		for (auto &name : entityNames)
		{
			sortedNames[name.second] = &name.first;
			meshSize += name.first.size() + 1;
		}

		// Write terrain settings
		{
			String defaultMaterialName;
			if (m_terrain->GetDefaultMaterial())
			{
				defaultMaterialName = m_terrain->GetDefaultMaterial()->GetName();
			}

			ChunkWriter terrainWriter(terrainChunk, writer);
			writer
				<< io::write<uint8>(m_hasTerrain)
				<< io::write_dynamic_range<uint16>(defaultMaterialName);
			terrainWriter.Finish();
		}

		// Write mesh names
		writer
			<< io::write<uint32>(*meshChunk)
			<< io::write<uint32>(meshSize);
		for (const auto &name : sortedNames)
		{
			writer << io::write_range(*name) << io::write<uint8>(0);
		}
		sink.Flush();

		ILOG("Successfully saved world file " << GetAssetPath());

		struct MaterialOverride
		{
			uint8 materialIndex;
			String materialName;
		};

		// Delete all removed entities
		for (const auto &ent : m_deletedEntities)
		{
			String oldFilename = (m_assetPath.parent_path() / m_assetPath.filename().replace_extension() / "Entities" / std::to_string(ent.pageId) / std::to_string(ent.entityId)).string() + ".wobj";
			std::transform(oldFilename.begin(), oldFilename.end(), oldFilename.begin(), [](char c)
						   { return c == '\\' ? '/' : c; });
			if (!AssetRegistry::RemoveFile(oldFilename))
			{
				ELOG("Failed to delete file " << oldFilename);
			}
		}

		m_deletedEntities.clear();

		// Save paged object locations as well
		for (auto &ent : m_mapEntities)
		{
			// Skip unmodified entities
			if (!ent->IsModified())
			{
				continue;
			}

			// Determine page of entity based on position
			const auto pos = ent->GetSceneNode().GetDerivedPosition();
			const auto pagePos = PagePosition(
				static_cast<uint32>(floor(pos.x / terrain::constants::PageSize)) + 32,
				static_cast<uint32>(floor(pos.z / terrain::constants::PageSize)) + 32);

			// Ensure old ref file is deleted
			if (const auto refPos = ent->GetReferencePagePosition())
			{
				if (*refPos != pagePos)
				{
					String oldFilename = (m_assetPath.parent_path() / m_assetPath.filename().replace_extension() / "Entities" / std::to_string(BuildPageIndex(refPos->x(), refPos->y())) / std::to_string(ent->GetUniqueId())).string() + ".wobj";
					std::transform(oldFilename.begin(), oldFilename.end(), oldFilename.begin(), [](char c)
								   { return c == '\\' ? '/' : c; });
					AssetRegistry::RemoveFile(oldFilename);
				}
			}

			// Ensure we are using the new ref pose now
			ent->SetReferencePagePosition(pagePos);

			// Create new file
			String baseFileName = (m_assetPath.parent_path() / m_assetPath.filename().replace_extension() / "Entities" / std::to_string(BuildPageIndex(pagePos.x(), pagePos.y()))).string();
			std::transform(baseFileName.begin(), baseFileName.end(), baseFileName.begin(), [](char c)
						   { return c == '\\' ? '/' : c; });

			auto filePtr = AssetRegistry::CreateNewFile(baseFileName + "/" + std::to_string(ent->GetUniqueId()) + ".wobj");
			if (!filePtr)
			{
				ELOG("Failed to write file " << baseFileName + "/" + std::to_string(ent->GetUniqueId()) + ".wobj");
				continue;
			}

			io::StreamSink fileSink{*filePtr};
			io::Writer fileWriter{fileSink};

			const Vector3 position = ent->GetSceneNode().GetDerivedPosition();
			const Vector3 scale = ent->GetSceneNode().GetScale();
			const Quaternion rotation = ent->GetSceneNode().GetDerivedOrientation();

			if (ent->IsWorldModelEntity())
			{
				// Save world model entity (version chunk, WWMO and the optional kit list)
				WorldEntityLoader::MapEntity placement;
				placement.entityType = WorldEntityType::WorldModel;
				placement.meshName = ent->GetAssetName();
				placement.uniqueId = ent->GetUniqueId();
				placement.position = position;
				placement.rotation = rotation;
				placement.scale = scale;
				placement.name = ent->GetDisplayName();
				placement.category = ent->GetCategory();
				placement.kitPaths = ent->GetKitPaths();
				WriteWorldModelPlacement(fileWriter, placement);
			}
			else
			{
				// Version 3 supports world model entities
				{
					ChunkWriter versionWriter(WorldEntityVersionChunk, fileWriter);
					fileWriter << io::write<uint32>(3);
					versionWriter.Finish();
				}

				// Save mesh entity
				ChunkWriter chunkWriter(WorldEntityMesh, fileWriter);
				const String mesh = ent->GetEntity()->GetMesh()->GetName().data();
				fileWriter
					<< io::write<uint64>(ent->GetUniqueId())
					<< io::write_dynamic_range<uint16>(mesh)
					<< io::write<float>(position.x)
					<< io::write<float>(position.y)
					<< io::write<float>(position.z)
					<< io::write<float>(rotation.w)
					<< io::write<float>(rotation.x)
					<< io::write<float>(rotation.y)
					<< io::write<float>(rotation.z)
					<< io::write<float>(scale.x)
					<< io::write<float>(scale.y)
					<< io::write<float>(scale.z);

				// Collect material overrides
				std::vector<MaterialOverride> materialOverrides;
				for (uint8 i = 0; i < ent->GetEntity()->GetNumSubEntities(); ++i)
				{
					SubEntity *sub = ent->GetEntity()->GetSubEntity(i);
					ASSERT(sub);

					SubMesh &submesh = ent->GetEntity()->GetMesh()->GetSubMesh(i);
					if (const bool hasDifferentMaterial = sub->GetMaterial() && sub->GetMaterial() != submesh.GetMaterial())
					{
						materialOverrides.emplace_back(i, String(sub->GetMaterial()->GetName()));
					}
				}

				// Serialize material overrides
				fileWriter << io::write<uint8>(materialOverrides.size());
				for (auto &materialOverride : materialOverrides)
				{
					fileWriter
						<< io::write<uint8>(materialOverride.materialIndex)
						<< io::write_dynamic_range<uint16>(materialOverride.materialName);
				}

				// Write name and category
				fileWriter
					<< io::write_dynamic_range<uint8>(ent->GetDisplayName())
					<< io::write_dynamic_range<uint16>(ent->GetCategory());

				chunkWriter.Finish();
			}
		}

		// Save terrain
		for (uint32 x = 0; x < 64; ++x)
		{
			for (uint32 y = 0; y < 64; ++y)
			{
				terrain::Page *page = m_terrain->GetPage(x, y);
				if (page && page->IsPrepared() && page->IsChanged())
				{
					// Page was loaded, so save any changes
					page->Save();
				}
			}
		}

		// Save authored foliage. Each modified page is rewritten as a whole .hfol file (or deleted
		// when it no longer holds any instances).
		if (m_foliageEditMode && m_foliage)
		{
			for (const uint16 pageIndex : m_foliageEditMode->GetDirtyPages())
			{
				String fileName = (m_assetPath.parent_path() / m_assetPath.filename().replace_extension() / "Foliage" / (std::to_string(pageIndex) + ".hfol")).string();
				std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](char c) { return c == '\\' ? '/' : c; });

				std::vector<InstancedFoliageInstance> pageInstances;
				m_foliage->GetInstancesForPage(pageIndex, pageInstances);

				if (pageInstances.empty())
				{
					// Page emptied — remove any existing file.
					AssetRegistry::RemoveFile(fileName);
					continue;
				}

				std::vector<FoliageInstance> serialized;
				serialized.reserve(pageInstances.size());
				for (const auto& source : pageInstances)
				{
					FoliageInstance instance;
					instance.uniqueId = source.uniqueId;
					instance.meshName = source.meshName;
					instance.position = source.position;
					instance.rotation = source.rotation;
					instance.scale = source.scale;
					instance.collides = source.collides;
					serialized.push_back(instance);
				}

				auto filePtr = AssetRegistry::CreateNewFile(fileName);
				if (!filePtr)
				{
					ELOG("Failed to write foliage file " << fileName);
					continue;
				}

				io::StreamSink foliageSink{ *filePtr };
				io::Writer foliageWriter{ foliageSink };
				WorldFoliageSerializer::Write(foliageWriter, serialized);
				foliageSink.Flush();
			}

			m_foliageEditMode->ClearDirtyPages();
		}

		// Save local fog volumes. One file for the whole map, rewritten in full when dirty
		// (or deleted once the last volume is removed).
		if (m_fogVolumesDirty)
		{
			Path fogPath = m_assetPath;
			fogPath.replace_extension(".hfog");
			String fogFileName = fogPath.string();
			std::transform(fogFileName.begin(), fogFileName.end(), fogFileName.begin(), [](char c) { return c == '\\' ? '/' : c; });

			if (m_fogVolumes.empty())
			{
				if (AssetRegistry::HasFile(fogFileName))
				{
					AssetRegistry::RemoveFile(fogFileName);
				}

				m_fogVolumesDirty = false;
			}
			else
			{
				auto fogFilePtr = AssetRegistry::CreateNewFile(fogFileName);
				if (!fogFilePtr)
				{
					ELOG("Failed to write fog volume file " << fogFileName);
				}
				else
				{
					io::StreamSink fogSink{*fogFilePtr};
					io::Writer fogWriter{fogSink};
					WorldFogVolumeSerializer::Write(fogWriter, m_fogVolumes);
					fogSink.Flush();

					m_fogVolumesDirty = false;
				}
			}
		}

		// Write kits edited in place. Kits shared with other loaded buildings wait for confirmation.
		SaveDirtyKits();

		return true;
	}

	void WorldEditorInstance::OnTerrainMouseMoved(const float viewportX, const float viewportY)
	{
		// Long enough to span the world diagonal (64 pages of 533 units, so ~48k corner to
		// corner). The terrain raycast honours the ray's length, and a shallow pick from a far
		// camera — exactly when brush precision matters most — easily outruns a shorter ray.
		constexpr float pickRayLength = 50000.0f;

		const Ray ray = m_camera->GetCameraToViewportRay(viewportX, viewportY, pickRayLength);

		m_selection.Clear();
		m_debugBoundingBox->Clear();

		const auto hitResult = m_terrain->RayIntersects(ray);
		if (!hitResult.first)
		{
			m_debugEntity->SetVisible(false);
			return;
		}

		// TODO: Move this whole method into the terrain edit mode3
		m_terrainEditMode->SetBrushPosition(hitResult.second.position);
		if (m_terrainEditMode->GetTerrainEditType() == TerrainEditType::Water)
		{
			m_waterEditMode->SetBrushPosition(hitResult.second.position);
		}
		if (terrain::Tile *tile = hitResult.second.tile)
		{
			m_selectionRaycaster->UpdateDebugAABB(tile->GetWorldBoundingBox(true));
			m_debugBoundingBox->SetVisible(true);
		}
		else
		{
			m_debugBoundingBox->SetVisible(false);
		}

		m_debugNode->SetPosition(hitResult.second.position);
		// Debug entity (preview cube) intentionally hidden — brush circles & vertex dots handle visualization.
	}

	Entity *WorldEditorInstance::CreateMapEntity(const String &assetName, const Vector3 &position, const Quaternion &orientation, const Vector3 &scale, uint64 objectId)
	{
		Entity *entity = m_entityFactory->CreateMapEntity(assetName, position, orientation, scale, objectId);
		if (entity)
		{
			MapEntity *mapEntity = entity->GetUserObject<MapEntity>();
			if (mapEntity)
			{
				mapEntity->remove.connect(this, &WorldEditorInstance::OnMapEntityRemoved);
				mapEntity->MarkModified();
			}
		}
		return entity;
	}

	WorldModelInstance *WorldEditorInstance::CreateWorldModelEntity(const String &assetName, const Vector3 &position, const Quaternion &orientation, const Vector3 &scale, uint64 objectId)
	{
		WorldModelInstance *worldModelInstance = m_entityFactory->CreateWorldModelEntity(assetName, position, orientation, scale, objectId);
		if (worldModelInstance)
		{
			MapEntity *mapEntity = worldModelInstance->GetUserObject<MapEntity>();
			if (mapEntity)
			{
				mapEntity->remove.connect(this, &WorldEditorInstance::OnMapEntityRemoved);
				mapEntity->MarkModified();
			}
		}
		return worldModelInstance;
	}

	std::function<void(Selectable&)> WorldEditorInstance::MakeDuplicationCallback(MapEntity& entity)
	{
		const String assetName = entity.GetAssetName();
		if (entity.IsWorldModelEntity())
		{
			return [this, assetName](Selectable& sel)
			{
				CreateWorldModelEntity(assetName, sel.GetPosition(), sel.GetOrientation(), sel.GetScale(), 0);
			};
		}
		else
		{
			return [this, assetName](Selectable& sel)
			{
				CreateMapEntity(assetName, sel.GetPosition(), sel.GetOrientation(), sel.GetScale(), 0);
			};
		}
	}

	void WorldEditorInstance::OnMapEntityRemoved(MapEntity &entity)
	{
		// Capture WMO pointer before the erase destroys the MapEntity (and detaches the WMO).
		WorldModelInstance* wmoToRemove = entity.IsWorldModelEntity() ? entity.GetWorldModelInstance() : nullptr;

		std::erase_if(m_mapEntities, [&](const auto &mapEntity)
					  {
			if (mapEntity.get() == &entity)
			{
				const Vector3 pos = entity.GetSceneNode().GetDerivedPosition();
				const auto pagePos = PagePosition(
					static_cast<uint32>(floor(pos.x / terrain::constants::PageSize)) + 32,
					static_cast<uint32>(floor(pos.z / terrain::constants::PageSize)) + 32);

				// Ensure we delete the previous reference page pos
				if (entity.GetReferencePagePosition() && *entity.GetReferencePagePosition() != pagePos)
				{
					m_deletedEntities.emplace_back(entity.GetUniqueId(), BuildPageIndex(
						entity.GetReferencePagePosition()->x(),
						entity.GetReferencePagePosition()->y()));
				}

				// As well as the current one
				m_deletedEntities.emplace_back(entity.GetUniqueId(), BuildPageIndex(pagePos.x(), pagePos.y()));
				return true;
			}

			return false; });

		// Release WMO instance from factory ownership now that ~MapEntity has run Destroy() on it.
		if (wmoToRemove)
		{
			m_entityFactory->RemoveWorldModelInstance(wmoToRemove);
		}
	}

	PagePosition WorldEditorInstance::GetPagePositionFromCamera() const
	{
		const auto &camPos = m_camera->GetDerivedPosition();

		const auto pagePos = PagePosition(
			static_cast<uint32>(floor(camPos.x / terrain::constants::PageSize)) + 32,
			static_cast<uint32>(floor(camPos.z / terrain::constants::PageSize)) + 32);

		return pagePos;
	}

	void WorldEditorInstance::SetMapEntry(proto::MapEntry *entry)
	{
		if (m_mapEntry == entry)
		{
			return;
		}

		m_mapEntry = entry;

		// TODO: Update spawn placement objects

		// Okay so we build up a grid of references to unit spawns per tile so that we can only display
		// spawn objects which are relevant to the currently loaded pages and not simply ALL spawns that exist in total!
	}

	void WorldEditorInstance::UpdateEnvironment(const float deltaSeconds)
	{
		EnvironmentPreview& preview = GetEnvironmentPreview();

		// Any authored change reconverts profiles and snaps, so edits show up immediately.
		bool dataChanged = false;
		if (preview.revision != m_environmentRevision)
		{
			m_environmentRevision = preview.revision;
			m_environmentProfiles->Clear();
			dataChanged = true;
		}

		if (preview.profileId)
		{
			if (!m_environmentPreviewActive)
			{
				m_timeOfDayBeforePreview = m_skyComponent->GetNormalizedTimeOfDay();
			}

			m_skyComponent->SetNormalizedTimeOfDay(preview.normalizedTime);

			if (dataChanged || !m_environmentPreviewActive || m_environmentProfileId != *preview.profileId)
			{
				m_environmentProfileId = *preview.profileId;
				m_environment.SetTarget(m_environmentProfiles->Get(m_environmentProfileId), true);
			}

			m_environmentPreviewActive = true;
		}
		else
		{
			const bool leftPreview = m_environmentPreviewActive;
			m_environmentPreviewActive = false;

			if (leftPreview)
			{
				m_skyComponent->SetNormalizedTimeOfDay(m_timeOfDayBeforePreview);
			}

			const proto::MapEntry* map = m_spawnEditMode ? m_spawnEditMode->GetMapEntry() : nullptr;
			const proto::Project& project = m_editor.GetProject();

			// A map without terrain has no page to resolve an area from, so it never has a zone:
			// treat the lookup as having succeeded with zone 0 rather than never retargeting.
			uint32 zoneId = 0;
			const bool haveZone = !m_hasTerrain || (m_terrain && m_terrain->TryGetArea(m_cameraAnchor->GetDerivedPosition(), zoneId));

			if (haveZone)
			{
				const uint32 profileId = ResolveEnvironmentProfileId(project.zones, project.maps, zoneId, map ? map->id() : 0);

				if (dataChanged || leftPreview || profileId != m_environmentProfileId)
				{
					m_environmentProfileId = profileId;
					m_environment.SetTarget(m_environmentProfiles->Get(profileId), dataChanged || leftPreview);
				}
			}
			else if (dataChanged || leftPreview)
			{
				// The page under the camera is still streaming in: still retarget instead of
				// leaving the preview profile applied. Fall back to the map default when no
				// profile has been resolved for this map yet, otherwise keep the current one.
				const uint32 profileId = (m_environmentProfileId == UINT32_MAX)
					? ResolveEnvironmentProfileId(project.zones, project.maps, 0, map ? map->id() : 0)
					: m_environmentProfileId;

				m_environmentProfileId = profileId;
				m_environment.SetTarget(m_environmentProfiles->Get(profileId), true);
			}
		}

		m_environment.Update(deltaSeconds, m_skyComponent->GetNormalizedTimeOfDay());
		m_skyComponent->ApplyEnvironment(m_environment.GetState());

		m_wind.Update(deltaSeconds, m_environment.GetState());
		m_scene.SetWind(m_wind.GetState());
		PublishWindShaderParameters(m_wind.GetState());

		if (DeferredRenderer* renderer = GetDeferredRenderer())
		{
			const EnvironmentState& state = m_environment.GetState();
			renderer->SetExposure(state.exposure);
			renderer->SetBloomIntensity(state.bloomIntensity);
			renderer->SetBloomThreshold(state.bloomThreshold);
			renderer->SetFogLightScattering(state.lightScattering);

			ColorGradingSettings grading;
			grading.SetSaturation(state.saturation);
			grading.SetContrast(state.contrast);
			grading.SetColorFilter(state.colorFilter);
			renderer->SetColorGrading(grading, state.colorLut, state.colorLutFrom, state.colorLutBlend);

			// Selected fresh every frame (and pushed even when empty) so a map without any fog
			// volumes -- or one whose volumes just faded out -- doesn't keep rendering the last
			// frame's set.
			if (m_camera)
			{
				const float hour = m_skyComponent->GetNormalizedTimeOfDay() * 24.0f;
				renderer->SetFogVolumes(SelectFogVolumes(m_fogVolumes, hour, m_camera->GetDerivedPosition(), renderer->GetVolumetricFogRange(),
					[this](const Vector3& center, const float radius) { return m_camera->IsVisible(Sphere(center, radius)); }));
			}
			else
			{
				renderer->SetFogVolumes({});
			}
		}
	}

	void WorldEditorInstance::AddUnitSpawn(proto::UnitSpawnEntry &spawn, bool select)
	{
		const auto *unit = m_editor.GetProject().units.getById(spawn.unitentry());
		if (!unit)
		{
			WLOG("Spawn point of non-existant unit " << spawn.unitentry() << " found");
			return;
		}
		ASSERT(unit);

		// Load the model
		const auto *model = m_editor.GetProject().models.getById(unit->malemodel() ? unit->malemodel() : unit->femalemodel());
		if (!model)
		{
			WLOG("Spawn has no model");
			return;
		}

		String meshFile;
		if (model->flags() & model_data_flags::IsCustomizable)
		{
			auto definition = AvatarDefinitionManager::Get().Load(model->filename());
			if (!definition)
			{
				ELOG("Unable to load avatar definition " << model->filename());
			}
			else
			{
				meshFile = definition->GetBaseMesh();

				// TODO: Apply customization
			}
		}
		else
		{
			meshFile = model->filename();
		}

		const uint32 guid = m_entityFactory->GenerateUnitSpawnId();
		Entity *entity = m_scene.CreateEntity("Spawn_" + std::to_string(guid), meshFile);
		ASSERT(entity);

		ASSERT(entity->GetMesh());
		entity->SetUserObject(&spawn);
		entity->SetQueryFlags(SceneQueryFlags_UnitSpawns);
		m_spawnEntities.push_back(entity);

		SceneNode *node = m_scene.GetRootSceneNode().CreateChildSceneNode(Vector3(spawn.positionx(), spawn.positiony(), spawn.positionz()));
		node->SetOrientation(Quaternion(Radian(spawn.rotation()), Vector3::UnitY));
		if (unit->scale() != 0.0f)
		{
			node->SetScale(Vector3::UnitScale * unit->scale());
		}

		Quaternion rotationOffset;
		rotationOffset.FromAngleAxis(Degree(90), Vector3::UnitY);
		SceneNode *entityOffsetNode = node->CreateChildSceneNode(Vector3::Zero, rotationOffset);
		entityOffsetNode->AttachObject(*entity);
		m_spawnNodes.push_back(entityOffsetNode);
		m_spawnNodes.push_back(node);
	}

	void WorldEditorInstance::AddObjectSpawn(proto::ObjectSpawnEntry &spawn)
	{
		const auto *object = m_editor.GetProject().objects.getById(spawn.objectentry());
		if (!object)
		{
			WLOG("Spawn point of non-existant unit " << spawn.objectentry() << " found");
			return;
		}
		ASSERT(object);

		// Load the model
		const auto *model = m_editor.GetProject().objectDisplays.getById(object->displayid());
		if (!model)
		{
			WLOG("ObjectSpawn has no model");
			return;
		}

		const uint32 guid = m_entityFactory->GenerateObjectSpawnId();
		Entity *entity = m_scene.CreateEntity("ObjectSpawn_" + std::to_string(guid), model->filename());
		ASSERT(entity);

		ASSERT(entity->GetMesh());
		entity->SetUserObject(&spawn);
		entity->SetQueryFlags(SceneQueryFlags_ObjectSpawns);
		m_spawnEntities.push_back(entity);

		SceneNode *node = m_scene.GetRootSceneNode().CreateChildSceneNode(Vector3(spawn.location().positionx(), spawn.location().positiony(), spawn.location().positionz()));
		node->SetOrientation(Quaternion(spawn.location().rotationw(), spawn.location().rotationx(), spawn.location().rotationy(), spawn.location().rotationz()));
		node->SetScale(Vector3::UnitScale);

		Quaternion rotationOffset;
		rotationOffset.FromAngleAxis(Degree(90), Vector3::UnitY);
		SceneNode *entityOffsetNode = node->CreateChildSceneNode(Vector3::Zero, rotationOffset);
		entityOffsetNode->AttachObject(*entity);
		m_spawnNodes.push_back(entityOffsetNode);
		m_spawnNodes.push_back(node);
	}

	void WorldEditorInstance::RemoveUnitSpawn(const proto::UnitSpawnEntry& spawn)
	{
		auto* map = m_spawnEditMode->GetMapEntry();
		if (map)
		{
			// Try to find the entity
			const auto entityIt = std::find_if(m_spawnEntities.begin(), m_spawnEntities.end(), [&spawn](const Entity* entity)
			{
				return entity->GetUserObject<const proto::UnitSpawnEntry>() == &spawn;
			});

			if (entityIt != m_spawnEntities.end())
			{
				Entity* entity = *entityIt;

				// Remove associated scene nodes
				const auto nodeIt = std::find_if(m_spawnNodes.begin(), m_spawnNodes.end(), [entity](const SceneNode* node)
				{
						return node == entity->GetParentSceneNode();
				});

				if (nodeIt != m_spawnNodes.end())
				{
					m_scene.DestroySceneNode(**nodeIt);
					m_spawnNodes.erase(nodeIt);
				}

				// Remove the entity
				m_scene.DestroyEntity(*entity);
				m_spawnEntities.erase(entityIt);
			}

			const auto it = std::find_if(map->mutable_unitspawns()->begin(), map->mutable_unitspawns()->end(), [&spawn](const proto::UnitSpawnEntry& entry)
				{
					return &entry == &spawn;
				});
			if (it != map->mutable_unitspawns()->end())
			{
				map->mutable_unitspawns()->erase(it);
			}
		}
	}

	void WorldEditorInstance::RemoveObjectSpawn(const proto::ObjectSpawnEntry& spawn)
	{
		auto* map = m_spawnEditMode->GetMapEntry();
		if (map)
		{
			// Try to find the entity
			const auto entityIt = std::find_if(m_spawnEntities.begin(), m_spawnEntities.end(), [&spawn](const Entity* entity)
				{
					return entity->GetUserObject<const proto::ObjectSpawnEntry>() == &spawn;
				});

			if (entityIt != m_spawnEntities.end())
			{
				Entity* entity = *entityIt;

				// Remove associated scene nodes
				const auto nodeIt = std::find_if(m_spawnNodes.begin(), m_spawnNodes.end(), [entity](const SceneNode* node)
					{
						return node == entity->GetParentSceneNode();
					});

				if (nodeIt != m_spawnNodes.end())
				{
					m_scene.DestroySceneNode(**nodeIt);
					m_spawnNodes.erase(nodeIt);
				}

				// Remove the entity
				m_scene.DestroyEntity(*entity);
				m_spawnEntities.erase(entityIt);
			}

			const auto it = std::find_if(map->mutable_objectspawns()->begin(), map->mutable_objectspawns()->end(), [&spawn](const proto::ObjectSpawnEntry& entry)
				{
					return &entry == &spawn;
				});
			if (it != map->mutable_objectspawns()->end())
			{
				map->mutable_objectspawns()->erase(it);
			}
		}
	}

	void WorldEditorInstance::SelectUnitSpawn(proto::UnitSpawnEntry& spawn)
	{
		// Find the scene entity that represents this spawn.
		const auto entityIt = std::find_if(m_spawnEntities.begin(), m_spawnEntities.end(), [&spawn](const Entity* entity)
		{
			return entity->GetUserObject<const proto::UnitSpawnEntry>() == &spawn;
		});

		if (entityIt == m_spawnEntities.end())
		{
			WLOG("Unable to select unit spawn: no scene entity found");
			return;
		}

		Entity* entity = *entityIt;

		m_selection.Clear();
		m_debugBoundingBox->Clear();

		auto removalCallback = [this](const proto::UnitSpawnEntry& s)
		{
			RemoveUnitSpawn(s);
		};

		m_selection.AddSelectable(std::make_unique<SelectedUnitSpawn>(
			spawn,
			m_editor.GetProject().units,
			m_editor.GetProject().models,
			*entity->GetParentSceneNode()->GetParentSceneNode(),
			*entity,
			nullptr,
			removalCallback));

		m_selectionRaycaster->UpdateDebugAABB(entity->GetWorldBoundingBox());
	}

	void WorldEditorInstance::SelectObjectSpawn(proto::ObjectSpawnEntry& spawn)
	{
		// Find the scene entity that represents this spawn.
		const auto entityIt = std::find_if(m_spawnEntities.begin(), m_spawnEntities.end(), [&spawn](const Entity* entity)
		{
			return entity->GetUserObject<const proto::ObjectSpawnEntry>() == &spawn;
		});

		if (entityIt == m_spawnEntities.end())
		{
			WLOG("Unable to select object spawn: no scene entity found");
			return;
		}

		Entity* entity = *entityIt;

		m_selection.Clear();
		m_debugBoundingBox->Clear();

		auto removalCallback = [this](const proto::ObjectSpawnEntry& s)
		{
			RemoveObjectSpawn(s);
		};

		m_selection.AddSelectable(std::make_unique<SelectedObjectSpawn>(
			spawn,
			m_editor.GetProject().objects,
			m_editor.GetProject().objectDisplays,
			*entity->GetParentSceneNode()->GetParentSceneNode(),
			*entity,
			nullptr,
			removalCallback));

		m_selectionRaycaster->UpdateDebugAABB(entity->GetWorldBoundingBox());
	}

	const void* WorldEditorInstance::GetSelectedSpawnEntry() const
	{
		if (m_selection.IsEmpty())
		{
			return nullptr;
		}

		// Walk the active selection and extract the proto entry of a selected spawn (if any).
		struct SpawnEntryExtractor final : SelectableVisitor
		{
			const void* entry = nullptr;

			void Visit(SelectedMapEntity&) override {}
			void Visit(SelectedKitProp&) override {}
			void Visit(SelectedTerrainTile&) override {}
			void Visit(SelectedUnitSpawn& selectable) override { entry = &selectable.GetEntry(); }
			void Visit(SelectedObjectSpawn& selectable) override { entry = &selectable.GetEntry(); }
			void Visit(SelectedAreaTrigger&) override {}
			void Visit(SelectedFogVolume&) override {}
		} extractor;

		m_selection.GetSelectedObjects().back()->Visit(extractor);
		return extractor.entry;
	}

	void WorldEditorInstance::FocusWorldPosition(const Vector3& position)
	{
		m_cameraAnchor->SetPosition(position);
		m_cameraVelocity = Vector3::Zero;
	}

	void WorldEditorInstance::FocusSelection()
	{
		if (m_selection.IsEmpty())
		{
			m_cameraAnchor->SetPosition(Vector3::Zero);
		}
		else
		{
			m_cameraAnchor->SetPosition(m_selection.GetSelectedObjects().back()->GetPosition());
		}
		m_cameraVelocity = Vector3::Zero;
	}

	void WorldEditorInstance::MoveCameraToWorldPosition(const float worldX, const float worldZ)
	{
		// Sample the terrain height at the target location so the camera anchor sits on the ground,
		// mirroring the behaviour of FocusSelection. Fall back to 0 when the terrain isn't available
		// (e.g. the page hasn't streamed in yet) — the page loader will react to the new camera
		// position regardless.
		float height = 0.0f;
		if (m_terrain && m_hasTerrain)
		{
			height = m_terrain->GetSmoothHeightAt(worldX, worldZ);
		}

		m_cameraAnchor->SetPosition(Vector3(worldX, height, worldZ));
		m_cameraVelocity = Vector3::Zero;
	}

	EditorViewpoint WorldEditorInstance::GetViewpoint() const
	{
		EditorViewpoint viewpoint;
		viewpoint.position = m_camera->GetDerivedPosition();

		const Vector3 forward = m_cameraAnchor->GetDerivedOrientation() * Vector3::NegativeUnitZ;
		viewpoint.yawDegrees = Radian(std::atan2(-forward.x, -forward.z)).GetValueDegrees();
		if (viewpoint.yawDegrees < 0.0f)
		{
			viewpoint.yawDegrees += 360.0f;
		}
		viewpoint.pitchDegrees = Radian(std::asin(std::clamp(forward.y, -1.0f, 1.0f))).GetValueDegrees();
		viewpoint.fovDegrees = m_camera->GetFOVy().GetValueDegrees();
		if (m_skyComponent)
		{
			viewpoint.timeOfDay = m_skyComponent->GetNormalizedTimeOfDay();
		}
		return viewpoint;
	}

	void WorldEditorInstance::SetViewpoint(const EditorViewpoint& viewpoint)
	{
		// Yaw about the world up axis, then pitch about the rotated right axis: the same order the
		// mouse look uses, so dragging afterwards continues smoothly.
		const Quaternion orientation =
			Quaternion(Degree(viewpoint.yawDegrees), Vector3::UnitY) *
			Quaternion(Degree(std::clamp(viewpoint.pitchDegrees, -89.0f, 89.0f)), Vector3::UnitX);

		m_cameraAnchor->SetOrientation(orientation);

		// The camera hangs behind the anchor; place the anchor so the eye lands on the position.
		m_cameraAnchor->SetPosition(viewpoint.position - orientation * m_cameraNode->GetPosition());
		m_cameraVelocity = Vector3::Zero;

		m_camera->SetFOVy(Degree(std::clamp(viewpoint.fovDegrees, 5.0f, 150.0f)));

		// Derived transforms are refreshed lazily during rendering; page streaming reads the camera's
		// derived position before the next frame, so bring it up to date now.
		m_cameraAnchor->Update(true, true);
		m_camera->InvalidateView();

		if (viewpoint.timeOfDay >= 0.0f && m_skyComponent)
		{
			m_skyComponent->SetNormalizedTimeOfDay(std::fmod(viewpoint.timeOfDay, 1.0f));
		}
	}

	void WorldEditorInstance::CreateFarTerrain()
	{
		m_farTerrain.reset();

		if (!m_terrain || !m_hasTerrain)
		{
			return;
		}

		// Same material setup as the client's WorldState::CreateFarTerrain, so the distance looks alike.
		const MaterialPtr parent = MaterialManager::Get().Load("Models/AlbedoNormal_Opaque_Base.hmat");
		if (!parent)
		{
			WLOG("Distant terrain disabled: its material could not be loaded");
			return;
		}

		auto material = std::make_shared<MaterialInstance>("EditorFarTerrain", parent);
		material->SetTextureParameter("Normal", "Textures/Character/BaseFlattenNormalMap.htex");
		material->SetScalarParameter("Specular", 0.2f);
		material->SetScalarParameter("Roughness", 1.0f);

		auto& workQueue = m_workQueue;
		auto& dispatcher = m_dispatcher;
		m_farTerrain = std::make_unique<terrain::FarTerrain>(
			*m_terrain,
			std::move(material),
			[&workQueue](std::function<void()> work) { workQueue.post(std::move(work)); },
			[&dispatcher](std::function<void()> work) { dispatcher.post(std::move(work)); });
	}

	void WorldEditorInstance::UpdateFarTerrain()
	{
		if (!m_farTerrain && m_hasTerrain)
		{
			CreateFarTerrain();
		}

		const uint32 radius = m_hasTerrain ? m_farTerrainRadius : 0;
		if (m_farTerrain)
		{
			if (m_farTerrain->GetRadius() != radius)
			{
				m_farTerrain->SetRadius(radius);
			}

			m_farTerrain->Update(m_camera->GetDerivedPosition());
		}

		// Far clip as in the client: half a page beyond the distant selection while it has pages.
		constexpr float defaultFarClip = 1000.0f;
		const bool hasDistantTerrain = m_farTerrain && radius > 0 && m_farTerrain->GetReadyPageCount() > 0;
		const float farClip = hasDistantTerrain
			? std::max(defaultFarClip, (static_cast<float>(radius) + 1.5f) * static_cast<float>(terrain::constants::PageSize))
			: defaultFarClip;

		if (m_camera->GetFarClipDistance() != farClip)
		{
			m_camera->SetFarClipDistance(farClip);
		}
	}

	bool WorldEditorInstance::WaitForStreaming(const float timeoutSeconds)
	{
		const GameTime deadline = GetAsyncTimeMs() + static_cast<GameTime>(timeoutSeconds * 1000.0f);

		while (GetAsyncTimeMs() < deadline)
		{
			const PagePosition center = GetPagePositionFromCamera();
			m_memoryPointOfView->UpdateCenter(center);
			m_visibleSection->UpdateCenter(center);
			UpdateFarTerrain();

			while (m_dispatcher.poll_one() > 0)
			{
			}

			bool pagesLoaded = true;
			if (m_hasTerrain && m_terrain)
			{
				for (int32 z = static_cast<int32>(center.y()) - 1; z <= static_cast<int32>(center.y()) + 1 && pagesLoaded; ++z)
				{
					for (int32 x = static_cast<int32>(center.x()) - 1; x <= static_cast<int32>(center.x()) + 1; ++x)
					{
						if (x < 0 || z < 0 || x >= 64 || z >= 64)
						{
							continue;
						}

						// Every page with a tile file must finish loading; pages that have not even
						// started streaming yet count as not loaded.
						terrain::Page* page = m_terrain->GetPage(x, z);
						if (page && !page->IsLoaded() && AssetRegistry::HasFile(m_terrain->GetPageFilename(x, z)))
						{
							pagesLoaded = false;
							break;
						}
					}
				}
			}

			const bool farLoaded = !m_farTerrain || m_farTerrain->GetRadius() == 0 || m_farTerrain->GetLoadingPageCount() == 0;

			if (pagesLoaded && farLoaded)
			{
				ILOG("Streaming ready around page " << center.x() << "," << center.y() << " ("
					<< (m_farTerrain ? m_farTerrain->GetReadyPageCount() : 0) << " distant pages)");
				return true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		const PagePosition center = GetPagePositionFromCamera();
		for (int32 z = static_cast<int32>(center.y()) - 1; z <= static_cast<int32>(center.y()) + 1; ++z)
		{
			for (int32 x = static_cast<int32>(center.x()) - 1; x <= static_cast<int32>(center.x()) + 1; ++x)
			{
				if (terrain::Page* page = m_terrain ? m_terrain->GetPage(x, z) : nullptr)
				{
					if (!page->IsLoaded())
					{
						WLOG("Page " << x << "," << z << " not loaded (prepared " << page->IsPrepared() << ", file "
							<< AssetRegistry::HasFile(m_terrain->GetPageFilename(x, z)) << ")");
					}
				}
			}
		}
		WLOG("Streaming did not finish within " << timeoutSeconds << " seconds; capturing what is loaded");
		return false;
	}

	bool WorldEditorInstance::CaptureViewpoint(const EditorViewpoint& viewpoint, const uint32 width, const uint32 height,
		const std::filesystem::path& path, const bool showGrass, const uint32 warmupFrames)
	{
		if (width == 0 || height == 0 || width > 8192 || height > 8192)
		{
			ELOG("Capture size " << width << "x" << height << " is not supported");
			return false;
		}

		const EditorViewpoint previous = GetViewpoint();
		SetViewpoint(viewpoint);
		WaitForStreaming(120.0f);

		// Editor helpers do not belong in the picture.
		const bool gridWasVisible = m_worldGrid && m_worldGrid->IsVisible();
		if (m_worldGrid) m_worldGrid->SetVisible(false);
		const bool debugBoxWasVisible = m_debugBoundingBox && m_debugBoundingBox->IsVisible();
		if (m_debugBoundingBox) m_debugBoundingBox->SetVisible(false);

		const bool grassWasVisible = m_grass && m_grass->IsVisible();
		if (m_grass && showGrass != grassWasVisible)
		{
			m_grass->SetVisible(showGrass);
			if (showGrass)
			{
				m_grass->RebuildAll();
			}
		}

		auto& gx = GraphicsDevice::Get();
		m_deferredRenderer->Resize(width, height);
		m_camera->SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));

		// Exposure adaptation, temporal shadows and the volumetric fog history need a few frames to
		// settle; the last of these frames is the one that is saved.
		constexpr float frameTime = 1.0f / 30.0f;
		for (uint32 frame = 0; frame <= warmupFrames; ++frame)
		{
			while (m_dispatcher.poll_one() > 0)
			{
			}

			UpdateFarTerrain();
			if (m_grass && m_grass->IsVisible())
			{
				m_grass->Update(*m_camera);
			}
			if (m_skyComponent)
			{
				m_skyComponent->SetPosition(m_camera->GetDerivedPosition());
				m_skyComponent->Update(frameTime, 0);
				UpdateEnvironment(frameTime);
			}

			gx.Reset();
			gx.SetViewport(0, 0, width, height, 0.0f, 1.0f);
			m_camera->SetFillMode(FillMode::Solid);
			m_camera->InvalidateView();
			m_deferredRenderer->Render(m_scene, *m_camera);
		}

		bool written = false;
		const auto target = std::dynamic_pointer_cast<RenderTexture>(m_deferredRenderer->GetFinalRenderTarget());
		if (!target || target->GetColorFormat() != PixelFormat::R16G16B16A16)
		{
			ELOG("Capture: unexpected final render target");
		}
		else
		{
			gx.FlushCommands();

			const uint32 pixelCount = width * height;
			std::vector<uint16> halfPixels(static_cast<size_t>(pixelCount) * 4);
			if (target->GetPixelDataSize() != halfPixels.size() * sizeof(uint16))
			{
				ELOG("Capture: render target size does not match " << width << "x" << height);
			}
			else
			{
				target->CopyPixelDataTo(reinterpret_cast<uint8*>(halfPixels.data()));

				// The tonemap output is already display-referred (gamma applied), so only quantize.
				std::vector<uint8> rgba(static_cast<size_t>(pixelCount) * 4);
				for (size_t i = 0; i < halfPixels.size(); ++i)
				{
					const float value = (i % 4 == 3) ? 1.0f : HalfToFloat(halfPixels[i]);
					rgba[i] = static_cast<uint8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
				}

				written = tex::WritePng(path, width, height, rgba.data());
				if (written)
				{
					ILOG("Captured " << width << "x" << height << " view " << viewpoint.ToString() << " to " << path.string());
				}
				else
				{
					ELOG("Capture: failed to write " << path.string());
				}
			}
		}

		// Back to the interactive state.
		if (m_grass && m_grass->IsVisible() != grassWasVisible)
		{
			m_grass->SetVisible(grassWasVisible);
		}
		if (m_worldGrid) m_worldGrid->SetVisible(gridWasVisible);
		if (m_debugBoundingBox) m_debugBoundingBox->SetVisible(debugBoxWasVisible);

		if (m_lastAvailViewportSize.x > 0.0f && m_lastAvailViewportSize.y > 0.0f)
		{
			m_deferredRenderer->Resize(static_cast<uint32>(m_lastAvailViewportSize.x), static_cast<uint32>(m_lastAvailViewportSize.y));
		}

		SetViewpoint(previous);
		return written;
	}

	void WorldEditorInstance::DrawViewSettings()
	{
		if (!ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		int radius = static_cast<int>(m_farTerrainRadius);
		if (ImGui::SliderInt("Distant Terrain", &radius, 0, 16, radius == 0 ? "Off" : "%d pages"))
		{
			SetFarTerrainRadius(static_cast<uint32>(radius));
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Draws the baked low-detail terrain (.tlod) beyond the streamed pages, like the "
				"client's TerrainFarRadius (default 4). Bake it with Generate Terrain LOD after sculpting.");
		}
		if (m_farTerrain && m_farTerrainRadius > 0)
		{
			ImGui::TextDisabled("%u of %u distant pages ready", m_farTerrain->GetReadyPageCount(), m_farTerrain->GetVisiblePageCount());
		}

		float fov = m_camera->GetFOVy().GetValueDegrees();
		if (ImGui::SliderFloat("Field of View", &fov, 10.0f, 120.0f, "%.0f deg"))
		{
			m_camera->SetFOVy(Degree(fov));
			m_camera->InvalidateView();
		}

		ImGui::Separator();

		const EditorViewpoint current = GetViewpoint();
		const String currentText = current.ToString();
		ImGui::TextWrapped("Viewpoint: %s", currentText.c_str());
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("x, y, z, yaw, pitch, field of view, time of day. Yaw 0 looks north, 90 west.\n"
				"mmo_edit --capture-view <world.hwld> --view \"<viewpoint>\" --out shot.png takes the same picture unattended.");
		}

		if (ImGui::Button("Copy Viewpoint"))
		{
			ImGui::SetClipboardText(currentText.c_str());
			m_viewpointText = currentText;
		}

		ImGui::InputText("##viewpoint", &m_viewpointText);
		ImGui::SameLine();
		const auto parsed = EditorViewpoint::Parse(m_viewpointText);
		ImGui::BeginDisabled(!parsed.has_value());
		if (ImGui::Button("Go") && parsed)
		{
			SetViewpoint(*parsed);
		}
		ImGui::EndDisabled();

		ImGui::InputInt2("Capture Size", m_captureSize);
		m_captureSize[0] = std::clamp(m_captureSize[0], 16, 8192);
		m_captureSize[1] = std::clamp(m_captureSize[1], 16, 8192);

		if (ImGui::Button("Capture Screenshot"))
		{
			const auto now = std::chrono::system_clock::now().time_since_epoch();
			const std::filesystem::path path = std::filesystem::path("screenshots") /
				(m_assetPath.stem().string() + "_" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now).count()) + ".png");
			CaptureViewpoint(current, static_cast<uint32>(m_captureSize[0]), static_cast<uint32>(m_captureSize[1]), path, m_showFoliage);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Renders the current view at the capture size into screenshots/ next to the editor executable.");
		}
	}

	void WorldEditorInstance::AddAreaTrigger(proto::AreaTriggerEntry &trigger, bool select)
	{
		// Create manual render object for visualization
		ManualRenderObject* renderObject = m_scene.CreateManualRenderObject("AreaTrigger_" + std::to_string(trigger.id()));
		renderObject->SetCastShadows(false);
		renderObject->SetQueryFlags(SceneQueryFlags_AreaTriggers);

		// Create wireframe visualization
		auto lineListOp = renderObject->AddLineListOperation(MaterialManager::Get().Load("Editor/Wireframe"));

		const bool isSphere = trigger.has_radius();
		if (isSphere)
		{
			// Draw sphere wireframe
			const float radius = trigger.radius();
			const int segments = 16;
			const int rings = 8;

			// Vertical circles
			for (int i = 0; i < segments; ++i)
			{
				const float angle1 = (float)i / segments * 2.0f * Pi;
				const float angle2 = (float)(i + 1) / segments * 2.0f * Pi;

				for (int j = 0; j < rings; ++j)
				{
					const float ring1 = (float)j / rings * Pi - Pi / 2.0f;
					const float ring2 = (float)(j + 1) / rings * Pi - Pi / 2.0f;

					Vector3 p1(radius * cos(ring1) * cos(angle1), radius * sin(ring1), radius * cos(ring1) * sin(angle1));
					Vector3 p2(radius * cos(ring2) * cos(angle1), radius * sin(ring2), radius * cos(ring2) * sin(angle1));
					Vector3 p3(radius * cos(ring1) * cos(angle2), radius * sin(ring1), radius * cos(ring1) * sin(angle2));

					lineListOp->AddLine(p1, p2);
					lineListOp->AddLine(p1, p3);
				}
			}
		}
		else
		{
			// Draw box wireframe
			const float hx = trigger.box_x() / 2.0f;
			const float hy = trigger.box_y() / 2.0f;
			const float hz = trigger.box_z() / 2.0f;

			Vector3 corners[8] = {
				Vector3(-hx, -hy, -hz), Vector3(hx, -hy, -hz),
				Vector3(hx, -hy, hz), Vector3(-hx, -hy, hz),
				Vector3(-hx, hy, -hz), Vector3(hx, hy, -hz),
				Vector3(hx, hy, hz), Vector3(-hx, hy, hz)
			};

			// Bottom face
			for (int i = 0; i < 4; ++i)
			{
				lineListOp->AddLine(corners[i], corners[(i + 1) % 4]);
			}

			// Top face
			for (int i = 0; i < 4; ++i)
			{
				lineListOp->AddLine(corners[i + 4], corners[(i + 1) % 4 + 4]);
			}

			// Vertical edges
			for (int i = 0; i < 4; ++i)
			{
				lineListOp->AddLine(corners[i], corners[i + 4]);
			}
		}

		m_areaTriggerRenderObjects.push_back(renderObject);

		// Create scene node
		SceneNode* node = m_scene.GetRootSceneNode().CreateChildSceneNode(Vector3(trigger.x(), trigger.y(), trigger.z()));
		if (!isSphere && trigger.has_box_o())
		{
			node->SetOrientation(Quaternion(Radian(trigger.box_o()), Vector3::UnitY));
		}
		node->AttachObject(*renderObject);
		m_areaTriggerNodes.push_back(node);

		// Track this trigger for later lookup
		m_areaTriggerMap[&trigger] = {node, renderObject};

		// Add to selection if requested
		if (select)
		{
			auto removal = [this](const proto::AreaTriggerEntry& entry)
			{
				// Find and remove the trigger's visual representation
				auto it = m_areaTriggerMap.find(const_cast<proto::AreaTriggerEntry*>(&entry));
				if (it != m_areaTriggerMap.end())
				{
					auto [node, renderObject] = it->second;
					
					// Remove from scene
					m_scene.DestroyManualRenderObject(*renderObject);
					m_scene.GetRootSceneNode().RemoveChild(*node);
					m_scene.DestroySceneNode(*node);
					
					// Remove from tracking vectors
					auto nodeIt = std::find(m_areaTriggerNodes.begin(), m_areaTriggerNodes.end(), node);
					if (nodeIt != m_areaTriggerNodes.end())
					{
						m_areaTriggerNodes.erase(nodeIt);
					}
					
					auto renderIt = std::find(m_areaTriggerRenderObjects.begin(), m_areaTriggerRenderObjects.end(), renderObject);
					if (renderIt != m_areaTriggerRenderObjects.end())
					{
						m_areaTriggerRenderObjects.erase(renderIt);
					}
					
					// Remove from map
					m_areaTriggerMap.erase(it);
				}
			
			// Remove from proto data so it doesn't reappear
			m_editor.GetProject().areaTriggers.remove(entry.id());
		};

		auto duplication = [this](Selectable& selectable)
		{
			// Not supported
		};

		auto selectable = std::make_unique<SelectedAreaTrigger>(trigger, *node, *renderObject, duplication, removal);
		m_selection.AddSelectable(std::move(selectable));
	}
}

void WorldEditorInstance::RemoveAllAreaTriggers()
{
	for (auto* renderObject : m_areaTriggerRenderObjects)
	{
		m_scene.DestroyManualRenderObject(*renderObject);
	}
	m_areaTriggerRenderObjects.clear();

	for (auto* node : m_areaTriggerNodes)
	{
		m_scene.GetRootSceneNode().RemoveChild(*node);
		m_scene.DestroySceneNode(*node);
	}
	m_areaTriggerNodes.clear();
	
	m_areaTriggerMap.clear();
}

proto::AreaTriggerEntry* WorldEditorInstance::FindAreaTriggerByRenderObject(ManualRenderObject* renderObject)
{
	for (auto& [entry, pair] : m_areaTriggerMap)
	{
		if (pair.second == renderObject)
		{
			return entry;
		}
	}
	return nullptr;
}

void WorldEditorInstance::SelectAreaTrigger(proto::AreaTriggerEntry& trigger)
{
	auto it = m_areaTriggerMap.find(&trigger);
	if (it == m_areaTriggerMap.end())
	{
		return;
	}

	auto [node, renderObject] = it->second;

	m_selection.Clear();

	auto removal = [this](const proto::AreaTriggerEntry& entry)
	{
		auto it = m_areaTriggerMap.find(const_cast<proto::AreaTriggerEntry*>(&entry));
		if (it != m_areaTriggerMap.end())
		{
			auto [node, renderObject] = it->second;
			m_scene.DestroyManualRenderObject(*renderObject);
			m_scene.GetRootSceneNode().RemoveChild(*node);
			m_scene.DestroySceneNode(*node);
			auto nodeIt = std::find(m_areaTriggerNodes.begin(), m_areaTriggerNodes.end(), node);
			if (nodeIt != m_areaTriggerNodes.end())
			{
				m_areaTriggerNodes.erase(nodeIt);
			}
			auto renderIt = std::find(m_areaTriggerRenderObjects.begin(), m_areaTriggerRenderObjects.end(), renderObject);
			if (renderIt != m_areaTriggerRenderObjects.end())
			{
				m_areaTriggerRenderObjects.erase(renderIt);
			}
			m_areaTriggerMap.erase(it);
		}
		m_editor.GetProject().areaTriggers.remove(entry.id());
	};

	auto duplication = [this](Selectable& selectable)
	{
		// Not supported
	};

	auto selectable = std::make_unique<SelectedAreaTrigger>(trigger, *node, *renderObject, duplication, removal);
	m_selection.AddSelectable(std::move(selectable));
}

void WorldEditorInstance::DrawSceneOutlinePanel(const String &sceneOutlineId)
	{
		// Update the scene outline window regularly, especially when selection changes
		if (m_sceneOutlineWindow)
		{
			m_sceneOutlineWindow->Draw(sceneOutlineId.c_str());
		}
	}

	void WorldEditorInstance::DrawMinimapPanel(const String &id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			// Bind the panel to this world's minimaps on first draw.
			if (!m_minimapWorldSet)
			{
				m_minimapPanel.SetWorld(m_assetPath.filename().replace_extension().string());
				m_minimapWorldSet = true;
			}

			if (ImGui::Button("Refresh"))
			{
				m_minimapPanel.Refresh();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("Scroll to zoom, right-drag to pan, click a tile to teleport");

			// Highlight the page the camera is currently over.
			const PagePosition camPage = GetPagePositionFromCamera();
			m_minimapPanel.SetHighlightPage(static_cast<int32>(camPage.x()), static_cast<int32>(camPage.y()));

			if (m_minimapPanel.Draw(ImGui::GetContentRegionAvail()))
			{
				float worldX, worldZ;
				m_minimapPanel.GetSelectedWorldCenter(worldX, worldZ);
				MoveCameraToWorldPosition(worldX, worldZ);
			}
		}
		ImGui::End();
	}

	bool WorldEditorInstance::GenerateTerrainLod(const bool onlyStale)
	{
		if (!m_terrain || !m_hasTerrain)
		{
			ELOG("Cannot generate terrain LOD: no terrain available");
			return false;
		}

		// Hidden by the baker along with everything else outside the terrain, but foliage keeps
		// its own visibility flag on top of its scene objects.
		const bool foliageWasVisible = m_foliage && m_foliage->IsVisible();
		if (m_foliage) m_foliage->SetVisible(false);

		// The stand-ins would draw over the pages being baked, and must reload the new bake anyway.
		if (m_farTerrain) m_farTerrain->Clear();

		TerrainLodBaker::Stats stats;
		{
			TerrainLodBaker baker(m_scene, *m_terrain, [this]() { return m_dispatcher.poll_one() > 0; });
			stats = baker.BakeAll(onlyStale);
		}

		if (m_foliage) m_foliage->SetVisible(foliageWasVisible);

		return stats.failed == 0;
	}

	void WorldEditorInstance::GenerateMinimaps()
	{
		if (!m_terrain || !m_hasTerrain)
		{
			ELOG("Cannot generate minimaps: no terrain available");
			return;
		}

		// Minimap configuration
		const uint32 minimapSize = 256; // render resolution per page tile
		const float pageSize = terrain::constants::PageSize;

		auto &gx = GraphicsDevice::Get();
		gx.CaptureState();

		// -----------------------------------------------------------------------
		// Hide editor-only support objects so they don't appear in the minimap.
		// We record their previous visibility so we can restore it afterwards.
		// -----------------------------------------------------------------------
		const bool gridWasVisible = m_worldGrid && m_worldGrid->IsVisible();
		if (m_worldGrid) m_worldGrid->SetVisible(false);

		const bool debugBBWasVisible = m_debugBoundingBox && m_debugBoundingBox->IsVisible();
		if (m_debugBoundingBox) m_debugBoundingBox->SetVisible(false);

		// Distant stand-ins lie over pages that are not streamed in; the minimap renders the real pages.
		if (m_farTerrain) m_farTerrain->Clear();

		// Hide all area-trigger render objects
		std::vector<bool> areaTriggerWasVisible;
		areaTriggerWasVisible.reserve(m_areaTriggerRenderObjects.size());
		for (ManualRenderObject *ro : m_areaTriggerRenderObjects)
		{
			areaTriggerWasVisible.push_back(ro ? ro->IsVisible() : false);
			if (ro) ro->SetVisible(false);
		}

		// Hide all fog volume wireframes
		std::vector<std::pair<ManualRenderObject *, bool>> fogVolumeWasVisible;
		fogVolumeWasVisible.reserve(m_fogVolumeVisuals.size());
		for (const auto &[volumeId, visual] : m_fogVolumeVisuals)
		{
			fogVolumeWasVisible.emplace_back(visual.second, visual.second->IsVisible());
			visual.second->SetVisible(false);
		}

		// Hide debug / preview entity (e.g. placement ghost)
		const bool debugEntityWasVisible = m_debugEntity && m_debugEntity->IsVisible();
		if (m_debugEntity) m_debugEntity->SetVisible(false);

		// Hide authored tree foliage so it does not appear on the minimap.
		const bool foliageWasVisible = m_foliage && m_foliage->IsVisible();
		if (m_foliage) m_foliage->SetVisible(false);

		Camera *renderCam = m_scene.CreateCamera("MinimapCamera");
		renderCam->SetProjectionType(ProjectionType::Orthographic);
		renderCam->SetOrthoWindow(pageSize, pageSize);

		// Set appropriate clip distances for top-down orthographic view
		// Near plane should be close to capture terrain details
		renderCam->SetNearClipDistance(0.1f);
		// Far plane should be generous to capture tall entities
		renderCam->SetFarClipDistance(500.0f);

		SceneNode *camNode = m_scene.GetRootSceneNode().CreateChildSceneNode("MinimapCameraNode");
		camNode->AttachObject(*renderCam);

		// Create render texture for minimap generation
		RenderTexturePtr minimapRT = gx.CreateRenderTexture("MinimapGeneration", minimapSize, minimapSize,
															RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer,
															PixelFormat::R8G8B8A8, PixelFormat::D32F);

		if (!minimapRT)
		{
			ELOG("Failed to create render texture for minimap generation");
		}
		else
		{
			// Iterate through all terrain pages
			uint32 processedPages = 0;

			for (uint32 pageX = 0; pageX < m_terrain->GetWidth(); ++pageX)
			{
				for (uint32 pageY = 0; pageY < m_terrain->GetHeight(); ++pageY)
				{
					// Get the terrain page
					terrain::Page *page = m_terrain->GetPage(pageX, pageY);

					// Ensure page is loaded for rendering
					if (!page->IsLoaded())
					{
						continue;
					}

					// Switch this page's water to an opaque, solid-colour material so it shows up on
					// the minimap. The normal water material samples scene depth/refraction textures
					// that are not bound during minimap generation and would otherwise be invisible.
					page->SetMinimapWaterMode(true);

					// Calculate world position for page centers
					const float worldX = page->GetSceneNode()->GetDerivedPosition().x + pageSize * 0.5f;
					const float worldZ = page->GetSceneNode()->GetDerivedPosition().z + pageSize * 0.5f;

					// Get terrain height at page center for camera positioning
					float terrainHeight = page->GetBoundingBox().GetExtents().y;

					// Get the maximum height in this page to ensure we capture all entities
					float maxHeight = terrainHeight;

					// Check all map entities in this page to find the maximum height.
					// Only consider WMO (WorldModel) entities — static mesh entities are
					// excluded from the minimap entirely.
					for (const auto &mapEntity : m_mapEntities)
					{
						if (mapEntity && &mapEntity->GetSceneNode())
						{
							// Skip non-WMO entities (static meshes, etc.)
							if (!mapEntity->IsWorldModelEntity())
							{
								continue;
							}

							const Vector3 &entityPos = mapEntity->GetSceneNode().GetDerivedPosition();

							// Check if this entity is within the current page bounds
							if (entityPos.x >= page->GetSceneNode()->GetDerivedPosition().x &&
								entityPos.x < page->GetSceneNode()->GetDerivedPosition().x + pageSize &&
								entityPos.z >= page->GetSceneNode()->GetDerivedPosition().z &&
								entityPos.z < page->GetSceneNode()->GetDerivedPosition().z + pageSize)
							{
								// Get the movable object's bounding box to determine its height
								MovableObject *movable = mapEntity->GetMovableObject();
								if (movable)
								{
									const AABB &entityBounds = movable->GetBoundingBox();
									if (!entityBounds.IsNull())
									{
										const Vector3 entityWorldPos = mapEntity->GetSceneNode().GetDerivedPosition();
										const Vector3 entityScale = mapEntity->GetSceneNode().GetDerivedScale();

										// Calculate the actual world-space top of the entity
										float entityTop = entityWorldPos.y + (entityBounds.max.y * entityScale.y);
										maxHeight = std::max(maxHeight, entityTop);
									}
								}
							}
						}
					}

					// Position camera high enough above the highest object in the page
					// Add extra margin to ensure we capture everything
					const Vector3 cameraPos(worldX, maxHeight + 50.0f, worldZ);
					renderCam->GetParentSceneNode()->SetPosition(cameraPos);

					// Set up camera to look straight down (negative Y direction)
					// Camera's default forward is -Z, so we need to rotate 90 degrees around X to look down -Y
					// This creates a top-down orthographic view where the camera looks down at the XZ terrain plane
					const Quaternion topDownOrientation = Quaternion(Degree(-90.0f), Vector3::UnitX);
					renderCam->GetParentSceneNode()->SetOrientation(topDownOrientation);
					renderCam->InvalidateFrustum();
					renderCam->InvalidateView();

					// Setup render target and projection
					minimapRT->Activate();
					minimapRT->Clear(ClearFlags::All);

					// Temporarily change entity render queue groups to match terrain for consistent rendering.
					// Only WMO (WorldModel) entities are considered; static mesh entities are hidden so
					// they don't appear in the minimap at all.
					std::vector<std::pair<MovableObject *, uint8>> originalRenderQueues;
					std::vector<std::pair<MovableObject *, bool>> hiddenMeshEntities;
					for (const auto &mapEntity : m_mapEntities)
					{
						if (mapEntity && &mapEntity->GetSceneNode())
						{
							const Vector3 &entityPos = mapEntity->GetSceneNode().GetDerivedPosition();
							if (entityPos.x >= page->GetSceneNode()->GetDerivedPosition().x &&
								entityPos.x < page->GetSceneNode()->GetDerivedPosition().x + pageSize &&
								entityPos.z >= page->GetSceneNode()->GetDerivedPosition().z &&
								entityPos.z < page->GetSceneNode()->GetDerivedPosition().z + pageSize)
							{
								MovableObject *movable = mapEntity->GetMovableObject();
								if (movable)
								{
									if (mapEntity->IsWorldModelEntity())
									{
										// WMOs: move to terrain render queue so they render correctly
										originalRenderQueues.emplace_back(movable, movable->GetRenderQueueGroup());
										movable->SetRenderQueueGroup(WorldGeometry1); // Same as terrain
									}
									else
									{
										// Static mesh entities: hide them for this render pass
										const bool wasVisible = movable->IsVisible();
										hiddenMeshEntities.emplace_back(movable, wasVisible);
										movable->SetVisible(false);
									}
								}
							}
						}
					}

					// Render the scene (terrain and objects in this page)
					// Minimap tiles are a top-down map, not a view through the atmosphere. (The old
					// SetFogRange here was never undone and silently removed the viewport fog.)
					const bool fogWasEnabled = m_scene.IsFogEnabled();
					m_scene.SetFogEnabled(false);
					m_scene.Render(*renderCam, PixelShaderType::Forward);
					m_scene.SetFogEnabled(fogWasEnabled);
					minimapRT->Update();

					// Restore original render queue groups
					for (const auto &[movable, originalQueue] : originalRenderQueues)
					{
						movable->SetRenderQueueGroup(originalQueue);
					}

					// Restore visibility of hidden mesh entities
					for (const auto &[movable, wasVisible] : hiddenMeshEntities)
					{
						movable->SetVisible(wasVisible);
					}

					// Restore the page's normal (translucent) water material now that the tile is rendered.
					page->SetMinimapWaterMode(false);

					// Create texture from render target
					TexturePtr minimapTexture = minimapRT->StoreToTexture();
					if (!minimapTexture)
					{
						WLOG("Failed to store minimap texture for page (" << pageX << ", " << pageY << ")");
						continue;
					}

					// Generate filename with encoded coordinates
					const uint16 pageIndex = BuildPageIndex(static_cast<uint8>(pageX), static_cast<uint8>(pageY));
					const String minimapFilename = "Textures/Minimaps/" + m_assetPath.filename().replace_extension().string() + "/" + std::to_string(pageIndex) + ".htex";

					// Save texture to AssetRegistry
					try
					{
						std::unique_ptr<std::ostream> outStream = AssetRegistry::CreateNewFile(minimapFilename);
						if (outStream)
						{
							// Save texture using the engine's texture format.
							// We use uncompressed RGBA instead of DXT1 to preserve maximum quality:
							// DXT1 is lossy and introduces visible block artefacts on the fine details
							// of terrain/WMO colours, while the file-size increase for a 256×256 tile
							// is only ~32 KB per page — an acceptable trade-off.
							io::StreamSink sink(*outStream);
							tex::v1_0::Header header(tex::Version_1_0);
							header.width = minimapSize;
							header.height = minimapSize;
							header.format = tex::v1_0::RGBA;
							header.hasMips = false;

							tex::v1_0::HeaderSaver saver(sink, header);

							header.mipmapOffsets[0] = static_cast<uint32>(sink.Position());

							// Copy raw pixel data from render texture (4 bytes per pixel, RGBA)
							const uint32 pixelDataSize = minimapSize * minimapSize * 4;
							std::vector<uint8> pixelData(pixelDataSize);
							minimapTexture->CopyPixelDataTo(pixelData.data());

							// Write uncompressed RGBA data directly — no lossy block compression
							header.mipmapLengths[0] = pixelDataSize;
							sink.Write(reinterpret_cast<const char *>(pixelData.data()), pixelDataSize);
							saver.finish();

							processedPages++;
							ILOG("Generated minimap for page (" << pageX << ", " << pageY << ") -> " << minimapFilename);
						}
						else
						{
							WLOG("Failed to create file for minimap: " << minimapFilename);
						}
					}
					catch (const std::exception &e)
					{
						ELOG("Error saving minimap for page (" << pageX << ", " << pageY << "): " << e.what());
					}
				}
			}

			ILOG("Minimap generation completed. Processed " << processedPages << " terrain pages.");
		}

		// Cleanup
		m_scene.DestroyCamera(*renderCam);
		m_scene.DestroySceneNode(*camNode);

		// Restore visibility of editor support objects that were hidden during minimap generation
		if (m_worldGrid) m_worldGrid->SetVisible(gridWasVisible);
		if (m_debugBoundingBox) m_debugBoundingBox->SetVisible(debugBBWasVisible);
		for (size_t i = 0; i < m_areaTriggerRenderObjects.size(); ++i)
		{
			if (m_areaTriggerRenderObjects[i])
			{
				m_areaTriggerRenderObjects[i]->SetVisible(areaTriggerWasVisible[i]);
			}
		}
		for (const auto &[renderObject, wasVisible] : fogVolumeWasVisible)
		{
			renderObject->SetVisible(wasVisible);
		}
		if (m_debugEntity) m_debugEntity->SetVisible(debugEntityWasVisible);
		if (m_foliage) m_foliage->SetVisible(foliageWasVisible);

		gx.RestoreState();

		// Force the minimap teleport panel to reload the freshly generated tiles.
		m_minimapPanel.Refresh();
	}

	Camera &WorldEditorInstance::GetCamera() const
	{
		ASSERT(m_camera);
		return *m_camera;
	}

	uint16 WorldEditorInstance::BuildPageIndex(uint8 x, const uint8 y) const
	{
		return (x << 8) | y;
	}

	bool WorldEditorInstance::GetPageCoordinatesFromIndex(const uint16 pageIndex, uint8 &x, uint8 &y) const
	{
		if (pageIndex > 0xFFFF)
		{
			return false;
		}

		x = static_cast<uint8>(pageIndex >> 8);
		y = static_cast<uint8>(pageIndex & 0xFF);
		return true;
	}

	void WorldEditorInstance::LoadPageEntities(const uint8 x, const uint8 y)
	{
		String baseFileName = (m_assetPath.parent_path() / m_assetPath.filename().replace_extension() / "Entities" / std::to_string(BuildPageIndex(x, y))).string();
		std::transform(baseFileName.begin(), baseFileName.end(), baseFileName.begin(), [](char c)
					   { return c == '\\' ? '/' : c; });

		const auto &files = AssetRegistry::ListFiles(baseFileName, ".wobj");
		for (const auto &file : files)
		{
			std::unique_ptr<std::istream> filePtr = AssetRegistry::OpenFile(file);
			if (!filePtr)
			{
				ELOG("Failed to open file " << file << "!");
				continue;
			}

			io::StreamSource source{*filePtr};
			io::Reader reader{source};
			WorldEntityLoader loader;
			if (!loader.Read(reader))
			{
				ELOG("Failed to read file " << file << "!");
				continue;
			}

			const auto &entity = loader.GetEntity();

			MapEntity *mapEntity = nullptr;

			if (entity.entityType == WorldEntityType::WorldModel)
			{
				// Use the wrapper so the remove signal gets connected (consistent with mesh entity loading below).
				WorldModelInstance *wmoInstance = CreateWorldModelEntity(
					entity.meshName,
					entity.position,
					entity.rotation,
					entity.scale,
					entity.uniqueId);
				if (!wmoInstance)
				{
					ELOG("Failed to create world model entity from file " << file << "!");
					continue;
				}

				mapEntity = wmoInstance->GetUserObject<MapEntity>();

				// Apply the placement's prop kits. The paths are kept even when a kit
				// fails to load, so a save does not silently drop them.
				// Unsaved kit edits made in this world show up on freshly loaded placements too.
				if (mapEntity && !entity.kitPaths.empty())
				{
					mapEntity->SetKitPaths(entity.kitPaths);
					ApplyKits(*mapEntity);
				}
			}
			else
			{
				// Create mesh entity
				Entity *object = CreateMapEntity(
					entity.meshName,
					entity.position,
					entity.rotation,
					entity.scale,
					entity.uniqueId);
				if (!object)
				{
					ELOG("Failed to create entity from file " << file << "!");
					continue;
				}

				mapEntity = object->GetUserObject<MapEntity>();

				// Apply material overrides (only for mesh entities)
				for (const auto &materialOverride : entity.materialOverrides)
				{
					if (materialOverride.materialIndex >= object->GetNumSubEntities())
					{
						WLOG("Entity has material override for material index greater than entity material count! Skipping material override");
						continue;
					}

					object->GetSubEntity(materialOverride.materialIndex)->SetMaterial(MaterialManager::Get().Load(materialOverride.materialName));
				}
			}

			// We just loaded the object - it has not been modified
			ASSERT(mapEntity);
			mapEntity->SetDisplayName(entity.name);
			mapEntity->SetCategory(entity.category);
			mapEntity->MarkAsUnmodified();
		}
	}

	void WorldEditorInstance::UnloadPageEntities(uint8 x, uint8 y)
	{
		const PagePosition targetPos(x, y);

		// Collect WMO raw pointers BEFORE erasing so we can release factory ownership
		// after ~MapEntity has run (which calls Destroy() and detaches the WMO).
		// Only unload unmodified entities — modified ones must stay alive so the user
		// doesn't lose unsaved changes when paging back in.
		std::vector<WorldModelInstance*> wmoToRemove;
		for (const auto& mapEntity : m_mapEntities)
		{
			const auto& refPos = mapEntity->GetReferencePagePosition();
			if (!refPos.has_value() || *refPos != targetPos)
			{
				continue;
			}
			if (mapEntity->IsModified())
			{
				continue;
			}
			if (mapEntity->IsWorldModelEntity())
			{
				wmoToRemove.push_back(mapEntity->GetWorldModelInstance());
			}
		}

		// Erase unmodified map entities belonging to this page.
		// ~MapEntity calls WorldModelInstance::Destroy() then detaches and destroys the scene node.
		std::erase_if(m_mapEntities, [&targetPos](const auto& mapEntity)
		{
			const auto& refPos = mapEntity->GetReferencePagePosition();
			return refPos.has_value() && *refPos == targetPos && !mapEntity->IsModified();
		});

		// Now free factory ownership of WMO instances (their child objects were already
		// cleaned up by ~MapEntity above).
		for (WorldModelInstance* wmo : wmoToRemove)
		{
			m_entityFactory->RemoveWorldModelInstance(wmo);
		}

		// Reflect the removal in the scene outline.
		if (m_sceneOutlineWindow)
		{
			m_sceneOutlineWindow->Update();
		}
	}

	void WorldEditorInstance::SetupGrass()
	{
		m_grass = std::make_unique<Foliage>(m_scene, GraphicsDevice::Get());

		// Sample the editor's terrain for height, normal, holes, material and coverage, exactly
		// like the client so foliage placement matches in-game. Placement rules are data-driven
		// per terrain material; this only resolves raw terrain data at a point.
		m_grass->SetTerrainSampleCallback([this](float x, float z, FoliagePlacementSample& out) -> bool
		{
			if (!m_hasTerrain || !m_terrain)
			{
				return false;
			}

			if (m_terrain->IsHoleAt(x, z))
			{
				return false;
			}

			out.height = m_terrain->GetSmoothHeightAt(x, z);
			out.normal = m_terrain->GetSmoothNormalAt(x, z);
			out.color = m_terrain->GetSmoothColorAt(x, z);

			if (const MaterialPtr material = m_terrain->GetBaseMaterialAt(x, z))
			{
				// Use the painted material directly (not the root) so per-instance foliage gating works.
				out.baseMaterial = material.get();
			}

			for (uint8 layer = 0; layer < 4; ++layer)
			{
				out.coverage[layer] = m_terrain->GetLayerValueAt(x, z, layer);
			}

			out.valid = true;
			return true;
		});

		FoliageSettings settings;
		settings.chunkSize = 32.0f;
		settings.maxViewDistance = 50.0f;
		settings.loadRadius = 3;
		settings.frustumCulling = true;
		settings.globalDensityMultiplier = 1.0f;
		m_grass->SetSettings(settings);

		// Terrain is centered at the origin and spans 64x64 pages.
		constexpr float halfTerrainSize = 64.0f * terrain::constants::PageSize * 0.5f;
		m_grass->SetBounds(AABB(
			Vector3(-halfTerrainSize, -1000.0f, -halfTerrainSize),
			Vector3(halfTerrainSize, 1000.0f, halfTerrainSize)));

		// Foliage layers are registered dynamically from the foliage definitions carried by the
		// terrain materials of each loaded terrain page. See RegisterPageFoliage / UnregisterPageFoliage.

		// Hidden by default; the "Show Foliage" world setting toggles it.
		m_grass->SetVisible(m_showFoliage);
	}

	void WorldEditorInstance::RegisterPageFoliage(const uint32 pageX, const uint32 pageY)
	{
		if (!m_grass || !m_terrain)
		{
			return;
		}

		terrain::Page* page = m_terrain->GetPage(pageX, pageY);
		if (!page || !page->IsLoaded())
		{
			return;
		}

		const uint16 pageIndex = static_cast<uint16>(pageX + pageY * 64);

		// A page may be reported available more than once; rebuild its contribution from scratch.
		UnregisterPageFoliage(pageX, pageY);

		std::vector<FoliageLayerKey> contributed;
		bool addedAnyLayer = false;

		for (uint32 tileY = 0; tileY < terrain::constants::TilesPerPage; ++tileY)
		{
			for (uint32 tileX = 0; tileX < terrain::constants::TilesPerPage; ++tileX)
			{
				terrain::Tile* tile = page->GetTile(tileX, tileY);
				if (!tile)
				{
					continue;
				}

				// The tile's assigned material (the painted .hmat or .hmi). Reading foliage from this
				// interface respects per-instance overrides; gating on its pointer distinguishes
				// instances that share the same base material.
				const MaterialPtr paintedMaterial = tile->GetBaseMaterial();
				if (!paintedMaterial)
				{
					continue;
				}

				MaterialInterface* material = paintedMaterial.get();
				if (material->GetFoliageEntries().empty())
				{
					continue;
				}

				for (const MaterialFoliageEntry& entry : material->GetFoliageEntries())
				{
					const FoliageLayerKey key{ material, entry.layerIndex, entry.meshPath };

					if (std::find(contributed.begin(), contributed.end(), key) != contributed.end())
					{
						continue;
					}
					contributed.push_back(key);

					auto it = m_foliageRegistry.find(key);
					if (it != m_foliageRegistry.end())
					{
						++it->second.refCount;
						continue;
					}

					MeshPtr mesh = MeshManager::Get().Load(entry.meshPath);
					if (!mesh)
					{
						WLOG("Foliage mesh '" << entry.meshPath << "' could not be loaded for material " << material->GetName());
						m_foliageRegistry.emplace(key, RegisteredFoliageLayer{ nullptr, 1 });
						continue;
					}

					const String layerName = String(material->GetName()) + "#" +
						std::to_string(static_cast<int>(entry.layerIndex)) + "#" + entry.meshPath;

					auto layer = std::make_shared<FoliageLayer>(layerName, mesh);
					FoliageLayerSettings& s = layer->GetSettings();
					s.density = entry.density;
					s.minScale = entry.minScale;
					s.maxScale = entry.maxScale;
					s.maxSlopeAngle = entry.maxSlopeAngle;
					s.minHeight = entry.minHeight;
					s.maxHeight = entry.maxHeight;
					s.fadeStartDistance = entry.fadeStartDistance;
					s.fadeEndDistance = entry.fadeEndDistance;
					s.randomYawRotation = entry.randomYaw;
					s.alignToNormal = entry.alignToNormal;
					s.castShadows = entry.castShadows;
					s.terrainLayerIndex = static_cast<int32>(entry.layerIndex);
					s.minCoverage = entry.minCoverage;
					s.terrainMaterial = material;

					m_grass->AddLayer(layer);
					m_foliageRegistry.emplace(key, RegisteredFoliageLayer{ layer, 1 });
					addedAnyLayer = true;
				}
			}
		}

		if (!contributed.empty())
		{
			m_pageFoliageKeys[pageIndex] = std::move(contributed);
		}

		if (addedAnyLayer)
		{
			m_grass->InvalidateActiveChunks();
		}
	}

	void WorldEditorInstance::UnregisterPageFoliage(const uint32 pageX, const uint32 pageY)
	{
		const uint16 pageIndex = static_cast<uint16>(pageX + pageY * 64);

		auto pageIt = m_pageFoliageKeys.find(pageIndex);
		if (pageIt == m_pageFoliageKeys.end())
		{
			return;
		}

		bool removedAnyLayer = false;

		for (const FoliageLayerKey& key : pageIt->second)
		{
			auto it = m_foliageRegistry.find(key);
			if (it == m_foliageRegistry.end())
			{
				continue;
			}

			if (it->second.refCount > 0)
			{
				--it->second.refCount;
			}

			if (it->second.refCount == 0)
			{
				if (it->second.layer && m_grass)
				{
					m_grass->RemoveLayer(it->second.layer->GetName());
					removedAnyLayer = true;
				}
				m_foliageRegistry.erase(it);
			}
		}

		m_pageFoliageKeys.erase(pageIt);

		if (removedAnyLayer && m_grass)
		{
			m_grass->InvalidateActiveChunks();
		}
	}

	void WorldEditorInstance::LoadPageFoliage(const uint8 x, const uint8 y)
	{
		if (!m_foliage)
		{
			return;
		}

		const uint16 pageIndex = BuildPageIndex(x, y);

		String fileName = (m_assetPath.parent_path() / m_assetPath.filename().replace_extension() / "Foliage" / (std::to_string(pageIndex) + ".hfol")).string();
		std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](char c)
					   { return c == '\\' ? '/' : c; });

		// A page without authored foliage simply has no file - that is not an error.
		std::unique_ptr<std::istream> filePtr = AssetRegistry::OpenFile(fileName);
		if (!filePtr)
		{
			return;
		}

		io::StreamSource source{*filePtr};
		io::Reader reader{source};
		WorldFoliageLoader loader;
		if (!loader.Read(reader))
		{
			ELOG("Failed to read foliage file " << fileName << "!");
			return;
		}

		for (const auto &src : loader.GetInstances())
		{
			InstancedFoliageInstance instance;
			instance.uniqueId = src.uniqueId;
			instance.meshName = src.meshName;
			instance.position = src.position;
			instance.rotation = src.rotation;
			instance.scale = src.scale;
			instance.pageIndex = pageIndex;
			instance.collides = src.collides;
			m_foliage->AddInstance(instance);
		}

		m_foliage->RebuildDirtyCells();
	}

	void WorldEditorInstance::UpdateWaterVisibility()
	{
		if (!m_terrain)
		{
			return;
		}

		// Water is always shown while the water edit mode is active so it can be edited,
		// regardless of the "Show Water" setting.
		const bool inWaterEditMode = (m_editMode == m_terrainEditMode.get()
			&& m_terrainEditMode->GetTerrainEditType() == TerrainEditType::Water);
		const bool effective = m_showWater || inWaterEditMode;

		if (effective != m_waterVisibilityApplied)
		{
			m_terrain->SetWaterVisible(effective);
			m_waterVisibilityApplied = effective;
		}
	}

	void WorldEditorInstance::UpdateUnderwaterPreview(const float deltaSeconds)
	{
		if (!m_waterVolume || !m_deferredRenderer || !m_camera)
		{
			return;
		}

		// With water hidden there is no surface to be under, and fog below an invisible surface
		// would only read as a rendering bug.
		if (!m_waterVisibilityApplied)
		{
			m_deferredRenderer->SetUnderwaterState(UnderwaterState());
			return;
		}

		// The editor has no character, so the camera stands in for the player as well. Audio is
		// deliberately left alone: the editor's audio device previews sounds, and muffling it while
		// flying around under water would only get in the way.
		const Vector3 cameraPosition = m_camera->GetDerivedPosition();
		m_waterVolume->Update(cameraPosition.x, cameraPosition.y, cameraPosition.z,
			cameraPosition.x, cameraPosition.y, cameraPosition.z, deltaSeconds);

		m_deferredRenderer->SetUnderwaterState(m_waterVolume->GetState());

		const uint32 screenWaterType = m_waterVolume->GetScreenWaterType();
		const auto* profile = screenWaterType != 0 ? m_editor.GetProject().waterProfiles.getById(screenWaterType) : nullptr;
		const String causticsTexture = (profile != nullptr && profile->has_caustics_texture())
			? profile->caustics_texture()
			: String();

		if (causticsTexture != m_underwaterCausticsTexture)
		{
			if (PostProcessPass* pass = m_deferredRenderer->GetPostProcessPass())
			{
				pass->SetCausticsTexture(causticsTexture.empty() ? nullptr : TextureManager::Get().CreateOrRetrieve(causticsTexture));
				m_underwaterCausticsTexture = causticsTexture;
			}
		}
	}

	void WorldEditorInstance::RemoveAllUnitSpawns()
	{
		for (auto *entity : m_spawnEntities)
		{
			m_scene.DestroyEntity(*entity);
		}

		m_spawnEntities.clear();

		for (auto *node : m_spawnNodes)
		{
			m_scene.GetRootSceneNode().RemoveChild(*node);
			m_scene.DestroySceneNode(*node);
		}

		m_spawnNodes.clear();

		m_entityFactory->ResetUnitSpawnIdGenerator();
		m_entityFactory->ResetObjectSpawnIdGenerator();
	}

	void WorldEditorInstance::OnPageAvailabilityChanged(const PageNeighborhood &page, const bool isAvailable)
	{
		if (!m_terrain)
		{
			return;
		}

		const auto &mainPage = page.GetMainPage();
		const PagePosition &pos = mainPage.GetPosition();

		auto *terrainPage = m_terrain->GetPage(pos.x(), pos.y());
		if (!terrainPage)
		{
			return;
		}

		if (isAvailable)
		{
			terrainPage->Prepare();
			EnsurePageIsLoaded(pos);

			LoadPageEntities(pos.x(), pos.y());
			LoadPageFoliage(pos.x(), pos.y());
			RegisterPageFoliage(pos.x(), pos.y());
		}
		else
		{
			UnloadPageEntities(pos.x(), pos.y());
			UnregisterPageFoliage(pos.x(), pos.y());

			if (m_foliage)
			{
				m_foliage->UnloadPage(BuildPageIndex(pos.x(), pos.y()));
				m_foliage->RebuildDirtyCells();
			}

			terrainPage->Unload();
		}
	}

	void WorldEditorInstance::EnsurePageIsLoaded(PagePosition pos)
	{
		auto *page = m_terrain->GetPage(pos.x(), pos.y());
		if (!page || !page->IsLoadable())
		{
			return;
		}

		if (!page->Load())
		{
			m_dispatcher.post([this, pos]()
							  { EnsurePageIsLoaded(pos); });
			return;
		}

		// Grass layers come from the materials of a fully loaded page. Page::Load creates one tile
		// per call, so the registration in OnPageAvailabilityChanged usually saw an unfinished page
		// and skipped it; this is the first point at which every tile exists.
		RegisterPageFoliage(pos.x(), pos.y());
	}

	void WorldEditorInstance::Visit(SelectedMapEntity &selectable)
	{
		MapEntity &mapEntity = selectable.GetEntity();
		
		ImGui::Text("Unique Id: %llu", mapEntity.GetUniqueId());

		if (mapEntity.IsWorldModelEntity())
		{
			// World model specific UI
			ImGui::Text("Type: World Model");
			ImGui::Text("Asset: %s", mapEntity.GetAssetName().c_str());

			if (ImGui::CollapsingHeader("Prop Kits", ImGuiTreeNodeFlags_DefaultOpen))
			{
				std::vector<String> kitPaths = mapEntity.GetKitPaths();
				bool kitsChanged = false;
				int removeIndex = -1;
				int moveUpIndex = -1;
				int moveDownIndex = -1;

				for (int i = 0; i < static_cast<int>(kitPaths.size()); ++i)
				{
					ImGui::PushID(i);

					const PropKitPtr kit = ResolveKit(kitPaths[i]);

					ImGui::BeginDisabled(i == 0);
					if (ImGui::SmallButton("^"))
					{
						moveUpIndex = i;
					}
					ImGui::EndDisabled();
					ImGui::SameLine();

					ImGui::BeginDisabled(i + 1 >= static_cast<int>(kitPaths.size()));
					if (ImGui::SmallButton("v"))
					{
						moveDownIndex = i;
					}
					ImGui::EndDisabled();
					ImGui::SameLine();

					if (ImGui::SmallButton(ICON_FA_TRASH))
					{
						removeIndex = i;
					}
					ImGui::SameLine();

					if (kit)
					{
						const bool unsaved = m_dirtyKits.contains(MakeKitKey(kitPaths[i]));
						ImGui::Text("%s%s", kit->name.c_str(), unsaved ? " *" : "");
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip(unsaved ? "%s\nUnsaved changes, written when the world is saved" : "%s", kitPaths[i].c_str());
						}
					}
					else
					{
						ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " %s", kitPaths[i].c_str());
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("Prop kit failed to load");
						}
					}

					ImGui::PopID();
				}

				if (removeIndex >= 0)
				{
					kitPaths.erase(kitPaths.begin() + removeIndex);
					kitsChanged = true;
				}
				else if (moveUpIndex > 0)
				{
					std::swap(kitPaths[moveUpIndex], kitPaths[moveUpIndex - 1]);
					kitsChanged = true;
				}
				else if (moveDownIndex >= 0 && moveDownIndex + 1 < static_cast<int>(kitPaths.size()))
				{
					std::swap(kitPaths[moveDownIndex], kitPaths[moveDownIndex + 1]);
					kitsChanged = true;
				}

				// Picker for a new kit: the pick is appended and the picker starts empty again
				String newKitPath;
				static const std::set<String> kitExtensions = { ".hkit" };
				if (AssetPickerWidget::Draw("Add Prop Kit", newKitPath, kitExtensions) && !newKitPath.empty())
				{
					kitPaths.push_back(newKitPath);
					kitsChanged = true;
				}

				if (kitsChanged)
				{
					mapEntity.SetKitPaths(std::move(kitPaths));
					ApplyKits(mapEntity);
					DeselectRemovedKitProps(mapEntity);
				}

				ImGui::Separator();

				// Active kit: where meshes dropped into the viewport go while this building is selected.
				const String activeKit = GetActiveKit(mapEntity);
				ImGui::BeginDisabled(mapEntity.GetKitPaths().empty());
				if (ImGui::BeginCombo("Active kit", activeKit.empty() ? "None" : activeKit.c_str()))
				{
					if (ImGui::Selectable("None", activeKit.empty()))
					{
						m_activeKits[mapEntity.GetUniqueId()] = String();
					}

					for (size_t i = 0; i < mapEntity.GetKitPaths().size(); ++i)
					{
						const String &kitPath = mapEntity.GetKitPaths()[i];
						ImGui::PushID(static_cast<int>(i));
						if (ImGui::Selectable(kitPath.c_str(), kitPath == activeKit))
						{
							m_activeKits[mapEntity.GetUniqueId()] = kitPath;
						}
						ImGui::PopID();
					}

					ImGui::EndCombo();
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip("Meshes dropped into the viewport while this building is selected are added to the active kit.\nClick a prop of a selected building to select and move it; that edits the kit it came from.");
				}

				ImGui::BeginDisabled(activeKit.empty());
				if (ImGui::Button("Save as new kit..."))
				{
					const Path source(activeKit);
					String target = (source.parent_path() / (source.stem().string() + "_" + std::to_string(mapEntity.GetUniqueId()) + ".hkit")).string();
					std::replace(target.begin(), target.end(), '\\', '/');

					m_saveAsNewKitPlacement = mapEntity.GetUniqueId();
					m_saveAsNewKitSource = activeKit;
					m_saveAsNewKitTarget = std::move(target);
					m_openSaveAsNewKit = true;
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip("Writes the active kit's current props and lights to a new file and uses it for this building only.\nThe original kit file is not changed.");
				}

				bool showKitCollision = m_showKitCollision && m_kitCollisionPlacementId == mapEntity.GetUniqueId();
				if (ImGui::Checkbox("Show kit collision", &showKitCollision))
				{
					m_showKitCollision = showKitCollision;
					m_kitCollisionPlacementId = mapEntity.GetUniqueId();
					m_kitCollisionDirty = true;
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Show the collision of this building's kit props (green: walkable, orange: too steep)");
				}
			}
		}
		else if (Entity* entity = mapEntity.GetEntity())
		{
			// Mesh entity specific UI
			ImGui::Text("Type: Mesh Entity");

			if (ImGui::CollapsingHeader("Mesh"))
			{
				const MeshPtr mesh = entity->GetMesh();
				String meshName = mesh->GetName().data();

				if (AssetPickerWidget::Draw("Mesh", meshName, asset_extensions::Meshes))
				{
					if (!meshName.empty())
					{
						entity->SetMesh(MeshManager::Get().Load(meshName));
						mapEntity.MarkModified();
					}
				}
			}

			if (ImGui::CollapsingHeader("Materials"))
			{
				for (uint32 i = 0; i < entity->GetNumSubEntities(); ++i)
				{
					SubEntity *sub = entity->GetSubEntity(i);
					ASSERT(sub);

					SubMesh &submesh = entity->GetMesh()->GetSubMesh(i);

					ImGui::PushID(i);

					// Get material
					MaterialPtr material = sub->GetMaterial();
					if (!material)
					{
						material = submesh.GetMaterial();
					}

					String materialName = sub->GetMaterial()->GetName().data();

					bool isOverridden = material != submesh.GetMaterial();

					ImGui::BeginDisabled(!isOverridden);
					if (ImGui::Checkbox("##overridden", &isOverridden))
					{
						sub->SetMaterial(submesh.GetMaterial());
						mapEntity.MarkModified();
					}
					ImGui::EndDisabled();

					ImGui::SameLine();

					if (AssetPickerWidget::Draw("Material", materialName, asset_extensions::Materials))
					{
						if (!materialName.empty())
						{
							sub->SetMaterial(MaterialManager::Get().Load(materialName));
						}
						else
						{
							// Clearing the material resets the override back to the submesh default
							sub->SetMaterial(submesh.GetMaterial());
						}
						mapEntity.MarkModified();
					}

					ImGui::PopID();
				}
			}
		}
	}

	void WorldEditorInstance::Visit(SelectedTerrainTile &selectable)
	{
		if (ImGui::CollapsingHeader("Tile"))
		{
			terrain::Tile &tile = selectable.GetTile();

			ImGui::Text("Tile Grid: %d x %d", tile.GetX(), tile.GetY());
			ImGui::Text("Page Grid: %d x %d", tile.GetPage().GetX(), tile.GetPage().GetY());

			// The tile material is wrapped in a per-tile MaterialInstance, so show the base material
			String materialName;
			if (const MaterialPtr baseMaterial = tile.GetBaseMaterial())
			{
				materialName = baseMaterial->GetName();
			}

			if (AssetPickerWidget::Draw("Material", materialName, asset_extensions::Materials))
			{
				// SetMaterial ignores null, so clearing the selection intentionally does nothing
				if (!materialName.empty())
				{
					tile.SetMaterial(MaterialManager::Get().Load(materialName));
				}
			}

			if (ImGui::Button("Set For Page"))
			{
				terrain::Page &page = tile.GetPage();
				for (uint32 x = 0; x < terrain::constants::TilesPerPage; ++x)
				{
					for (uint32 y = 0; y < terrain::constants::TilesPerPage; ++y)
					{
						terrain::Tile*pageTile = page.GetTile(x, y);
						if (pageTile)
						{
							pageTile->SetMaterial(tile.GetBaseMaterial());
						}
					}
				}
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Sets the selected material for all tiles on the whole page");
			}
		}
	}

	void WorldEditorInstance::Visit(SelectedUnitSpawn &selectable)
	{
		// Notify the spawn edit mode which spawn is currently shown so it can
		// enable waypoint editing when movement type is PATROL.
		m_spawnEditMode->SetSelectedSpawn(&selectable.GetEntry());

		if (ImGui::CollapsingHeader("Unit Spawn"))
		{
			// Optional name used for trigger target resolution (NamedCreature).
			ImGui::InputText("Spawn Name", selectable.GetEntry().mutable_name());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Optional unique name for this spawn point.\nUsed by trigger actions that target a 'Named Creature'.");

			proto::UnitEntry *selectedUnit = m_editor.GetProject().units.getById(selectable.GetEntry().unitentry());
			if (ImGui::BeginCombo("Unit", selectedUnit ? selectedUnit->name().c_str() : "(None)"))
			{
				for (const auto &unit : m_editor.GetProject().units.getTemplates().entry())
				{
					ImGui::PushID(unit.id());
					if (ImGui::Selectable(unit.name().c_str(), &unit == selectedUnit))
					{
						selectable.GetEntry().set_unitentry(unit.id());
						selectable.RefreshEntity();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			bool isActive = selectable.GetEntry().isactive();
			if (ImGui::Checkbox("Is Active", &isActive))
			{
				selectable.GetEntry().set_isactive(isActive);
			}

			bool respawn = selectable.GetEntry().respawn();
			if (ImGui::Checkbox("Respawn", &respawn))
			{
				selectable.GetEntry().set_respawn(respawn);
			}

			float healthPercent = selectable.GetEntry().health_percent();
			if (ImGui::SliderFloat("Health Percent", &healthPercent, 0.0f, 1.0f))
			{
				selectable.GetEntry().set_health_percent(healthPercent);
			}

			static const char *s_standStateStrings[] = {
				"Stand",
				"Sit",
				"Sleep",
				"Dead",
				"Kneel"};

			static_assert(std::size(s_standStateStrings) == unit_stand_state::Count_, "Size of stand state strings must match");

			if (ImGui::BeginCombo("Stand State", s_standStateStrings[selectable.GetEntry().standstate()]))
			{
				uint32 index = 0;
				for (const auto &mode : s_standStateStrings)
				{
					if (ImGui::Selectable(mode, selectable.GetEntry().movement() == index))
					{
						selectable.GetEntry().set_standstate(index);
					}

					index++;
				}

				ImGui::EndCombo();
			}

			uint64 respawnDelay = selectable.GetEntry().respawndelay();
			if (ImGui::InputScalar("Respawn Delay (ms)", ImGuiDataType_U64, &respawnDelay))
			{
				selectable.GetEntry().set_respawndelay(respawnDelay);
			}

			static const char *s_movementModeStrings[] = {
				"Stationary",
				"Random Movement",
				"Patrol"};

			if (ImGui::BeginCombo("Movement", s_movementModeStrings[selectable.GetEntry().movement()]))
			{
				uint32 index = 0;
				for (const auto &mode : s_movementModeStrings)
				{
					if (ImGui::Selectable(mode, selectable.GetEntry().movement() == index))
					{
						selectable.GetEntry().set_movement(static_cast<proto::UnitSpawnEntry_MovementType>(index));
					}

					index++;
				}

				ImGui::EndCombo();
			}

			// --- Patrol Waypoints section (only when PATROL movement is selected) ---
			if (selectable.GetEntry().movement() == proto::UnitSpawnEntry_MovementType_PATROL)
			{
				ImGui::Separator();
				ImGui::Text("Patrol Waypoints");
				ImGui::Separator();
				const bool editActive = m_spawnEditMode->IsWaypointEditActive();
			if (editActive)
			{
				ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
				if (ImGui::Button("Exit Waypoint Edit", ImVec2(-1, 0)))
					m_spawnEditMode->SetWaypointEditActive(false);
				ImGui::PopStyleColor(3);
				ImGui::TextDisabled("Click viewport to add/drag waypoints. [Esc] to exit.");
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.9f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
				if (ImGui::Button("Edit Patrol Waypoints", ImVec2(-1, 0)))
					m_spawnEditMode->SetWaypointEditActive(true);
				ImGui::PopStyleColor(3);
			}

				bool waypointChanged = false;
				int removeIndex = -1;

				for (int i = 0; i < selectable.GetEntry().waypoints_size(); ++i)
				{
					auto* wp = selectable.GetEntry().mutable_waypoints(i);

					ImGui::PushID(i);

					ImGui::Text("[%d]", i + 1);
					ImGui::SameLine();

					float waitTime = static_cast<float>(wp->waittime());
					ImGui::SetNextItemWidth(120.0f);
					if (ImGui::InputFloat("Wait (ms)", &waitTime, 0.0f, 0.0f, "%.0f"))
					{
						wp->set_waittime(static_cast<uint32>(waitTime));
						waypointChanged = true;
					}

					ImGui::SameLine();

					if (ImGui::SmallButton("X"))
					{
						removeIndex = i;
					}

					ImGui::PopID();
				}

				if (removeIndex >= 0)
				{
					selectable.GetEntry().mutable_waypoints()->erase(
						selectable.GetEntry().mutable_waypoints()->begin() + removeIndex);
					waypointChanged = true;
				}

				if (selectable.GetEntry().waypoints_size() > 0)
				{
					if (ImGui::Button("Clear All"))
					{
						selectable.GetEntry().mutable_waypoints()->Clear();
						waypointChanged = true;
					}
				}

				if (waypointChanged)
				{
					m_spawnEditMode->RebuildWaypointVisualization();
				}
			}

			ImGui::Separator();
			ImGui::Text("Additional Spawn Triggers");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("These triggers fire in addition to the triggers defined on the creature template.");
			}

			const int triggerCount = m_editor.GetProject().triggers.count();

			// List current additional triggers with remove buttons
			int removeIdx = -1;
			for (int t = 0; t < selectable.GetEntry().additional_trigger_ids_size(); ++t)
			{
				const uint32 tid = selectable.GetEntry().additional_trigger_ids(t);
				const auto* te = m_editor.GetProject().triggers.getById(tid);
				ImGui::PushID(t);
				ImGui::Text("%s", te ? te->name().c_str() : "(unknown)");
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
				{
					removeIdx = t;
				}
				ImGui::PopID();
			}
			if (removeIdx >= 0)
			{
				selectable.GetEntry().mutable_additional_trigger_ids()->erase(
					selectable.GetEntry().mutable_additional_trigger_ids()->begin() + removeIdx);
			}

			static int s_addSpawnTriggerIdx = 0;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50.0f);
			ImGui::Combo("##AddSpawnTrigger2", &s_addSpawnTriggerIdx,
				[](void* data, int idx, const char** out_text) -> bool
				{
					const auto* triggers = static_cast<proto::Triggers*>(data);
					if (idx < 0 || idx >= triggers->entry_size()) { return false; }
					*out_text = triggers->entry(idx).name().c_str();
					return true;
				},
				&m_editor.GetProject().triggers.getTemplates(), triggerCount, -1);
			ImGui::SameLine();
			if (ImGui::SmallButton("Add") && triggerCount > 0)
			{
				selectable.GetEntry().add_additional_trigger_ids(
					m_editor.GetProject().triggers.getTemplates().entry(s_addSpawnTriggerIdx).id());
			}
		}
	}

	void WorldEditorInstance::Visit(SelectedObjectSpawn &selectable)
	{
		if (ImGui::CollapsingHeader("Object Spawn"))
		{
			// Optional name used for trigger target resolution (NamedWorldObject).
			ImGui::InputText("Spawn Name", selectable.GetEntry().mutable_name());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Optional unique name for this spawn point.\nUsed by trigger actions that target a 'Named World Object'.");

			proto::ObjectEntry *selectedObject = m_editor.GetProject().objects.getById(selectable.GetEntry().objectentry());
			if (ImGui::BeginCombo("Object", selectedObject ? selectedObject->name().c_str() : "(None)"))
			{
				for (const auto &object : m_editor.GetProject().objects.getTemplates().entry())
				{
					ImGui::PushID(object.id());
					if (ImGui::Selectable(object.name().c_str(), &object == selectedObject))
					{
						selectable.GetEntry().set_objectentry(object.id());
						selectable.RefreshEntity();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			bool isActive = selectable.GetEntry().isactive();
			if (ImGui::Checkbox("Is Active", &isActive))
			{
				selectable.GetEntry().set_isactive(isActive);
			}

			bool respawn = selectable.GetEntry().respawn();
			if (ImGui::Checkbox("Respawn", &respawn))
			{
				selectable.GetEntry().set_respawn(respawn);
			}

			uint64 respawnDelay = selectable.GetEntry().respawndelay();
			if (ImGui::InputScalar("Respawn Delay (ms)", ImGuiDataType_U64, &respawnDelay))
			{
				selectable.GetEntry().set_respawndelay(respawnDelay);
			}

			uint32 maxCount = selectable.GetEntry().maxcount();
			if (ImGui::InputScalar("Max Count", ImGuiDataType_U32, &maxCount))
			{
				selectable.GetEntry().set_maxcount(maxCount);
			}

			// Doors get a readable Closed/Open selector — note the proto default is 1 (open),
			// so doors placed with defaults would spawn open unless set to Closed here.
			if (selectedObject && selectedObject->type() == game_world_object_type::Door)
			{
				const uint32 state = selectable.GetEntry().state();
				const char* statePreview = (state == 0) ? "Closed" : "Open";
				if (ImGui::BeginCombo("State", statePreview))
				{
					if (ImGui::Selectable("Closed", state == 0))
					{
						selectable.GetEntry().set_state(0);
					}
					if (ImGui::Selectable("Open", state != 0))
					{
						selectable.GetEntry().set_state(1);
					}
					ImGui::EndCombo();
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Initial door state on spawn. Closed doors block movement and line of sight.");
			}
			else
			{
				uint32 state = selectable.GetEntry().state();
				if (ImGui::InputScalar("State", ImGuiDataType_U32, &state))
				{
					selectable.GetEntry().set_state(state);
				}
			}

			// Allow editing animation progress
			uint32 animProgress = selectable.GetEntry().animprogress();
			if (ImGui::SliderInt("Animation Progress", (int *)&animProgress, 0, 100))
			{
				selectable.GetEntry().set_animprogress(animProgress);
			}

			// Per-spawn loot entry override (0 = use base ObjectEntry.objectlootentry)
			uint32 lootEntry = selectable.GetEntry().loot_entry();
			if (ImGui::InputScalar("Loot Entry", ImGuiDataType_U32, &lootEntry))
			{
				selectable.GetEntry().set_loot_entry(lootEntry);
			}

			// Per-spawn trigger override
			// Show base trigger count from ObjectEntry as read-only info
			const proto::ObjectEntry* objEntry = m_editor.GetProject().objects.getById(selectable.GetEntry().objectentry());
			if (objEntry && objEntry->triggers_size() > 0)
			{
				ImGui::TextDisabled("Base: %d trigger(s)", objEntry->triggers_size());
			}

			uint32 triggerId = selectable.GetEntry().trigger_id();
			if (ImGui::InputScalar("Trigger ID", ImGuiDataType_U32, &triggerId))
			{
				selectable.GetEntry().set_trigger_id(triggerId);
			}
		}
	}

	void WorldEditorInstance::Visit(SelectedFogVolume &selectable)
	{
		if (ImGui::CollapsingHeader("Fog Volume", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Fog Volume #%u", selectable.GetVolumeId());
			ImGui::TextDisabled("Edit its properties in the Fog Volumes mode panel above.");
		}
	}

	void WorldEditorInstance::Visit(SelectedAreaTrigger &selectable)
	{
		if (ImGui::CollapsingHeader("Area Trigger", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Name
			char nameBuf[256];
			strncpy_s(nameBuf, selectable.GetEntry().name().c_str(), sizeof(nameBuf) - 1);
			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
			{
				selectable.GetEntry().set_name(nameBuf);
			}

			// ID (read-only)
			uint32 id = selectable.GetEntry().id();
			ImGui::BeginDisabled();
			ImGui::InputScalar("ID", ImGuiDataType_U32, &id, nullptr, nullptr, "%u", ImGuiInputTextFlags_ReadOnly);
			ImGui::EndDisabled();

			// Map (read-only)
			uint32 mapId = selectable.GetEntry().map();
			ImGui::BeginDisabled();
			ImGui::InputScalar("Map", ImGuiDataType_U32, &mapId, nullptr, nullptr, "%u", ImGuiInputTextFlags_ReadOnly);
			ImGui::EndDisabled();

			ImGui::Separator();

			// Position
			Vector3 pos = selectable.GetPosition();
			if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
			{
				selectable.SetPosition(pos);
			}

			// Type-specific properties
			const bool isSphere = selectable.GetEntry().has_radius();
			if (isSphere)
			{
				// Sphere properties
				float radius = selectable.GetEntry().radius();
				if (ImGui::DragFloat("Radius", &radius, 0.1f, 0.1f, 1000.0f))
				{
					selectable.GetEntry().set_radius(radius);
					selectable.RefreshVisual();
				}
			}
			else
			{
				// Box properties
				float boxX = selectable.GetEntry().box_x();
				float boxY = selectable.GetEntry().box_y();
				float boxZ = selectable.GetEntry().box_z();

				if (ImGui::DragFloat("Box Width (X)", &boxX, 0.1f, 0.1f, 1000.0f))
				{
					selectable.GetEntry().set_box_x(boxX);
					selectable.RefreshVisual();
				}
				if (ImGui::DragFloat("Box Height (Y)", &boxY, 0.1f, 0.1f, 1000.0f))
				{
					selectable.GetEntry().set_box_y(boxY);
					selectable.RefreshVisual();
				}
				if (ImGui::DragFloat("Box Depth (Z)", &boxZ, 0.1f, 0.1f, 1000.0f))
				{
					selectable.GetEntry().set_box_z(boxZ);
					selectable.RefreshVisual();
				}

				// Orientation
				float orientationDeg = selectable.GetEntry().has_box_o() ? Degree(Radian(selectable.GetEntry().box_o())).GetValueDegrees() : 0.0f;
				if (ImGui::DragFloat("Orientation (Y)", &orientationDeg, 1.0f, 0.0f, 360.0f))
				{
					selectable.GetEntry().set_box_o(Degree(orientationDeg).GetValueRadians());
					selectable.SetOrientation(Quaternion(Degree(orientationDeg), Vector3::UnitY));
				}
			}

			ImGui::Separator();

			// Script trigger
			if (selectable.GetEntry().has_on_enter_trigger())
			{
				uint32 scriptId = selectable.GetEntry().on_enter_trigger();
				if (ImGui::InputScalar("On Enter Script", ImGuiDataType_U32, &scriptId))
				{
					selectable.GetEntry().set_on_enter_trigger(scriptId);
				}
			}
			else
			{
				if (ImGui::Button("Add On Enter Script"))
				{
					selectable.GetEntry().set_on_enter_trigger(0);
				}
			}
		}
	}

	bool WorldEditorInstance::ReadMVERChunk(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *versionChunk);

		// Read chunk only once
		RemoveChunkHandler(*versionChunk);

		m_worldFileVersion = 0;
		if (!(reader >> m_worldFileVersion))
		{
			ELOG("Failed to read version chunk!");
			return false;
		}

		if (m_worldFileVersion < 1 || m_worldFileVersion > 3)
		{
			ELOG("Detected unsuppoted file format version!");
			return false;
		}

		AddChunkHandler(*meshChunk, false, *this, &WorldEditorInstance::ReadMeshChunk);
		AddChunkHandler(*terrainChunk, false, *this, &WorldEditorInstance::ReadTerrainChunk);

		return reader;
	}

	bool WorldEditorInstance::ReadMeshChunk(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *meshChunk);

		// Only when we have read mesh names we support reading entity chunks otherwise entities would refer to meshes which we don't know about!
		if (m_worldFileVersion == 2)
		{
			AddChunkHandler(*entityChunk, false, *this, &WorldEditorInstance::ReadEntityChunkV2);
		}
		else if (m_worldFileVersion < 2)
		{
			AddChunkHandler(*entityChunk, false, *this, &WorldEditorInstance::ReadEntityChunk);
		}

		// Read chunk only once
		RemoveChunkHandler(*meshChunk);

		m_meshNames.clear();
		if (chunkSize > 0)
		{
			const size_t contentStart = reader.getSource()->position();
			while (reader.getSource()->position() - contentStart < chunkSize)
			{
				String meshName;
				if (!(reader >> io::read_string(meshName)))
				{
					ELOG("Failed to read world file: Unexpected end of file");
					return false;
				}

				m_meshNames.emplace_back(std::move(meshName));
			}
		}

		return reader;
	}

	bool WorldEditorInstance::ReadEntityChunk(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *entityChunk);

		if (chunkSize != sizeof(MapEntityChunkContent))
		{
			ELOG("Failed to read world file: Invalid entity chunk size");
			return false;
		}

		MapEntityChunkContent content;
		reader.readPOD(content);
		if (!reader)
		{
			ELOG("Failed to read world file: Unexpected end of file");
			return false;
		}

		if (content.meshNameIndex >= m_meshNames.size())
		{
			ELOG("Failed to read world file: Invalid mesh name index");
			return false;
		}

		if (content.uniqueId == 0)
		{
			content.uniqueId = m_entityFactory->GenerateUniqueId();
		}
		else
		{
			m_entityFactory->NotifyExistingId(content.uniqueId);
		}

		CreateMapEntity(m_meshNames[content.meshNameIndex], content.position, content.rotation, content.scale, content.uniqueId);

		return reader;
	}

	bool WorldEditorInstance::ReadEntityChunkV2(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *entityChunk);

		if (m_meshNames.empty())
		{
			ELOG("No mesh names known, can't read entity chunks before mesh chunk!");
			return false;
		}

		uint32 uniqueId;
		uint32 meshNameIndex;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale;
		if (!(reader >> io::read<uint32>(uniqueId) >> io::read<uint32>(meshNameIndex) >> io::read<float>(position.x) >> io::read<float>(position.y) >> io::read<float>(position.z) >> io::read<float>(rotation.w) >> io::read<float>(rotation.x) >> io::read<float>(rotation.y) >> io::read<float>(rotation.z) >> io::read<float>(scale.x) >> io::read<float>(scale.y) >> io::read<float>(scale.z)))
		{
			ELOG("Failed to read map entity chunk content, unexpected end of file!");
			return false;
		}

		if (meshNameIndex >= m_meshNames.size())
		{
			ELOG("Map entity chunk references unknown mesh names!");
			return false;
		}

		struct MaterialOverride
		{
			uint8 materialIndex;
			String materialName;
		};

		std::vector<MaterialOverride> materialOverrides;

		uint8 numMaterialOverrides;
		if (!(reader >> io::read<uint8>(numMaterialOverrides)))
		{
			ELOG("Failed to read material override count for map entity chunk, unexpected end of file!");
			return false;
		}

		materialOverrides.resize(numMaterialOverrides);
		for (uint8 i = 0; i < numMaterialOverrides; ++i)
		{
			if (!(reader >> io::read<uint8>(materialOverrides[i].materialIndex) >> io::read_container<uint16>(materialOverrides[i].materialName)))
			{
				ELOG("Failed to read material override for map entity chunk, unexpected end of file!");
				return false;
			}
		}

		if (uniqueId == 0)
		{
			uniqueId = m_entityFactory->GenerateUniqueId();
		}
		else
		{
			m_entityFactory->NotifyExistingId(uniqueId);
		}

		if (Entity *entity = CreateMapEntity(m_meshNames[meshNameIndex], position, rotation, scale, uniqueId))
		{
			// Apply material overrides
			for (const auto &materialOverride : materialOverrides)
			{
				if (materialOverride.materialIndex >= entity->GetNumSubEntities())
				{
					WLOG("Entity has material override for material index greater than entity material count! Skipping material override");
					continue;
				}

				entity->GetSubEntity(materialOverride.materialIndex)->SetMaterial(MaterialManager::Get().Load(materialOverride.materialName));
			}
		}

		return reader;
	}

	bool WorldEditorInstance::ReadTerrainChunk(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *terrainChunk);

		// Read chunk only once
		RemoveChunkHandler(*terrainChunk);

		String defaultTerrainMaterial;
		reader >> io::read<uint8>(m_hasTerrain) >> io::read_container<uint16>(defaultTerrainMaterial);

		if (!defaultTerrainMaterial.empty())
		{
			m_terrain->SetDefaultMaterial(MaterialManager::Get().Load(defaultTerrainMaterial));
			if (!m_terrain->GetDefaultMaterial())
			{
				WLOG("Failed to load referenced terrain default material!");
			}
		}
		else if (m_hasTerrain)
		{
			WLOG("Terrain is enabled but terrain has no default material set!");
		}

		return reader;
	}

	bool WorldEditorInstance::OnReadFinished()
	{
		m_meshNames.clear();
		RemoveAllChunkHandlers();

		return ChunkReader::OnReadFinished();
	}

	void WorldEditorInstance::SetEditMode(WorldEditMode *editMode)
	{
		if (editMode == m_editMode)
		{
			return;
		}

		if (m_editMode)
		{
			m_editMode->OnDeactivate();
		}

		m_editMode = editMode;

		if (m_editMode)
		{
			m_editMode->OnActivate();
		}
	}

	void WorldEditorInstance::ClearSelection()
	{
		m_debugBoundingBox->SetVisible(false);
		m_selection.Clear();

		// Clear spawn edit mode's selected spawn so waypoint visualization is removed.
		if (m_spawnEditMode)
		{
			m_spawnEditMode->SetSelectedSpawn(nullptr);
		}
	}

	bool WorldEditorInstance::IsTransforming() const
	{
		return m_transformWidget && m_transformWidget->IsActive();
	}

	ManualRenderObject* WorldEditorInstance::CreateManualRenderObject(const String& name)
	{
		return m_scene.CreateManualRenderObject(name);
	}

	SceneNode* WorldEditorInstance::CreateChildSceneNode()
	{
		return m_scene.GetRootSceneNode().CreateChildSceneNode();
	}

	void WorldEditorInstance::DestroyManualRenderObject(const ManualRenderObject& obj)
	{
		m_scene.DestroyManualRenderObject(obj);
	}

	void WorldEditorInstance::DestroySceneNode(const SceneNode& node)
	{
		m_scene.DestroySceneNode(node);
	}

	uint32 WorldEditorInstance::GenerateFogVolumeId()
	{
		uint32 maxId = 0;
		for (const FogVolume& volume : m_fogVolumes)
		{
			maxId = std::max(maxId, volume.id);
		}

		return maxId + 1;
	}

	void WorldEditorInstance::AddFogVolumeVisual(const uint32 volumeId, const bool select)
	{
		const auto volumeIt = std::find_if(m_fogVolumes.begin(), m_fogVolumes.end(), [volumeId](const FogVolume& volume)
		{
			return volume.id == volumeId;
		});
		if (volumeIt == m_fogVolumes.end())
		{
			return;
		}

		// Never leave a second visual behind for the same volume.
		RemoveFogVolumeVisual(volumeId);

		ManualRenderObject* renderObject = m_scene.CreateManualRenderObject("FogVolume_" + std::to_string(volumeId));
		renderObject->SetCastShadows(false);
		renderObject->SetQueryFlags(SceneQueryFlags_FogVolumes);
		BuildFogVolumeWireframe(*renderObject, *volumeIt);

		// Same transform the renderer applies: world = Quaternion(Degree(yaw), UnitY) * local + position.
		SceneNode* node = m_scene.GetRootSceneNode().CreateChildSceneNode(volumeIt->position);
		node->SetOrientation(Quaternion(Degree(volumeIt->yaw), Vector3::UnitY));
		node->AttachObject(*renderObject);

		m_fogVolumeVisuals[volumeId] = { node, renderObject };

		if (select)
		{
			SelectFogVolume(volumeId);
		}
	}

	void WorldEditorInstance::RefreshFogVolumeVisual(const uint32 volumeId)
	{
		const auto visualIt = m_fogVolumeVisuals.find(volumeId);
		if (visualIt == m_fogVolumeVisuals.end())
		{
			return;
		}

		const auto volumeIt = std::find_if(m_fogVolumes.begin(), m_fogVolumes.end(), [volumeId](const FogVolume& volume)
		{
			return volume.id == volumeId;
		});
		if (volumeIt == m_fogVolumes.end())
		{
			return;
		}

		auto [node, renderObject] = visualIt->second;
		BuildFogVolumeWireframe(*renderObject, *volumeIt);
		node->SetPosition(volumeIt->position);
		node->SetOrientation(Quaternion(Degree(volumeIt->yaw), Vector3::UnitY));

		// Let the transform widget follow edits made in the details panel.
		if (SelectedFogVolume* selected = GetSelectedFogVolume(); selected && selected->GetVolumeId() == volumeId)
		{
			selected->NotifyTransformChanged();
		}
	}

	void WorldEditorInstance::RemoveFogVolumeVisual(const uint32 volumeId)
	{
		const auto visualIt = m_fogVolumeVisuals.find(volumeId);
		if (visualIt == m_fogVolumeVisuals.end())
		{
			return;
		}

		auto [node, renderObject] = visualIt->second;
		m_scene.DestroyManualRenderObject(*renderObject);
		m_scene.GetRootSceneNode().RemoveChild(*node);
		m_scene.DestroySceneNode(*node);

		m_fogVolumeVisuals.erase(visualIt);
	}

	void WorldEditorInstance::RemoveAllFogVolumeVisuals()
	{
		// A selected fog volume references its node and render object, so drop it first. Checked against
		// the whole selection, not just the last entry: a ctrl-click in the scene outline can append a
		// SelectedMapEntity onto a fog volume selection without clearing it, and destroying the visuals
		// underneath a SelectedFogVolume that is still part of that mixed selection would leave it
		// dangling (use-after-free on the next gizmo move/scale).
		if (SelectionHasFogVolume(nullptr))
		{
			ClearSelection();
		}

		while (!m_fogVolumeVisuals.empty())
		{
			RemoveFogVolumeVisual(m_fogVolumeVisuals.begin()->first);
		}
	}

	void WorldEditorInstance::SelectFogVolume(const uint32 volumeId)
	{
		const auto visualIt = m_fogVolumeVisuals.find(volumeId);
		if (visualIt == m_fogVolumeVisuals.end())
		{
			return;
		}

		auto [node, renderObject] = visualIt->second;

		m_selection.Clear();
		m_debugBoundingBox->Clear();

		// Runs from inside SelectedFogVolume::Remove() while the viewport iterates the selection, so it
		// must not touch the selection; the caller clears it afterwards. SelectedFogVolume::Deselect()
		// does not touch the destroyed node or render object.
		auto removal = [this](const uint32 id)
		{
			EraseFogVolume(id);
		};

		auto duplication = [this](Selectable& selectable)
		{
			// Alt + transform: leave a copy of the volume behind at the current transform while the
			// original keeps following the widget, like map entity duplication.
			const auto* selected = dynamic_cast<SelectedFogVolume*>(&selectable);
			if (!selected)
			{
				return;
			}

			const auto sourceIt = std::find_if(m_fogVolumes.begin(), m_fogVolumes.end(), [id = selected->GetVolumeId()](const FogVolume& volume)
			{
				return volume.id == id;
			});
			if (sourceIt == m_fogVolumes.end())
			{
				return;
			}

			FogVolume copy = *sourceIt;
			copy.id = GenerateFogVolumeId();
			m_fogVolumes.push_back(copy);
			MarkFogVolumesChanged();
			AddFogVolumeVisual(copy.id, false);
		};

		m_selection.AddSelectable(std::make_unique<SelectedFogVolume>(volumeId, m_fogVolumes, *this, *node, *renderObject, duplication, removal));
	}

	void WorldEditorInstance::RemoveFogVolume(const uint32 volumeId)
	{
		// The selectable references the visual about to be destroyed, so drop it first. Checked against
		// the whole selection (see SelectionHasFogVolume), not just the last entry, so a mixed selection
		// containing this volume plus e.g. a scene-outline-picked map entity doesn't leave a dangling
		// SelectedFogVolume behind.
		if (SelectionHasFogVolume(&volumeId))
		{
			ClearSelection();
		}

		EraseFogVolume(volumeId);
	}

	void WorldEditorInstance::EraseFogVolume(const uint32 volumeId)
	{
		RemoveFogVolumeVisual(volumeId);

		const auto volumeIt = std::find_if(m_fogVolumes.begin(), m_fogVolumes.end(), [volumeId](const FogVolume& volume)
		{
			return volume.id == volumeId;
		});
		if (volumeIt != m_fogVolumes.end())
		{
			m_fogVolumes.erase(volumeIt);
			MarkFogVolumesChanged();
		}
	}

	uint32 WorldEditorInstance::GetSelectedFogVolumeId() const
	{
		const SelectedFogVolume* selected = GetSelectedFogVolume();
		return selected ? selected->GetVolumeId() : 0;
	}

	SelectedFogVolume* WorldEditorInstance::GetSelectedFogVolume() const
	{
		if (m_selection.IsEmpty())
		{
			return nullptr;
		}

		// Only the most recently added selection entry counts here; see the header doc comment for why
		// a fog volume elsewhere in a mixed selection is intentionally not reported by this method.
		return dynamic_cast<SelectedFogVolume*>(m_selection.GetSelectedObjects().back().get());
	}

	bool WorldEditorInstance::SelectionHasFogVolume(const uint32* volumeId) const
	{
		for (const auto& selectable : m_selection.GetSelectedObjects())
		{
			if (const auto* fogVolume = dynamic_cast<SelectedFogVolume*>(selectable.get()))
			{
				if (!volumeId || fogVolume->GetVolumeId() == *volumeId)
				{
					return true;
				}
			}
		}

		return false;
	}

	MapEntity *WorldEditorInstance::FindMapEntity(const uint64 uniqueId) const
	{
		for (const auto &mapEntity : m_mapEntities)
		{
			if (mapEntity->GetUniqueId() == uniqueId)
			{
				return mapEntity.get();
			}
		}

		return nullptr;
	}

	PropKitPtr WorldEditorInstance::ResolveKit(const String &kitPath) const
	{
		const String key = MakeKitKey(kitPath);
		if (const auto it = m_dirtyKits.find(key); it != m_dirtyKits.end())
		{
			return it->second.kit;
		}

		return PropKitManager::Get().Find(key);
	}

	PropKitPtr WorldEditorInstance::LoadKit(const String &kitPath)
	{
		const String key = MakeKitKey(kitPath);
		if (const auto it = m_dirtyKits.find(key); it != m_dirtyKits.end())
		{
			return it->second.kit;
		}

		PropKitPtr kit = PropKitManager::Get().Load(key);
		if (kit)
		{
			m_observedKits[key] = kit;
		}

		return kit;
	}

	const WorldModelMeshRef *WorldEditorInstance::FindKitProp(const String &kitPath, const size_t propIndex) const
	{
		const PropKitPtr kit = ResolveKit(kitPath);
		if (!kit || propIndex >= kit->props.size())
		{
			return nullptr;
		}

		return &kit->props[propIndex];
	}

	PropKit *WorldEditorInstance::GetEditableKit(const String &kitPath, const uint64 originPlacementId)
	{
		const String key = MakeKitKey(kitPath);
		if (const auto it = m_dirtyKits.find(key); it != m_dirtyKits.end())
		{
			it->second.originPlacementId = originPlacementId;
			it->second.editingPlacementIds.insert(originPlacementId);
			return it->second.kit.get();
		}

		// The cached kit is shared with the prop kit editor and other worlds, so edits go into a copy
		// that only this world shows until it is saved.
		const PropKitPtr cached = PropKitManager::Get().Load(key);
		if (!cached)
		{
			ELOG("Unable to edit prop kit " << key << ": the kit could not be loaded");
			return nullptr;
		}

		DirtyKit &dirty = m_dirtyKits[key];
		dirty.kit = std::make_shared<PropKit>(*cached);
		dirty.originPlacementId = originPlacementId;
		dirty.editingPlacementIds = { originPlacementId };
		return dirty.kit.get();
	}

	void WorldEditorInstance::ApplyKits(MapEntity &placement)
	{
		WorldModelInstance *instance = placement.GetWorldModelInstance();
		if (!instance)
		{
			return;
		}

		// The paths are kept even when a kit fails to load, so a save does not silently drop them.
		std::vector<PropKitPtr> kits;
		kits.reserve(placement.GetKitPaths().size());
		for (const String &kitPath : placement.GetKitPaths())
		{
			if (PropKitPtr kit = LoadKit(kitPath))
			{
				kits.push_back(std::move(kit));
			}
			else
			{
				WLOG("Failed to load prop kit " << kitPath << " for world model " << placement.GetUniqueId());
			}
		}

		instance->SetPropKits(std::move(kits));
		m_kitCollisionDirty = true;
	}

	void WorldEditorInstance::RefreshKitUsers(const String &kitPath)
	{
		for (const auto &mapEntity : m_mapEntities)
		{
			if (!mapEntity->IsWorldModelEntity())
			{
				continue;
			}

			const auto &kitPaths = mapEntity->GetKitPaths();
			if (std::any_of(kitPaths.begin(), kitPaths.end(), [&kitPath](const String &path) { return KitPathsEqual(path, kitPath); }))
			{
				ApplyKits(*mapEntity);
			}
		}
	}

	size_t WorldEditorInstance::CountKitUsers(const String &kitPath, const uint64 excludePlacementId) const
	{
		size_t count = 0;
		for (const auto &mapEntity : m_mapEntities)
		{
			if (!mapEntity->IsWorldModelEntity() || (excludePlacementId != 0 && mapEntity->GetUniqueId() == excludePlacementId))
			{
				continue;
			}

			const auto &kitPaths = mapEntity->GetKitPaths();
			if (std::any_of(kitPaths.begin(), kitPaths.end(), [&kitPath](const String &path) { return KitPathsEqual(path, kitPath); }))
			{
				++count;
			}
		}

		return count;
	}

	String WorldEditorInstance::GetActiveKit(const MapEntity &placement) const
	{
		const auto &kitPaths = placement.GetKitPaths();

		if (const auto it = m_activeKits.find(placement.GetUniqueId()); it != m_activeKits.end())
		{
			// "None" was picked explicitly
			if (it->second.empty())
			{
				return String();
			}

			for (const String &kitPath : kitPaths)
			{
				if (KitPathsEqual(kitPath, it->second))
				{
					return kitPath;
				}
			}

			// The picked kit was removed from the list; fall back to the default.
		}

		return kitPaths.empty() ? String() : kitPaths.front();
	}

	MapEntity *WorldEditorInstance::GetSelectedPlacement() const
	{
		if (m_selection.IsEmpty())
		{
			return nullptr;
		}

		Selectable *selected = m_selection.GetSelectedObjects().back().get();
		if (const auto *selectedEntity = dynamic_cast<SelectedMapEntity *>(selected))
		{
			MapEntity &entity = selectedEntity->GetEntity();
			return entity.IsWorldModelEntity() ? &entity : nullptr;
		}

		if (const auto *selectedProp = dynamic_cast<SelectedKitProp *>(selected))
		{
			return FindMapEntity(selectedProp->GetPlacementId());
		}

		return nullptr;
	}

	void WorldEditorInstance::EditKitProp(const uint64 placementId, const String &kitPath, const size_t propIndex, const std::function<void(WorldModelMeshRef &)> &edit)
	{
		PropKit *kit = GetEditableKit(kitPath, placementId);
		if (!kit || propIndex >= kit->props.size())
		{
			return;
		}

		edit(kit->props[propIndex]);

		RefreshKitUsers(kitPath);
		UpdateSelectedKitPropBounds();
	}

	void WorldEditorInstance::DuplicateKitProp(const uint64 placementId, const String &kitPath, const size_t propIndex)
	{
		PropKit *kit = GetEditableKit(kitPath, placementId);
		if (!kit || propIndex >= kit->props.size())
		{
			return;
		}

		// Copy first: push_back may reallocate the vector the source lives in.
		const WorldModelMeshRef copy = kit->props[propIndex];
		kit->props.push_back(copy);

		RefreshKitUsers(kitPath);
	}

	void WorldEditorInstance::QueueKitPropRemoval(const uint64 placementId, const String &kitPath, const size_t propIndex)
	{
		m_pendingKitPropRemovals.push_back({placementId, kitPath, propIndex});
	}

	void WorldEditorInstance::ApplyPendingKitPropRemovals()
	{
		if (m_pendingKitPropRemovals.empty())
		{
			return;
		}

		// Highest index first, so earlier removals do not shift the props still queued.
		std::vector<PendingKitPropRemoval> removals = std::move(m_pendingKitPropRemovals);
		m_pendingKitPropRemovals.clear();
		std::sort(removals.begin(), removals.end(), [](const PendingKitPropRemoval &a, const PendingKitPropRemoval &b)
		{
			return a.propIndex > b.propIndex;
		});

		std::vector<String> changedKits;
		std::set<std::pair<String, size_t>> removed;
		for (const PendingKitPropRemoval &removal : removals)
		{
			const String key = MakeKitKey(removal.kitPath);
			String lowerKey = key;
			std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			if (!removed.emplace(lowerKey, removal.propIndex).second)
			{
				continue;
			}

			PropKit *kit = GetEditableKit(key, removal.placementId);
			if (!kit || removal.propIndex >= kit->props.size())
			{
				continue;
			}

			kit->props.erase(kit->props.begin() + static_cast<std::ptrdiff_t>(removal.propIndex));
			if (std::none_of(changedKits.begin(), changedKits.end(), [&key](const String &path) { return KitPathsEqual(path, key); }))
			{
				changedKits.push_back(key);
			}
		}

		// Prop indices of the changed kits are stale now, so drop selected props of those kits.
		const bool selectsChangedKit = std::any_of(m_selection.GetSelectedObjects().begin(), m_selection.GetSelectedObjects().end(), [&changedKits](const auto &selectable)
		{
			const auto *selectedProp = dynamic_cast<SelectedKitProp *>(selectable.get());
			return selectedProp && std::any_of(changedKits.begin(), changedKits.end(), [selectedProp](const String &path) { return KitPathsEqual(path, selectedProp->GetKitPath()); });
		});
		if (selectsChangedKit)
		{
			ClearSelection();
		}

		for (const String &kitPath : changedKits)
		{
			RefreshKitUsers(kitPath);
		}
	}

	bool WorldEditorInstance::TrySelectKitProp(const float viewportX, const float viewportY, const bool allowMultiSelect)
	{
		MapEntity *placement = GetSelectedPlacement();
		if (!placement || placement->GetKitPaths().empty())
		{
			return false;
		}

		const Ray ray = m_camera->GetCameraToViewportRay(viewportX, viewportY, 10000.0f);

		String bestKit;
		size_t bestIndex = 0;
		float bestDistance = std::numeric_limits<float>::max();

		for (const String &kitPath : placement->GetKitPaths())
		{
			const PropKitPtr kit = ResolveKit(kitPath);
			if (!kit)
			{
				continue;
			}

			for (size_t i = 0; i < kit->props.size(); ++i)
			{
				const WorldModelMeshRef &prop = kit->props[i];
				if (!prop.visible || prop.meshPath.empty())
				{
					continue;
				}

				const MeshPtr mesh = MeshManager::Get().Load(prop.meshPath);
				if (!mesh)
				{
					continue;
				}

				// Test the ray against the mesh bounds in the prop's local space (an oriented box).
				const Matrix4 transform = MakeKitPropWorldTransform(placement->GetSceneNode(), prop);
				if (!transform.IsAffine())
				{
					continue;
				}

				const Matrix4 inverse = transform.InverseAffine();
				const Vector3 localStart = inverse.TransformAffine(ray.origin);
				const Vector3 localEnd = inverse.TransformAffine(ray.destination);
				if (localStart == localEnd)
				{
					continue;
				}

				const Ray localRay(localStart, localEnd);
				const auto [hit, localDistance] = localRay.IntersectsAABB(mesh->GetBounds());
				if (!hit)
				{
					continue;
				}

				const Vector3 hitPoint = transform.TransformAffine(localRay.GetPoint(std::max(localDistance, 0.0f)));
				const float distance = (hitPoint - ray.origin).GetLength();
				if (distance < bestDistance)
				{
					bestDistance = distance;
					bestKit = kitPath;
					bestIndex = i;
				}
			}
		}

		if (bestKit.empty())
		{
			return false;
		}

		// A wall in front of the prop wins: compare with the nearest real geometry under the cursor.
		// A prop that is itself realized as an entity is hit at or behind its own bounds, so it never
		// hides itself.
		constexpr float occlusionTolerance = 0.05f;
		if (bestDistance > m_selectionRaycaster->GetClosestEntityHitDistance(viewportX, viewportY) + occlusionTolerance)
		{
			return false;
		}

		if (!allowMultiSelect)
		{
			m_selection.Clear();
		}

		m_selection.AddSelectable(std::make_unique<SelectedKitProp>(*this, placement->GetUniqueId(), bestKit, bestIndex));
		UpdateSelectedKitPropBounds();
		return true;
	}

	void WorldEditorInstance::UpdateSelectedKitPropBounds()
	{
		if (m_selection.IsEmpty())
		{
			return;
		}

		const auto *selectedProp = dynamic_cast<SelectedKitProp *>(m_selection.GetSelectedObjects().back().get());
		if (!selectedProp)
		{
			return;
		}

		const MapEntity *placement = FindMapEntity(selectedProp->GetPlacementId());
		const WorldModelMeshRef *prop = FindKitProp(selectedProp->GetKitPath(), selectedProp->GetPropIndex());
		if (!placement || !prop)
		{
			return;
		}

		const MeshPtr mesh = MeshManager::Get().Load(prop->meshPath);
		if (!mesh)
		{
			return;
		}

		AABB bounds = mesh->GetBounds();
		bounds.Transform(MakeKitPropWorldTransform(placement->GetSceneNode(), *prop));
		m_selectionRaycaster->UpdateDebugAABB(bounds);
		m_debugBoundingBox->SetVisible(true);
	}

	void WorldEditorInstance::UpdateKitCollisionOverlay()
	{
		const MapEntity *placement = GetSelectedPlacement();
		const uint64 placementId = placement ? placement->GetUniqueId() : 0;
		if (placementId != m_kitCollisionPlacementId)
		{
			// A different (or no) building is selected: its display starts switched off.
			m_kitCollisionPlacementId = placementId;
			m_showKitCollision = false;
		}

		if (!m_showKitCollision || !placement)
		{
			if (m_kitCollisionOverlay)
			{
				m_kitCollisionOverlay.reset();
				m_kitCollisionVertices.clear();
				m_kitCollisionIndices.clear();
			}
			return;
		}

		if (!m_kitCollisionOverlay)
		{
			m_kitCollisionOverlay = std::make_unique<CollisionOverlay>(m_scene, m_scene.GetRootSceneNode());
			m_kitCollisionDirty = true;
		}

		const SceneNode &node = placement->GetSceneNode();
		if (node.GetDerivedPosition() != m_kitCollisionPosition || node.GetDerivedOrientation() != m_kitCollisionOrientation || node.GetDerivedScale() != m_kitCollisionScale)
		{
			m_kitCollisionPosition = node.GetDerivedPosition();
			m_kitCollisionOrientation = node.GetDerivedOrientation();
			m_kitCollisionScale = node.GetDerivedScale();
			m_kitCollisionDirty = true;
		}

		if (m_kitCollisionDirty)
		{
			RebuildKitCollisionOverlay(*placement);
		}
	}

	void WorldEditorInstance::RebuildKitCollisionOverlay(const MapEntity &placement)
	{
		m_kitCollisionDirty = false;
		m_kitCollisionVertices.clear();
		m_kitCollisionIndices.clear();

		for (const String &kitPath : placement.GetKitPaths())
		{
			const PropKitPtr kit = ResolveKit(kitPath);
			if (!kit)
			{
				continue;
			}

			for (const WorldModelMeshRef &prop : kit->props)
			{
				if (!prop.visible || prop.meshPath.empty())
				{
					continue;
				}

				const MeshPtr mesh = MeshManager::Get().Load(prop.meshPath);
				if (!mesh)
				{
					continue;
				}

				const AABBTree &tree = mesh->GetCollisionTree();
				const auto &vertices = tree.GetVertices();
				const auto &indices = tree.GetIndices();
				if (vertices.empty() || indices.empty())
				{
					continue;
				}

				const Matrix4 transform = MakeKitPropWorldTransform(placement.GetSceneNode(), prop);
				const uint32 base = static_cast<uint32>(m_kitCollisionVertices.size());
				for (const Vector3 &vertex : vertices)
				{
					m_kitCollisionVertices.push_back(transform.TransformAffine(vertex));
				}
				for (const uint32 index : indices)
				{
					m_kitCollisionIndices.push_back(base + index);
				}
			}
		}

		CollisionOverlayData data;
		if (!m_kitCollisionIndices.empty())
		{
			data.vertices = &m_kitCollisionVertices;
			data.indices = &m_kitCollisionIndices;
		}
		m_kitCollisionOverlay->Rebuild(data);
	}

	void WorldEditorInstance::DeselectRemovedKitProps(const MapEntity &placement)
	{
		const std::vector<String> &kitPaths = placement.GetKitPaths();

		std::vector<Selection::Index> stale;
		Selection::Index index = 0;
		for (const auto &selectable : m_selection.GetSelectedObjects())
		{
			const auto *selectedProp = dynamic_cast<const SelectedKitProp *>(selectable.get());
			if (selectedProp && selectedProp->GetPlacementId() == placement.GetUniqueId() &&
				std::none_of(kitPaths.begin(), kitPaths.end(), [selectedProp](const String &path) { return KitPathsEqual(path, selectedProp->GetKitPath()); }))
			{
				stale.push_back(index);
			}

			++index;
		}

		// Highest index first, so the indices still to remove stay valid.
		for (auto it = stale.rbegin(); it != stale.rend(); ++it)
		{
			m_selection.RemoveSelectable(*it);
		}
	}

	bool WorldEditorInstance::AddMeshToActiveKit(const String &meshPath, const Vector3 &worldPosition)
	{
		MapEntity *placement = GetSelectedPlacement();
		if (!placement)
		{
			return false;
		}

		// Only a drop into the building furnishes it; a drop elsewhere in the world places a world entity.
		const WorldModelInstance *instance = placement->GetWorldModelInstance();
		if (!instance || !instance->GetWorldBoundingBox(true).Intersects(worldPosition))
		{
			return false;
		}

		const String activeKit = GetActiveKit(*placement);
		if (activeKit.empty())
		{
			return false;
		}

		PropKit *kit = GetEditableKit(activeKit, placement->GetUniqueId());
		if (!kit)
		{
			return false;
		}

		// Kits are stored in building-local space: undo the placement's transform.
		const SceneNode &node = placement->GetSceneNode();
		const Vector3 scale = node.GetDerivedScale();
		Vector3 localPosition = node.GetDerivedOrientation().UnitInverse() * (worldPosition - node.GetDerivedPosition());
		constexpr float epsilon = 1.0e-6f;
		localPosition.x = std::abs(scale.x) > epsilon ? localPosition.x / scale.x : localPosition.x;
		localPosition.y = std::abs(scale.y) > epsilon ? localPosition.y / scale.y : localPosition.y;
		localPosition.z = std::abs(scale.z) > epsilon ? localPosition.z / scale.z : localPosition.z;

		WorldModelMeshRef prop;
		prop.meshPath = meshPath;
		prop.position = localPosition;
		kit->props.push_back(std::move(prop));
		const size_t propIndex = kit->props.size() - 1;

		ILOG("Added " << meshPath << " to prop kit " << activeKit << " of world model " << placement->GetUniqueId());

		RefreshKitUsers(activeKit);

		m_selection.Clear();
		m_selection.AddSelectable(std::make_unique<SelectedKitProp>(*this, placement->GetUniqueId(), activeKit, propIndex));
		UpdateSelectedKitPropBounds();
		return true;
	}

	void WorldEditorInstance::PollKitCache()
	{
		for (auto &[kitPath, observed] : m_observedKits)
		{
			const PropKitPtr cached = PropKitManager::Get().Find(kitPath);
			if (!cached || cached == observed)
			{
				continue;
			}

			// The kit was saved elsewhere (the prop kit editor replaces the cached kit on save).
			observed = cached;

			if (m_dirtyKits.contains(kitPath))
			{
				WLOG("Prop kit " << kitPath << " was saved in another editor while this world has unsaved edits to it; saving the world will overwrite that version");
				continue;
			}

			RefreshKitUsers(kitPath);
		}
	}

	bool WorldEditorInstance::WriteKitFile(const String &kitPath, const PropKit &kit)
	{
		const std::unique_ptr<std::ostream> stream = AssetRegistry::CreateNewFile(kitPath);
		if (!stream)
		{
			ELOG("Failed to save prop kit " << kitPath << ": unable to open file for writing");
			return false;
		}

		io::StreamSink sink{*stream};
		io::Writer writer{sink};

		PropKitSerializer serializer;
		serializer.Serialize(kit, writer);
		sink.Flush();

		return true;
	}

	void WorldEditorInstance::SaveDirtyKits()
	{
		std::vector<String> kitPaths;
		kitPaths.reserve(m_dirtyKits.size());
		for (const auto &[kitPath, dirty] : m_dirtyKits)
		{
			kitPaths.push_back(kitPath);
		}

		for (const String &kitPath : kitPaths)
		{
			const uint64 originPlacementId = m_dirtyKits[kitPath].originPlacementId;
			if (CountKitUsers(kitPath, originPlacementId) == 0)
			{
				SaveDirtyKit(kitPath);
				continue;
			}

			// Used by other buildings: ask first (one prompt per kit, see DrawKitModals).
			if (std::none_of(m_pendingKitSaves.begin(), m_pendingKitSaves.end(), [&kitPath](const String &path) { return KitPathsEqual(path, kitPath); }))
			{
				m_pendingKitSaves.push_back(kitPath);
			}
			m_openKitSavePrompt = true;
		}
	}

	void WorldEditorInstance::SaveDirtyKit(const String &kitPath)
	{
		const String key = MakeKitKey(kitPath);
		const auto it = m_dirtyKits.find(key);
		if (it == m_dirtyKits.end())
		{
			return;
		}

		if (!WriteKitFile(key, *it->second.kit))
		{
			// Stays dirty, so the next save tries again.
			return;
		}

		// Replace the cached kit with a copy, so further unsaved edits here do not leak into it.
		const PropKitPtr saved = std::make_shared<PropKit>(*it->second.kit);
		PropKitManager::Get().Remove(key);
		PropKitManager::Get().Insert(key, saved);
		m_observedKits[key] = saved;

		ILOG("Saved prop kit " << key << " (" << saved->props.size() << " props, " << saved->lights.size() << " lights)");

		m_dirtyKits.erase(it);
		RefreshKitUsers(key);
		UpdateSelectedKitPropBounds();
	}

	void WorldEditorInstance::SaveKitAsNew(const uint64 placementId, const String &sourceKitPath, const String &targetKitPath)
	{
		MapEntity *placement = FindMapEntity(placementId);
		if (!placement)
		{
			ELOG("Unable to save prop kit as new kit: world model " << placementId << " is no longer loaded");
			return;
		}

		const PropKitPtr source = LoadKit(sourceKitPath);
		if (!source)
		{
			ELOG("Unable to save prop kit " << sourceKitPath << " as new kit: the kit could not be loaded");
			return;
		}

		const String target = MakeKitKey(targetKitPath);

		PropKit copy = *source;
		copy.name = Path(target).stem().string();
		if (!WriteKitFile(target, copy))
		{
			return;
		}

		const PropKitPtr saved = std::make_shared<PropKit>(std::move(copy));
		PropKitManager::Get().Remove(target);
		PropKitManager::Get().Insert(target, saved);
		m_observedKits[target] = saved;

		// The new kit replaces the source kit in this building's list only.
		std::vector<String> kitPaths = placement->GetKitPaths();
		for (String &kitPath : kitPaths)
		{
			if (KitPathsEqual(kitPath, sourceKitPath))
			{
				kitPath = target;
			}
		}
		placement->SetKitPaths(std::move(kitPaths));
		m_activeKits[placementId] = target;

		// Edits to the source kit made on this building now live in the new kit, so the other
		// buildings keep the saved source kit. If another building edited the working copy too, it
		// stays dirty: those edits stay pending for the other buildings.
		const String sourceKey = MakeKitKey(sourceKitPath);
		if (const auto it = m_dirtyKits.find(sourceKey); it != m_dirtyKits.end() && it->second.editingPlacementIds == std::set<uint64>{ placementId })
		{
			ILOG("Discarded unsaved edits of prop kit " << sourceKey << "; they were saved to " << target);
			m_dirtyKits.erase(it);
		}

		// Selected props of the source kit on this building now belong to the new kit.
		if (std::any_of(m_selection.GetSelectedObjects().begin(), m_selection.GetSelectedObjects().end(), [placementId, &sourceKitPath](const auto &selectable)
		{
			const auto *selectedProp = dynamic_cast<SelectedKitProp *>(selectable.get());
			return selectedProp && selectedProp->GetPlacementId() == placementId && KitPathsEqual(selectedProp->GetKitPath(), sourceKitPath);
		}))
		{
			ClearSelection();
		}

		ApplyKits(*placement);
		RefreshKitUsers(sourceKitPath);

		ILOG("Saved prop kit " << sourceKey << " as new kit " << target << " for world model " << placementId);
		m_editor.GetHost().assetImported(Path(target).parent_path());
	}

	void WorldEditorInstance::DrawKitModals()
	{
		static const char *savePromptId = "Save Prop Kit##sharedKitSavePrompt";
		static const char *saveAsNewId = "Save as New Prop Kit##saveAsNewKit";

		if (m_openKitSavePrompt)
		{
			ImGui::OpenPopup(savePromptId);
			m_openKitSavePrompt = false;
		}

		if (ImGui::BeginPopupModal(savePromptId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			// Kits saved or discarded in the meantime need no answer.
			std::erase_if(m_pendingKitSaves, [this](const String &kitPath) { return !m_dirtyKits.contains(kitPath); });

			if (m_pendingKitSaves.empty())
			{
				ImGui::CloseCurrentPopup();
			}
			else
			{
				const String kitPath = m_pendingKitSaves.front();
				const size_t otherUsers = CountKitUsers(kitPath, m_dirtyKits[kitPath].originPlacementId);

				ImGui::Text("%s is used by %zu other %s. Save anyway?", kitPath.c_str(), otherUsers, otherUsers == 1 ? "building" : "buildings");
				ImGui::Spacing();

				bool answered = false;
				if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
				{
					SaveDirtyKit(kitPath);
					answered = true;
				}
				ImGui::SameLine();
				if (ImGui::Button("Don't save", ImVec2(120.0f, 0.0f)))
				{
					// The edits stay pending until the next world save.
					answered = true;
				}

				if (answered)
				{
					m_pendingKitSaves.erase(m_pendingKitSaves.begin());
					if (m_pendingKitSaves.empty())
					{
						ImGui::CloseCurrentPopup();
					}
				}
			}

			ImGui::EndPopup();
		}

		if (m_openSaveAsNewKit)
		{
			ImGui::OpenPopup(saveAsNewId);
			m_openSaveAsNewKit = false;
		}

		if (ImGui::BeginPopupModal(saveAsNewId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Writes %s, including unsaved edits, to a new kit used by this building only.", m_saveAsNewKitSource.c_str());
			ImGui::TextUnformatted("The original kit file is not changed.");
			ImGui::Spacing();

			ImGui::SetNextItemWidth(400.0f);
			ImGui::InputText("New kit path", &m_saveAsNewKitTarget);

			String target = MakeKitKey(m_saveAsNewKitTarget);
			if (!target.empty() && Path(target).extension() != ".hkit")
			{
				target += ".hkit";
			}

			const bool isSource = KitPathsEqual(target, m_saveAsNewKitSource);
			const bool exists = !target.empty() && AssetRegistry::HasFile(target);
			if (isSource || exists)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "A file with this name already exists.");
			}

			ImGui::BeginDisabled(target.empty() || isSource || exists);
			if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
			{
				SaveKitAsNew(m_saveAsNewKitPlacement, m_saveAsNewKitSource, target);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	void WorldEditorInstance::Visit(SelectedKitProp &selectable)
	{
		const MapEntity *placement = FindMapEntity(selectable.GetPlacementId());
		const WorldModelMeshRef *prop = FindKitProp(selectable.GetKitPath(), selectable.GetPropIndex());
		if (!placement || !prop)
		{
			ImGui::TextDisabled("The selected kit prop no longer exists.");
			return;
		}

		const bool unsaved = m_dirtyKits.contains(MakeKitKey(selectable.GetKitPath()));

		ImGui::Text("Type: Kit Prop");
		ImGui::Text("Building: %llu (%s)", placement->GetUniqueId(), placement->GetAssetName().c_str());
		ImGui::Text("Kit: %s%s", selectable.GetKitPath().c_str(), unsaved ? " *" : "");

		const size_t users = CountKitUsers(selectable.GetKitPath(), 0);
		if (users > 1)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " This kit is used by %zu loaded buildings; edits change all of them.", users);
		}

		if (ImGui::CollapsingHeader("Prop", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Edit a copy, so merely showing the prop does not turn the kit into a working copy.
			WorldModelMeshRef edited = *prop;
			const uint32 changes = DrawMeshRefProperties(edited);
			if ((changes & mesh_ref_edit::Delete) != 0)
			{
				QueueKitPropRemoval(selectable.GetPlacementId(), selectable.GetKitPath(), selectable.GetPropIndex());
			}
			else if (changes != mesh_ref_edit::None)
			{
				EditKitProp(selectable.GetPlacementId(), selectable.GetKitPath(), selectable.GetPropIndex(), [&edited](WorldModelMeshRef &target)
				{
					target = edited;
				});
			}
		}
	}
}
