// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_editor_instance.h"
#include "selection_raycaster.h"

#include <DetourDebugDraw.h>
#include <imgui_internal.h>

#include "editor_host.h"
#include "world_editor.h"
#include "paging/world_page_loader.h"
#include "assets/asset_registry.h"
#include "editors/material_editor/node_editor/node_layout.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene_node.h"
#include "selected_map_entity.h"
#include "stream_sink.h"
#include "game/object_type_id.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "game_client/world_entity_loader.h"
#include "graphics/texture_mgr.h"
#include "nav_build/common.h"
#include "scene_graph/mesh_manager.h"
#include "terrain/page.h"
#include "terrain/tile.h"
#include "edit_modes/navigation_edit_mode.h"
#include "stream_sink.h"
#include "tex_v1_0/header_save.h"
#include "graphics/render_texture.h"

#include "stb_dxt.h"

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

		m_scene.SetFogRange(210.0f, 300.0f);

		const Vector3 fogColor = Vector3(0.231f * 1.5f, 0.398f * 1.5f, 0.535f * 1.5f);
		m_scene.SetFogColor(fogColor);

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

		m_selectionRaycaster = std::make_unique<SelectionRaycaster>(
			*m_camera,
			*m_raySceneQuery,
			m_selection,
			*m_debugBoundingBox,
			nullptr, // terrain - will be set after terrain is created
			m_editor,
			nullptr); // spawn edit mode - will be set later

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

		// Setup terrain
		m_terrain = std::make_unique<terrain::Terrain>(m_scene, m_camera, 64, 64);
		m_terrain->SetTileSceneQueryFlags(SceneQueryFlags_Tile);
		m_terrain->SetWireframeMaterial(MaterialManager::Get().Load("Editor/Wireframe.hmat"));

		// Update selection raycaster with terrain pointer
		m_selectionRaycaster->SetTerrain(m_terrain.get());

		// Replace all \ with /
		String baseFileName = (m_assetPath.parent_path() / m_assetPath.filename().replace_extension() / "Terrain").string();
		std::transform(baseFileName.begin(), baseFileName.end(), baseFileName.begin(), [](char c)
					   { return c == '\\' ? '/' : c; });
		m_terrain->SetBaseFileName(baseFileName);

		m_debugNode = m_scene.GetRootSceneNode().CreateChildSceneNode();
		m_debugEntity = m_scene.CreateEntity("TerrainDebug", "Editor/Joint.hmsh");
		m_debugNode->AttachObject(*m_debugEntity);
		m_debugEntity->SetVisible(false);

		// Setup sky component for day/night cycle control
		m_skyComponent = std::make_unique<SkyComponent>(m_scene);
		m_skyComponent->SetTimeSpeed(0.0f); // Start with time paused in editor

		// Setup edit modes
		m_terrainEditMode = std::make_unique<TerrainEditMode>(*this, *m_terrain, m_editor.GetProject().zones, *m_camera);
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
			m_hasTerrain,
			m_editMode,
			m_terrainEditMode.get(),
			[this](WorldEditMode *mode)
			{ SetEditMode(mode); });

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
			{ GenerateMinimaps(); });

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
		m_spawnEditMode = std::make_unique<SpawnEditMode>(*this, m_editor.GetProject().maps, m_editor.GetProject().units, m_editor.GetProject().objects);
		m_skyEditMode = std::make_unique<SkyEditMode>(*this, *m_skyComponent);
		m_areaTriggerEditMode = std::make_unique<AreaTriggerEditMode>(*this, m_editor.GetProject().maps, m_editor.GetProject().areaTriggers);
		m_editMode = nullptr;

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
	}

	WorldEditorInstance::~WorldEditorInstance()
	{
		// Stop background loading thread
		m_work.reset();
		m_workQueue.stop();
		m_dispatcher.stop();
		m_backgroundLoader.join();

		m_transformWidget.reset();
		m_mapEntities.clear();
		m_worldGrid.reset();
		m_scene.Clear();
	}

	void WorldEditorInstance::Render()
	{
		m_dispatcher.poll();

		const float deltaTimeSeconds = ImGui::GetCurrentContext()->IO.DeltaTime;

		if (ImGui::IsKeyPressed(ImGuiKey_F))
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

		if (ImGui::IsKeyPressed(ImGuiKey_Z))
		{
			if (!m_selection.IsEmpty())
			{
				m_selection.GetSelectedObjects().back()->Translate(
					-m_selection.GetSelectedObjects().back()->GetPosition());
			}
			m_cameraVelocity = Vector3::Zero;
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
		}

		m_cameraAnchor->Translate(m_cameraVelocity * deltaTimeSeconds, TransformSpace::Local);
		m_cameraVelocity *= powf(0.025f, deltaTimeSeconds);

		// Terrain deformation
		if (m_hovering && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (m_editMode)
				m_editMode->OnMouseHold(deltaTimeSeconds);
		}

		const auto pos = GetPagePositionFromCamera();
		m_memoryPointOfView->UpdateCenter(pos);
		m_visibleSection->UpdateCenter(pos);

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

		HandleKeyboardShortcuts();

		WorldEditMode *availableModes[] = {
			m_entityEditMode.get(),
			m_terrainEditMode.get(),
			m_spawnEditMode.get(),
			m_areaTriggerEditMode.get(),
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
		m_viewportPanel->Draw(viewportId, m_editMode);
		DrawSceneOutlinePanel(sceneOutlineId);

		if (m_initDockLayout)
		{
			InitializeDockLayout(dockspaceId, viewportId, detailsId, worldSettingsId);
		}

		ImGui::PopID();
	}

	void WorldEditorInstance::HandleKeyboardShortcuts()
	{
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
	}

	void WorldEditorInstance::InitializeDockLayout(ImGuiID dockspaceId, const String &viewportId, const String &detailsId, const String &worldSettingsId)
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

		ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
		ImGui::DockBuilderDockWindow(sceneOutlineId.c_str(), sideTopId);
		ImGui::DockBuilderDockWindow(detailsId.c_str(), sideId);
		ImGui::DockBuilderDockWindow(worldSettingsId.c_str(), sideId);

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
			}

			// TODO: Move this into the edit modes handling of OnMouseUp
			if (m_editMode == m_entityEditMode.get())
			{
				if (!widgetWasActive && button == 0 && m_hovering)
				{
					m_selectionRaycaster->PerformEntitySelection(
						(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
						(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y,
						ImGui::IsKeyPressed(ImGuiKey_LeftShift));
				}
			}
			else if (m_editMode == m_spawnEditMode.get())
			{
				if (!widgetWasActive && button == 0 && m_hovering)
				{
					m_selectionRaycaster->PerformSpawnSelection(
						(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
						(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
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

			// TODO: Move this into edit modes handling of OnMouseMoved
			if (m_rightButtonPressed || (m_leftButtonPressed && (m_editMode != m_terrainEditMode.get() || (m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Deform &&
																										   m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Paint &&
																										   m_terrainEditMode->GetTerrainEditType() != TerrainEditType::Area &&
																										   m_terrainEditMode->GetTerrainEditType() != TerrainEditType::VertexShading))))
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
		// Build mesh name index map
		std::map<String, uint32> entityNames;
		for (const auto &mapEntity : m_mapEntities)
		{
			const auto meshName = String(mapEntity->GetEntity().GetMesh()->GetName());
			if (entityNames.contains(meshName))
			{
				continue;
			}

			entityNames.emplace(meshName, entityNames.size());
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

			{
				ChunkWriter chunkWriter(WorldEntityVersionChunk, fileWriter);
				fileWriter << io::write<uint32>(2);
				chunkWriter.Finish();
			}

			{
				ChunkWriter chunkWriter(WorldEntityMesh, fileWriter);
				const Vector3 position = ent->GetSceneNode().GetDerivedPosition();
				const Vector3 scale = ent->GetSceneNode().GetScale();
				const Quaternion rotation = ent->GetSceneNode().GetDerivedOrientation();
				const String mesh = ent->GetEntity().GetMesh()->GetName().data();
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
				for (uint8 i = 0; i < ent->GetEntity().GetNumSubEntities(); ++i)
				{
					SubEntity *sub = ent->GetEntity().GetSubEntity(i);
					ASSERT(sub);

					SubMesh &submesh = ent->GetEntity().GetMesh()->GetSubMesh(i);
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

		return true;
	}

	void WorldEditorInstance::OnTerrainMouseMoved(const float viewportX, const float viewportY)
	{
		const Ray ray = m_camera->GetCameraToViewportRay(viewportX, viewportY, 10000.0f);

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
		m_debugEntity->SetVisible(true);
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

	void WorldEditorInstance::OnMapEntityRemoved(MapEntity &entity)
	{
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

void WorldEditorInstance::DrawSceneOutlinePanel(const String &sceneOutlineId)
	{
		// Update the scene outline window regularly, especially when selection changes
		if (m_sceneOutlineWindow)
		{
			m_sceneOutlineWindow->Draw(sceneOutlineId.c_str());
		}
	}

	void WorldEditorInstance::GenerateMinimaps()
	{
		if (!m_terrain || !m_hasTerrain)
		{
			ELOG("Cannot generate minimaps: no terrain available");
			return;
		}

		// Minimap configuration
		const uint32 minimapSize = 256; // 512x512 pixels per minimap
		const float pageSize = terrain::constants::PageSize;

		auto &gx = GraphicsDevice::Get();
		gx.CaptureState();

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

					// Calculate world position for page centers
					const float worldX = page->GetSceneNode()->GetDerivedPosition().x + pageSize * 0.5f;
					const float worldZ = page->GetSceneNode()->GetDerivedPosition().z + pageSize * 0.5f;

					// Get terrain height at page center for camera positioning
					float terrainHeight = page->GetBoundingBox().GetExtents().y;

					// Get the maximum height in this page to ensure we capture all entities
					float maxHeight = terrainHeight;

					// Check all map entities in this page to find the maximum height
					for (const auto &mapEntity : m_mapEntities)
					{
						if (mapEntity && &mapEntity->GetSceneNode())
						{
							const Vector3 &entityPos = mapEntity->GetSceneNode().GetDerivedPosition();

							// Check if this entity is within the current page bounds
							if (entityPos.x >= page->GetSceneNode()->GetDerivedPosition().x &&
								entityPos.x < page->GetSceneNode()->GetDerivedPosition().x + pageSize &&
								entityPos.z >= page->GetSceneNode()->GetDerivedPosition().z &&
								entityPos.z < page->GetSceneNode()->GetDerivedPosition().z + pageSize)
							{
								// Get the entity's bounding box to determine its height
								Entity &entity = mapEntity->GetEntity();
								const AABB &entityBounds = entity.GetBoundingBox();
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

					// Temporarily change entity render queue groups to match terrain for consistent rendering
					std::vector<std::pair<Entity *, uint8>> originalRenderQueues;
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
								Entity &entity = mapEntity->GetEntity();
								originalRenderQueues.emplace_back(&entity, entity.GetRenderQueueGroup());
								entity.SetRenderQueueGroup(WorldGeometry1); // Same as terrain
							}
						}
					}

					// Render the scene (terrain and objects in this page)
					m_scene.SetFogRange(10000.0f, 100000.0f);
					m_scene.Render(*renderCam, PixelShaderType::Forward);
					minimapRT->Update();

					// Restore original render queue groups
					for (const auto &[entity, originalQueue] : originalRenderQueues)
					{
						entity->SetRenderQueueGroup(originalQueue);
					}

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
							// Save texture using the engine's texture format
							io::StreamSink sink(*outStream);
							tex::v1_0::Header header(tex::Version_1_0);
							header.width = minimapSize;
							header.height = minimapSize;
							header.format = tex::v1_0::DXT1;
							header.hasMips = false;

							tex::v1_0::HeaderSaver saver(sink, header);

							header.mipmapOffsets[0] = static_cast<uint32>(sink.Position());

							// Copy pixel data from render texture
							const uint32 pixelDataSize = minimapSize * minimapSize * 4; // RGBA

							// Apply compression
							const size_t compressedSize = pixelDataSize / 8;

							std::vector<uint8> pixelData(pixelDataSize);
							minimapTexture->CopyPixelDataTo(pixelData.data());

							// Allocate buffer for compression
							std::vector<uint8> buffer;
							buffer.resize(compressedSize);

							// Apply compression
							rygCompress(buffer.data(), pixelData.data(), minimapSize, minimapSize, false);
							header.mipmapLengths[0] = static_cast<uint32>(buffer.size());
							sink.Write(reinterpret_cast<const char *>(buffer.data()), buffer.size());
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

		gx.RestoreState();
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
			Entity *object = CreateMapEntity(
				entity.meshName,
				entity.position,
				entity.rotation,
				entity.scale,
				entity.uniqueId);

			// We just loaded the object - it has not been modified
			MapEntity *mapEntity = object->GetUserObject<MapEntity>();
			ASSERT(mapEntity);
			mapEntity->SetDisplayName(entity.name);
			mapEntity->SetCategory(entity.category);
			mapEntity->MarkAsUnmodified();

			// Apply material overrides
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
	}

	void WorldEditorInstance::UnloadPageEntities(uint8 x, uint8 y)
	{
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
		}
		else
		{
			UnloadPageEntities(pos.x(), pos.y());

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
		}
	}

	void WorldEditorInstance::Visit(SelectedMapEntity &selectable)
	{
		static const char *s_noMaterialPreview = "<None>";

		MapEntity &mapEntity = selectable.GetEntity();
		Entity &entity = mapEntity.GetEntity();

		ImGui::Text("Unique Id: %u", mapEntity.GetUniqueId());

		if (ImGui::CollapsingHeader("Mesh"))
		{
			const MeshPtr mesh = entity.GetMesh();
			const String meshName = mesh->GetName().data();

			const String filename = Path(meshName).filename().string();

			if (ImGui::BeginCombo("Mesh", filename.c_str()))
			{
				// TODO: Draw available mesh files from asset registry

				ImGui::EndCombo();
			}

			if (ImGui::BeginDragDropTarget())
			{
				// We only accept mesh file drops
				if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(".hmsh"))
				{
					entity.SetMesh(MeshManager::Get().Load(*static_cast<String *>(payload->Data)));
					mapEntity.MarkModified();
				}

				ImGui::EndDragDropTarget();
			}
		}

		if (ImGui::CollapsingHeader("Materials"))
		{
			for (uint32 i = 0; i < entity.GetNumSubEntities(); ++i)
			{
				SubEntity *sub = entity.GetSubEntity(i);
				ASSERT(sub);

				SubMesh &submesh = entity.GetMesh()->GetSubMesh(i);

				ImGui::PushID(i);

				// Get material
				MaterialPtr material = sub->GetMaterial();
				if (!material)
				{
					material = submesh.GetMaterial();
				}

				// Build preview string
				const char *previewString = s_noMaterialPreview;
				if (material)
				{
					previewString = material->GetName().data();
				}

				const String materialName = sub->GetMaterial()->GetName().data();
				const String filename = Path(materialName).filename().string();

				bool isOverridden = material != submesh.GetMaterial();

				ImGui::BeginDisabled(!isOverridden);
				if (ImGui::Checkbox("##overridden", &isOverridden))
				{
					sub->SetMaterial(submesh.GetMaterial());
					mapEntity.MarkModified();
				}
				ImGui::EndDisabled();

				ImGui::SameLine();

				if (ImGui::BeginCombo("Material", filename.c_str()))
				{
					ImGui::EndCombo();
				}

				if (ImGui::BeginDragDropTarget())
				{
					// We only accept mesh file drops
					if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(".hmat"))
					{
						sub->SetMaterial(MaterialManager::Get().Load(*static_cast<String *>(payload->Data)));
						mapEntity.MarkModified();
					}

					if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(".hmi"))
					{
						sub->SetMaterial(MaterialManager::Get().Load(*static_cast<String *>(payload->Data)));
						mapEntity.MarkModified();
					}

					ImGui::EndDragDropTarget();
				}

				ImGui::PopID();
			}
		}
	}

	void WorldEditorInstance::Visit(SelectedTerrainTile &selectable)
	{
		if (ImGui::CollapsingHeader("Tile"))
		{
			static const char *s_noMaterialPreview = "<None>";

			terrain::Tile &tile = selectable.GetTile();

			ImGui::Text("Tile Grid: %d x %d", tile.GetX(), tile.GetY());
			ImGui::Text("Page Grid: %d x %d", tile.GetPage().GetX(), tile.GetPage().GetY());

			// Get material
			MaterialPtr material = tile.GetMaterial();

			// Build preview string
			const char *previewString = s_noMaterialPreview;
			if (material)
			{
				previewString = material->GetName().data();
			}

			if (ImGui::BeginCombo("Material", previewString))
			{
				ImGui::EndCombo();
			}

			if (ImGui::BeginDragDropTarget())
			{
				// We only accept mesh file drops
				if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(".hmat"))
				{
					tile.SetMaterial(MaterialManager::Get().Load(*static_cast<String *>(payload->Data)));
				}

				if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(".hmi"))
				{
					tile.SetMaterial(MaterialManager::Get().Load(*static_cast<String *>(payload->Data)));
				}

				ImGui::EndDragDropTarget();
			}

			if (ImGui::Button("Set For Page"))
			{
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Sets the selected material for all tiles on the whole page");
			}
		}
	}

	void WorldEditorInstance::Visit(SelectedUnitSpawn &selectable)
	{
		if (ImGui::CollapsingHeader("Unit Spawn"))
		{
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
				"Patrol",
				"Route"};

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
		}
	}

	void WorldEditorInstance::Visit(SelectedObjectSpawn &selectable)
	{
		if (ImGui::CollapsingHeader("Object Spawn"))
		{
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

			uint32 state = selectable.GetEntry().state();
			if (ImGui::InputScalar("State", ImGuiDataType_U32, &state))
			{
				selectable.GetEntry().set_state(state);
			}

			// Allow editing animation progress
			uint32 animProgress = selectable.GetEntry().animprogress();
			if (ImGui::SliderInt("Animation Progress", (int *)&animProgress, 0, 100))
			{
				selectable.GetEntry().set_animprogress(animProgress);
			}
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
	}

	bool WorldEditorInstance::IsTransforming() const
	{
		return m_transformWidget && m_transformWidget->IsActive();
	}
}
