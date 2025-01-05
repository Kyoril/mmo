// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_editor_instance.h"

#include <imgui_internal.h>

#include "editor_host.h"
#include "world_editor.h"
#include "paging/world_page_loader.h"
#include "assets/asset_registry.h"
#include "editors/material_editor/node_layout.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/scene_node.h"
#include "selected_map_entity.h"
#include "stream_sink.h"
#include "scene_graph/mesh_manager.h"
#include "terrain/page.h"
#include "terrain/tile.h"

namespace mmo
{
	/// @brief Default value. Object will not be hit by any scene query.
	static constexpr uint32 SceneQueryFlags_None = 0;

	/// @brief Used for map entities.
	static constexpr uint32 SceneQueryFlags_Entity = 1 << 0;

	static constexpr uint32 SceneQueryFlags_Tile = 1 << 1;

	static constexpr uint32 SceneQueryFlags_Spawns = 1 << 2;

	static const ChunkMagic versionChunk = MakeChunkMagic('MVER');
	static const ChunkMagic meshChunk = MakeChunkMagic('MESH');
	static const ChunkMagic entityChunk = MakeChunkMagic('MENT');
	static const ChunkMagic terrainChunk = MakeChunkMagic('RRET');

	struct MapEntityChunkContent
	{
		uint32 uniqueId;
		uint32 meshNameIndex;
		Vector3 position;
		Quaternion rotation;
		Vector3 scale;
	};

	WorldEditorInstance::WorldEditorInstance(EditorHost& host, WorldEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
		, m_wireFrame(false)
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
		auto& workQueue = m_workQueue;
		auto& dispatcher = m_dispatcher;
		m_backgroundLoader = std::thread([&workQueue]()
		{
			workQueue.run();
		});

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

		m_scene.GetRootSceneNode().AttachObject(*m_debugBoundingBox);

		PagePosition worldSize(64, 64);
		m_memoryPointOfView = std::make_unique<PagePOVPartitioner>(
			worldSize,
			2,
			pos,
			*m_pageLoader
			);

		m_transformWidget = std::make_unique<TransformWidget>(m_selection, m_scene, *m_camera);
		m_transformWidget->SetTransformMode(TransformMode::Translate);
		m_transformWidget->copySelection += [this]()
		{
			if (m_selection.IsEmpty())
			{
				return;
			}

			// Copy selection
			for(const auto& selected : m_selection.GetSelectedObjects())
			{
				selected->Duplicate();
			}
		};

		// Setup terrain
		m_terrain = std::make_unique<terrain::Terrain>(m_scene, m_camera, 64, 64);
		m_terrain->SetTileSceneQueryFlags(SceneQueryFlags_Tile);

		// Replace all \ with /
		String baseFileName = (m_assetPath.parent_path() / m_assetPath.filename().replace_extension()).string();
		std::transform(baseFileName.begin(), baseFileName.end(), baseFileName.begin(), [](char c) { return c == '\\' ? '/' : c; });
		m_terrain->SetBaseFileName(baseFileName);

		m_cloudsEntity = m_scene.CreateEntity("Clouds", "Models/SkySphere.hmsh");
		m_cloudsEntity->SetRenderQueueGroup(SkiesEarly);
		m_cloudsEntity->SetQueryFlags(0);
		m_cloudsNode = &m_scene.CreateSceneNode("Clouds");
		m_cloudsNode->AttachObject(*m_cloudsEntity);
		m_cloudsNode->SetScale(Vector3::UnitScale * 40.0f);
		m_scene.GetRootSceneNode().AddChild(*m_cloudsNode);

		m_debugNode = m_scene.GetRootSceneNode().CreateChildSceneNode();
		m_debugEntity = m_scene.CreateEntity("TerrainDebug", "Editor/Joint.hmsh");
		m_debugNode->AttachObject(*m_debugEntity);
		m_debugEntity->SetVisible(false);

		// TODO: Load map file
		std::unique_ptr<std::istream> streamPtr = AssetRegistry::OpenFile(GetAssetPath().string());
		if (!streamPtr)
		{
			ELOG("Failed to load world file '" << GetAssetPath() << "'");
			return;
		}

		AddChunkHandler(*versionChunk, true, *this, &WorldEditorInstance::ReadMVERChunk);

		io::StreamSource source{ *streamPtr };
		io::Reader reader{ source };
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
			if(ImGui::IsKeyDown(ImGuiKey_S))
			{
				direction.z = 1.0f;
			}
			if(ImGui::IsKeyDown(ImGuiKey_A))
			{
				direction.x = -1.0f;
			}
			if(ImGui::IsKeyDown(ImGuiKey_D))
			{
				direction.x = 1.0f;
			}
			if(ImGui::IsKeyDown(ImGuiKey_Q))
			{
				direction.y = -1.0f;
			}
			if(ImGui::IsKeyDown(ImGuiKey_E))
			{
				direction.y = 1.0f;
			}

			if (direction != Vector3::Zero)
			{
				m_cameraVelocity = direction.NormalizedCopy() * m_cameraSpeed;
			}
		}

		if (m_cloudsNode)
		{
			m_cloudsNode->SetPosition(m_camera->GetDerivedPosition());
		}

		m_cameraAnchor->Translate(m_cameraVelocity * deltaTimeSeconds, TransformSpace::Local);
		m_cameraVelocity *= powf(0.025f, deltaTimeSeconds);

		// Terrain deformation
		if (m_hovering && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			const float factor = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? -1.0f : 1.0f;
			
			if (m_editMode == WorldEditMode::Terrain)
			{
				int32 pageX, pageY;
				m_terrain->GetPageIndexByWorldPosition(m_brushPosition, pageX, pageY);

				const float pageOffsetX = pageX * terrain::constants::PageSize;
				const float pageOffsetY = pageY * terrain::constants::PageSize;
				constexpr float scale = static_cast<float>(terrain::constants::PageSize / (terrain::constants::VerticesPerPage - 1));

				int globalVertexX = static_cast<int>((m_brushPosition.x + pageOffsetX) / scale);
				int globalVertexY = static_cast<int>((m_brushPosition.z + pageOffsetY) / scale);
				int pageVertexX = globalVertexX % (terrain::constants::VerticesPerPage - 1);
				int pageVertexY = globalVertexY % (terrain::constants::VerticesPerPage - 1);

				int vX = pageX * (terrain::constants::VerticesPerPage - 1) + pageVertexX;
				int vY = pageY * (terrain::constants::VerticesPerPage - 1) + pageVertexY;

				const int32 outerRadius = m_terrainBrushSize;
				const int32 innerRadius = std::max(1, static_cast<int32>(static_cast<float>(m_terrainBrushSize) * m_terrainBrushHardness));

				if (m_terrainEditMode == TerrainEditMode::Deform)
				{
					if (m_terrainDeformMode == TerrainDeformMode::Flatten && ImGui::IsKeyDown(ImGuiKey_LeftControl))
					{
						m_deformFlattenHeight = m_terrain->GetHeightAt(vX, vY);
					}
					else
					{
						switch (m_terrainDeformMode)
						{
						case TerrainDeformMode::Sculpt:
						{
							m_terrain->Deform(vX, vY,
								innerRadius, outerRadius, m_terrainBrushPower * factor * deltaTimeSeconds);
						} break;
						case TerrainDeformMode::Smooth:
						{
							m_terrain->Smooth(vX, vY,
								innerRadius, outerRadius, m_terrainBrushPower* factor * deltaTimeSeconds);
						} break;
						case TerrainDeformMode::Flatten:
						{
							m_terrain->Flatten(vX, vY,
								innerRadius, outerRadius, m_terrainBrushPower * factor * deltaTimeSeconds, m_deformFlattenHeight);
						} break;
						}
					}
				}
				else if (m_terrainEditMode == TerrainEditMode::Paint)
				{
					m_terrain->Paint(m_terrainPaintLayer, vX, vY,
						innerRadius, outerRadius, m_terrainBrushPower * factor* deltaTimeSeconds);
				}
			}
		}

		const auto pos = GetPagePositionFromCamera();
		m_memoryPointOfView->UpdateCenter(pos);
		m_visibleSection->UpdateCenter(pos);
		
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		auto& gx = GraphicsDevice::Get();

		// Render the scene first
		gx.Reset();
		gx.SetClearColor(Color::Black);
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);
		m_camera->SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);

		m_scene.Render(*m_camera);
		m_transformWidget->Update(m_camera);
		
		m_viewportRT->Update();
	}

	static const char* s_editModeStrings[] = {
		"None",

		"Map Entities",
		"Terrain",
		"Spawns"
	};

	static_assert(std::size(s_editModeStrings) == static_cast<uint32>(WorldEditMode::Count_), "There needs to be one string per enum value to display!");

	static const char* s_terrainEditModeStrings[] = {
		"Select",
		"Deform",
		"Paint"
	};

	static_assert(std::size(s_terrainEditModeStrings) == static_cast<uint32>(TerrainEditMode::Count_), "There needs to be one string per enum value to display!");

	static const char* s_terrainDeformModeStrings[] = {
		"Sculpt",
		"Smooth",
		"Flatten"
	};

	static_assert(std::size(s_terrainDeformModeStrings) == static_cast<uint32>(TerrainDeformMode::Count_), "There needs to be one string per enum value to display!");


	void WorldEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockspaceId = ImGui::GetID("##model_dockspace_");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();
		const String worldSettingsId = "World Settings##" + GetAssetPath().string();

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


		if (ImGui::IsKeyDown(ImGuiKey_F1))
		{
			m_editMode = WorldEditMode::None;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_F2))
		{
			m_editMode = WorldEditMode::StaticMapEntities;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_F3) && m_hasTerrain)
		{
			m_editMode = WorldEditMode::Terrain;
		}

		if (ImGui::Begin(detailsId.c_str()))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
		    if (ImGui::BeginTable("split", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
		    {

		        ImGui::EndTable();
		    }
		    ImGui::PopStyleVar();

			ImGui::Separator();

			if (ImGui::Button("Save"))
			{
				Save();
			}

			ImGui::Separator();

			if (ImGui::BeginCombo("Mode", s_editModeStrings[static_cast<uint32>(m_editMode)], ImGuiComboFlags_None))
			{
				for (uint32 i = 0; i < static_cast<uint32>(WorldEditMode::Count_); ++i)
				{
					ImGui::PushID(i);

					ImGui::BeginDisabled(i == static_cast<uint32>(WorldEditMode::Terrain) && !m_hasTerrain);
					if (ImGui::Selectable(s_editModeStrings[i], i == static_cast<uint32>(m_editMode)))
					{
						m_editMode = static_cast<WorldEditMode>(i);
						m_selection.Clear();
					}
					ImGui::EndDisabled();
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			if (m_editMode == WorldEditMode::Spawns)
			{
				static const char* s_noneMap = "<None>";

				if (ImGui::BeginCombo("Map", m_mapEntry ? m_mapEntry->name().c_str() : s_noneMap, ImGuiComboFlags_None))
				{
					for (uint32 i = 0; i < static_cast<uint32>(m_editor.GetProject().maps.count()); ++i)
					{
						ImGui::PushID(i);
						if (ImGui::Selectable(m_editor.GetProject().maps.getTemplates().entry(i).name().c_str(), m_editor.GetProject().maps.getTemplates().mutable_entry(i) == m_mapEntry))
						{
							m_mapEntry = m_editor.GetProject().maps.getTemplates().mutable_entry(i);

							// TODO: Reload spawn objects
						}
						ImGui::PopID();
					}

					ImGui::EndCombo();
				}
			}

			if (m_editMode == WorldEditMode::Terrain && m_hasTerrain)
			{
				if (ImGui::BeginCombo("Terrain Edit Mode", s_terrainEditModeStrings[static_cast<uint32>(m_terrainEditMode)], ImGuiComboFlags_None))
				{
					for (uint32 i = 0; i < static_cast<uint32>(TerrainEditMode::Count_); ++i)
					{
						ImGui::PushID(i);
						if (ImGui::Selectable(s_terrainEditModeStrings[i], i == static_cast<uint32>(m_terrainEditMode)))
						{
							m_terrainEditMode = static_cast<TerrainEditMode>(i);
							m_selection.Clear();
						}
						ImGui::PopID();
					}

					ImGui::EndCombo();
				}

				if (m_terrainEditMode == TerrainEditMode::Deform && m_hasTerrain)
				{
					if (ImGui::BeginCombo("Deform Mode", s_terrainDeformModeStrings[static_cast<uint32>(m_terrainDeformMode)], ImGuiComboFlags_None))
					{
						for (uint32 i = 0; i < static_cast<uint32>(TerrainDeformMode::Count_); ++i)
						{
							ImGui::PushID(i);
							if (ImGui::Selectable(s_terrainDeformModeStrings[i], i == static_cast<uint32>(m_terrainDeformMode)))
							{
								m_terrainDeformMode = static_cast<TerrainDeformMode>(i);
								m_selection.Clear();
							}
							ImGui::PopID();
						}

						ImGui::EndCombo();
					}
				}
				else if (m_terrainEditMode == TerrainEditMode::Paint && m_hasTerrain)
				{
					static const char* s_layerNames[] = { "Layer 1", "Layer 2", "Layer 3", "Layer 4" };

					if (ImGui::BeginCombo("Layer", s_layerNames[m_terrainPaintLayer]))
					{
						for (uint32 i = 0; i < std::size(s_layerNames); ++i)
						{
							ImGui::PushID(i);
							if (ImGui::Selectable(s_layerNames[i], i == m_terrainPaintLayer))
							{
								m_terrainPaintLayer = i;
							}
							ImGui::PopID();
						}

						ImGui::EndCombo();
					}
				}

				ImGui::SliderInt("Brush Radius", &m_terrainBrushSize, 1, terrain::constants::VerticesPerTile);
				ImGui::SliderFloat("Brush Hardness", &m_terrainBrushHardness, 0.0f, 1.0f);
				ImGui::SliderFloat("Brush Power", &m_terrainBrushPower, 0.01f, 10.0f);
			}

			ImGui::Separator();

			if (!m_selection.IsEmpty())
			{
				Selectable* selected = m_selection.GetSelectedObjects().back().get();

				selected->Visit(*this);

				if (selected->SupportsTranslate() || selected->SupportsRotate() || selected->SupportsScale())
				{
					if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
					{
						if (selected->SupportsTranslate())
						{
							if (Vector3 position = selected->GetPosition(); ImGui::InputFloat3("Position", position.Ptr()))
							{
								selected->SetPosition(position);
							}
						}

						if (selected->SupportsRotate())
						{
							Rotator rotation = selected->GetOrientation().ToRotator();
							if (float angles[3] = { rotation.roll.GetValueDegrees(), rotation.yaw.GetValueDegrees(), rotation.pitch.GetValueDegrees() }; ImGui::InputFloat3("Rotation", angles, "%.3f"))
							{
								rotation.roll = angles[0];
								rotation.pitch = angles[2];
								rotation.yaw = angles[1];

								Quaternion quaternion = Quaternion::FromRotator(rotation);
								quaternion.Normalize();

								selected->SetOrientation(quaternion);
							}
						}

						if (selected->SupportsScale())
						{
							Vector3 scale = selected->GetScale();
							if (ImGui::InputFloat3("Scale", scale.Ptr()))
							{
								selected->SetScale(scale);
							}
						}
					}
				}

			}
		}
		ImGui::End();
		
		if (ImGui::Begin(worldSettingsId.c_str()))
		{
			if (ImGui::CollapsingHeader("World Settings", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (ImGui::Checkbox("Has Terrain", &m_hasTerrain))
				{
					m_terrain->SetVisible(m_hasTerrain);

					// Ensure terrain mode is not selectable if there is no terrain
					if (!m_hasTerrain && m_editMode == WorldEditMode::Terrain)
					{
						m_editMode = WorldEditMode::None;
					}
				}

				ImGui::BeginDisabled(!m_hasTerrain);

				static const char* s_noMaterialPreview = "<None>";

				const char* previewString = s_noMaterialPreview;
				if (m_terrain->GetDefaultMaterial())
				{
					previewString = m_terrain->GetDefaultMaterial()->GetName().data();
				}

				if (ImGui::BeginCombo("Terrain Default Material", previewString))
				{
					ImGui::EndCombo();
				}

				if (m_hasTerrain)
				{
					if (ImGui::BeginDragDropTarget())
					{
						// We only accept mesh file drops
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmat"))
						{
							m_terrain->SetDefaultMaterial(MaterialManager::Get().Load(*static_cast<String*>(payload->Data)));
						}

						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmi"))
						{
							m_terrain->SetDefaultMaterial(MaterialManager::Get().Load(*static_cast<String*>(payload->Data)));
						}

						ImGui::EndDragDropTarget();
					}
				}

				ImGui::EndDisabled();
			}
		}
		ImGui::End();
		
		if (ImGui::Begin(viewportId.c_str()))
		{
			// Determine the current viewport position
			auto viewportPos = ImGui::GetWindowContentRegionMin();
			viewportPos.x += ImGui::GetWindowPos().x;
			viewportPos.y += ImGui::GetWindowPos().y;

			// Determine the available size for the viewport window and either create the render target
			// or resize it if needed
			const auto availableSpace = ImGui::GetContentRegionAvail();
			
			if (m_viewportRT == nullptr)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("Viewport", std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y));
				m_lastAvailViewportSize = availableSpace;
			}
			else if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
			{
				m_viewportRT->Resize(availableSpace.x, availableSpace.y);
				m_lastAvailViewportSize = availableSpace;
			}

			// Render the render target content into the window as image object
			ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);
			ImGui::SetItemUsingMouseWheel();

			m_hovering = ImGui::IsItemHovered();
			if (m_hovering)
			{
				m_cameraSpeed = std::max(std::min(m_cameraSpeed + ImGui::GetIO().MouseWheel * 5.0f, 200.0f), 1.0f);

				m_leftButtonPressed = ImGui::IsMouseDown(ImGuiMouseButton_Left);
				m_rightButtonPressed = ImGui::IsMouseDown(ImGuiMouseButton_Right);

				const auto mousePos = ImGui::GetMousePos();
				const auto contentRectMin = ImGui::GetWindowPos();
				m_lastContentRectMin = contentRectMin;

				if (ImGui::IsKeyPressed(ImGuiKey_Delete))
				{
					if (!m_selection.IsEmpty())
					{
						for (auto& selected : m_selection.GetSelectedObjects())
						{
							selected->Remove();
						}

						m_selection.Clear();
					}
				}
			}

			if (m_editMode == WorldEditMode::StaticMapEntities)
			{
				if (ImGui::BeginDragDropTarget())
				{
					// We only accept mesh file drops
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
					{
						Vector3 position = Vector3::Zero;

						const ImVec2 mousePos = ImGui::GetMousePos();
						const Plane plane = Plane(Vector3::UnitY, Vector3::Zero);
						const Ray ray = m_camera->GetCameraToViewportRay(
							(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
							(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y,
							10000.0f);
						const auto hit = ray.Intersects(plane);
						if (hit.first)
						{
							position = ray.GetPoint(hit.second);
						}
						else
						{
							position = ray.GetPoint(10.0f);
						}

						// Snap to grid?
						if (m_gridSnap)
						{
							const float gridSize = m_translateSnapSizes[m_currentTranslateSnapSize];

							// Snap position to grid size
							position.x = std::round(position.x / gridSize) * gridSize;
							position.y = std::round(position.y / gridSize) * gridSize;
							position.z = std::round(position.z / gridSize) * gridSize;
						}

						CreateMapEntity(*static_cast<String*>(payload->Data), position, Quaternion::Identity, Vector3::UnitScale);
					}
					ImGui::EndDragDropTarget();
				}
		    }
			
			ImGui::SetItemAllowOverlap();
			ImGui::SetCursorPos(ImVec2(16, 16));
			
			if (ImGui::Button("Toggle Grid"))
			{
				m_worldGrid->SetVisible(!m_worldGrid->IsVisible());
			}
			ImGui::SameLine();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();


			if (ImGui::Checkbox("Snap", &m_gridSnap))
			{
				m_transformWidget->SetSnapping(m_gridSnap);
			}
			ImGui::SameLine();

			if (m_gridSnap)
			{
				static const char* s_translategridSizes[] = { "0.1", "0.25", "0.5", "1.0", "1.5", "2.0", "4.0" };
				static const char* s_rotateSnapSizes[] = { "1", "5", "10", "15", "45", "90" };

				const char* previewValue = nullptr;

				switch (m_transformWidget->GetTransformMode())
				{
				case TransformMode::Translate:
				case TransformMode::Scale:
					previewValue = s_translategridSizes[m_currentTranslateSnapSize];
					break;
				case TransformMode::Rotate:
					previewValue = s_rotateSnapSizes[m_currentRotateSnapSize];
					break;
				}

				ImGui::SetNextItemWidth(50.0f);

				if (ImGui::BeginCombo("##snapSizes", previewValue, ImGuiComboFlags_None))
				{
					switch (m_transformWidget->GetTransformMode())
					{
					case TransformMode::Translate:
					case TransformMode::Scale:
						for (int i = 0; i < std::size(s_translategridSizes); ++i)
						{
							const bool isSelected = i == m_currentTranslateSnapSize;
							if (ImGui::Selectable(s_translategridSizes[i], isSelected))
							{
								m_currentTranslateSnapSize = i;
								m_transformWidget->SetTranslateSnapSize(m_translateSnapSizes[m_currentTranslateSnapSize]);
							}
							if (isSelected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}
						break;
					case TransformMode::Rotate:
						for (int i = 0; i < std::size(s_rotateSnapSizes); ++i)
						{
							const bool isSelected = i == m_currentRotateSnapSize;
							if (ImGui::Selectable(s_rotateSnapSizes[i], isSelected))
							{
								m_currentRotateSnapSize = i;
								m_transformWidget->SetRotateSnapSize(m_rotateSnapSizes[m_currentRotateSnapSize]);
							}
							if (isSelected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}
					}

					ImGui::EndCombo();
				}
			}
		}
		ImGui::End();
		
		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockspaceId);
			ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar); // Add empty node
			ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockspaceId;
			const auto sideId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 400.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);

			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(detailsId.c_str(), sideId);
			ImGui::DockBuilderDockWindow(worldSettingsId.c_str(), sideId);
			
			ImGui::DockBuilderFinish(dockspaceId);
			m_initDockLayout = false;
			
			auto* wnd = ImGui::FindWindowByName(viewportId.c_str());
			if (wnd)
			{
				ImGuiDockNode* node = wnd->DockNode;
				node->WantHiddenTabBarToggle = true;
			}
		}

		ImGui::PopID();
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
			if (m_editMode == WorldEditMode::StaticMapEntities)
			{
				if (!widgetWasActive && button == 0 && m_hovering)
				{
					PerformEntitySelectionRaycast(
						(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
						(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
				}
			}
			else if (m_editMode == WorldEditMode::Spawns)
			{
				if (!widgetWasActive && button == 0 && m_hovering)
				{
					PerformSpawnSelectionRaycast(
						(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
						(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
				}
			}
			else if (m_editMode == WorldEditMode::Terrain)
			{
				if (m_terrainEditMode == TerrainEditMode::Select)
				{
					PerformTerrainSelectionRaycast(
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

			// TODO: This shit needs to be changed so that each tool has it's own camera controls or something like that
			if (m_rightButtonPressed || (m_leftButtonPressed && (m_editMode != WorldEditMode::Terrain || (m_terrainEditMode != TerrainEditMode::Deform && m_terrainEditMode != TerrainEditMode::Paint))))
			{
				m_cameraAnchor->Yaw(-Degree(deltaX * 90.0f * deltaTimeSeconds), TransformSpace::World);
				m_cameraAnchor->Pitch(-Degree(deltaY * 90.0f * deltaTimeSeconds), TransformSpace::Local);
			}

			m_lastMouseX = x;
			m_lastMouseY = y;
		}

		const auto mousePos = ImGui::GetMousePos();
		m_transformWidget->OnMouseMoved(
			(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
			(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);

		if (m_editMode == WorldEditMode::Terrain)
		{
			if (m_terrainEditMode != TerrainEditMode::Select)
			{
				OnTerrainMouseMoved(
					(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
					(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
			}
		}
	}

	void WorldEditorInstance::Save()
	{
		// Build mesh name index map
		std::map<String, uint32> entityNames;
		for (const auto& mapEntity : m_mapEntities)
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
			return;
		}

		io::StreamSink sink{ *streamPtr };
		io::Writer writer{ sink };

		// Start writing file
		writer
			<< io::write<uint32>(*versionChunk)
			<< io::write<uint32>(sizeof(uint32))
			<< io::write<uint32>(2);

		uint32 meshSize = 0;
		std::vector<const String*> sortedNames(entityNames.size());
		for (auto& name : entityNames)
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
		for (const auto& name : sortedNames)
		{
			writer << io::write_range(*name) << io::write<uint8>(0);
		}

		struct MaterialOverride
		{
			uint8 materialIndex;
			String materialName;
		};

		// Write entities
		for (const auto& ent : m_mapEntities)
		{
			ChunkWriter chunkWriter(entityChunk, writer);

			MapEntityChunkContent content;
			content.meshNameIndex = entityNames[String(ent->GetEntity().GetMesh()->GetName())];
			content.position = ent->GetSceneNode().GetDerivedPosition();
			content.rotation = ent->GetSceneNode().GetDerivedOrientation();
			content.scale = ent->GetSceneNode().GetDerivedScale();
			content.uniqueId = 0;	// TODO: Unique ID
			writer.WritePOD(content);

			std::vector<MaterialOverride> materialOverrides;

			// Write number of material overrides
			for (uint8 i = 0; i < ent->GetEntity().GetNumSubEntities(); ++i)
			{
				SubEntity* sub = ent->GetEntity().GetSubEntity(i);
				ASSERT(sub);

				SubMesh& submesh = ent->GetEntity().GetMesh()->GetSubMesh(i);
				if (const bool hasDifferentMaterial = sub->GetMaterial() && sub->GetMaterial() != submesh.GetMaterial())
				{
					materialOverrides.emplace_back(i, String(sub->GetMaterial()->GetName()));
				}
			}

			// Serialize material overrides
			writer << io::write<uint8>(materialOverrides.size());
			for (uint8 i = 0; i < materialOverrides.size(); ++i)
			{
				writer
					<< io::write<uint8>(materialOverrides[i].materialIndex)
					<< io::write_dynamic_range<uint16>(materialOverrides[i].materialName);
			}
			chunkWriter.Finish();
		}
		
		// TODO
		sink.Flush();

		ILOG("Successfully saved world file " << GetAssetPath());

		// Save terrain
		for (uint32 x = 0; x < 64; ++x)
		{
			for (uint32 y = 0; y < 64; ++y)
			{
				terrain::Page* page = m_terrain->GetPage(x, y);
				if (page && page->IsPrepared() && page->IsChanged())
				{
					// Page was loaded, so save any changes
					page->Save();
				}
			}
		}
	}

	void WorldEditorInstance::UpdateDebugAABB(const AABB& aabb)
	{
		m_debugBoundingBox->Clear();

		auto lineListOp = m_debugBoundingBox->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/WorldGrid.hmat"));

		lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.min.z), Vector3(aabb.max.x, aabb.min.y, aabb.min.z));
		lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.min.z), Vector3(aabb.min.x, aabb.max.y, aabb.min.z));
		lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.min.z), Vector3(aabb.min.x, aabb.min.y, aabb.max.z));

		lineListOp->AddLine(Vector3(aabb.max.x, aabb.max.y, aabb.max.z), Vector3(aabb.min.x, aabb.max.y, aabb.max.z));
		lineListOp->AddLine(Vector3(aabb.max.x, aabb.max.y, aabb.max.z), Vector3(aabb.max.x, aabb.min.y, aabb.max.z));
		lineListOp->AddLine(Vector3(aabb.max.x, aabb.max.y, aabb.max.z), Vector3(aabb.max.x, aabb.max.y, aabb.min.z));

		lineListOp->AddLine(Vector3(aabb.max.x, aabb.min.y, aabb.min.z), Vector3(aabb.max.x, aabb.min.y, aabb.max.z));
		lineListOp->AddLine(Vector3(aabb.max.x, aabb.min.y, aabb.min.z), Vector3(aabb.max.x, aabb.max.y, aabb.min.z));

		lineListOp->AddLine(Vector3(aabb.min.x, aabb.max.y, aabb.min.z), Vector3(aabb.min.x, aabb.max.y, aabb.max.z));
		lineListOp->AddLine(Vector3(aabb.min.x, aabb.max.y, aabb.min.z), Vector3(aabb.max.x, aabb.max.y, aabb.min.z));

		lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.max.z), Vector3(aabb.max.x, aabb.min.y, aabb.max.z));
		lineListOp->AddLine(Vector3(aabb.min.x, aabb.min.y, aabb.max.z), Vector3(aabb.min.x, aabb.max.y, aabb.max.z));

		// TODO: Missing lines (6)
	}

	void WorldEditorInstance::PerformEntitySelectionRaycast(const float viewportX, const float viewportY)
	{
		const Ray ray = m_camera->GetCameraToViewportRay(viewportX, viewportY, 10000.0f);
		m_raySceneQuery->SetRay(ray);
		m_raySceneQuery->SetSortByDistance(true);
		m_raySceneQuery->SetQueryMask(SceneQueryFlags_Entity);
		m_raySceneQuery->ClearResult();
		m_raySceneQuery->Execute();

		if (!ImGui::IsKeyPressed(ImGuiKey_LeftShift))
		{
			m_selection.Clear();
		}

		m_debugBoundingBox->Clear();

		const auto& hitResult = m_raySceneQuery->GetLastResult();
		if (!hitResult.empty())
		{
			Entity* entity = (Entity*)hitResult[0].movable;
			if (entity)
			{
				MapEntity* mapEntity = entity->GetUserObject<MapEntity>();
				if (mapEntity)
				{
					String asset = entity->GetMesh()->GetName().data();
					m_selection.AddSelectable(std::make_unique<SelectedMapEntity>(*mapEntity, [this, asset](Selectable& selected)
					{
						CreateMapEntity(asset, selected.GetPosition(), selected.GetOrientation(), selected.GetScale());
					}));
					UpdateDebugAABB(hitResult[0].movable->GetWorldBoundingBox());
				}
			}
		}
	}

	void WorldEditorInstance::PerformSpawnSelectionRaycast(float viewportX, float viewportY)
	{
		const Ray ray = m_camera->GetCameraToViewportRay(viewportX, viewportY, 10000.0f);
		m_raySceneQuery->SetRay(ray);
		m_raySceneQuery->SetSortByDistance(true);
		m_raySceneQuery->SetQueryMask(SceneQueryFlags_Spawns);
		m_raySceneQuery->ClearResult();
		m_raySceneQuery->Execute();

		m_selection.Clear();
		m_debugBoundingBox->Clear();

		const auto& hitResult = m_raySceneQuery->GetLastResult();
		if (!hitResult.empty())
		{
			Entity* entity = (Entity*)hitResult[0].movable;
			if (entity)
			{
				proto::UnitSpawnEntry* spawnEntry = entity->GetUserObject<proto::UnitSpawnEntry>();
				if (spawnEntry)
				{
					m_selection.AddSelectable(std::make_unique<SelectedUnitSpawn>(*spawnEntry, [this, spawnEntry](Selectable& selected)
						{
							// TODO: Implement
						}));
					UpdateDebugAABB(hitResult[0].movable->GetWorldBoundingBox());
				}
			}
		}
	}

	void WorldEditorInstance::PerformTerrainSelectionRaycast(float viewportX, float viewportY)
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

		if (terrain::Tile* tile = hitResult.second.tile)
		{
			m_selection.AddSelectable(std::make_unique<SelectedTerrainTile>(*tile));
			UpdateDebugAABB(tile->GetPage().GetBoundingBox());
		}

		m_debugNode->SetPosition(hitResult.second.position);
		m_debugEntity->SetVisible(true);
	}

	void WorldEditorInstance::OnTerrainMouseMoved(float viewportX, float viewportY)
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

		m_brushPosition = hitResult.second.position;

		if (terrain::Tile* tile = hitResult.second.tile)
		{
			UpdateDebugAABB(tile->GetWorldBoundingBox());
			m_debugBoundingBox->SetVisible(true);
		}
		else
		{
			m_debugBoundingBox->SetVisible(false);
		}

		m_debugNode->SetPosition(hitResult.second.position);
		m_debugEntity->SetVisible(true);
	}

	Entity* WorldEditorInstance::CreateMapEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale)
	{
		const String uniqueId = "Entity_" + std::to_string(m_objectIdGenerator.GenerateId());
		Entity* entity = m_scene.CreateEntity(uniqueId, assetName);
		if (entity)
		{
			entity->SetQueryFlags(SceneQueryFlags_Entity);

			auto& node = m_scene.CreateSceneNode(uniqueId);
			m_scene.GetRootSceneNode().AddChild(node);
			node.AttachObject(*entity);
			node.SetPosition(position);
			node.SetOrientation(orientation);
			node.SetScale(scale);

			const auto& mapEntity = m_mapEntities.emplace_back(std::make_unique<MapEntity>(m_scene, node, *entity));
			mapEntity->remove.connect(this, &WorldEditorInstance::OnMapEntityRemoved);
			entity->SetUserObject(m_mapEntities.back().get());
		}

		return entity;
	}

	Entity* WorldEditorInstance::CreateUnitSpawnEntity(proto::UnitSpawnEntry& spawn)
	{
		proto::Project& project = m_editor.GetProject();

		// TODO: Use different mesh file?
		String meshFile = "Editor/Joint.hmsh";

		if (const auto* unit = project.units.getById(spawn.unitentry()))
		{
			const uint32 modelId = unit->malemodel() ? unit->malemodel() : unit->femalemodel();

			if (modelId == 0)
			{
				// TODO: Maybe spawn a dummy unit in editor later, so we can at least see, select and delete / modify the spawn
				WLOG("No model id assigned!");
			}
			else if (const auto* model = project.models.getById(modelId))
			{
				meshFile = model->filename();
			}
			else
			{
				WLOG("Model " << modelId << " not found!");
			}
		}
		else
		{
			WLOG("Spawn point of non-existant unit " << spawn.unitentry() << " found");
		}

		const String uniqueId = "UnitSpawn_" + std::to_string(m_unitSpawnIdGenerator.GenerateId());
		Entity* entity = m_scene.CreateEntity(uniqueId, meshFile);
		if (entity)
		{
			entity->SetQueryFlags(SceneQueryFlags_Spawns);

			auto& node = m_scene.CreateSceneNode(uniqueId);
			m_scene.GetRootSceneNode().AddChild(node);
			node.AttachObject(*entity);
			node.SetPosition(Vector3(spawn.positionx(), spawn.positiony(), spawn.positionz()));
			node.SetOrientation(Quaternion(Radian(spawn.rotation()), Vector3::UnitY));
			node.SetScale(Vector3::UnitScale);

			// TODO: Is this safe? Does protobuf move the object around in memory?
			entity->SetUserObject(&spawn);
		}

		return entity;
	}

	void WorldEditorInstance::OnMapEntityRemoved(MapEntity& entity)
	{
		m_mapEntities.erase(std::remove_if(m_mapEntities.begin(), m_mapEntities.end(), [&entity](const auto& mapEntity)
		{
			return mapEntity.get() == &entity;
		}), m_mapEntities.end());
	}

	PagePosition WorldEditorInstance::GetPagePositionFromCamera() const
	{
		const auto& camPos = m_camera->GetDerivedPosition();

		const auto pagePos = PagePosition(
			static_cast<uint32>(floor(camPos.x / terrain::constants::PageSize)) + 32,
			static_cast<uint32>(floor(camPos.z / terrain::constants::PageSize)) + 32);

		return pagePos;
	}

	void WorldEditorInstance::SetMapEntry(proto::MapEntry* entry)
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

	void WorldEditorInstance::OnPageAvailabilityChanged(const PageNeighborhood& page, const bool isAvailable)
	{
		if (!m_terrain)
		{
			return;
		}

		const auto &mainPage = page.GetMainPage();
		const PagePosition &pos = mainPage.GetPosition();

		if (isAvailable)
		{
			m_terrain->PreparePage(pos.x(), pos.y());
			m_terrain->LoadPage(pos.x(), pos.y());
		}
		else
		{
			m_terrain->UnloadPage(pos.x(), pos.y());
		}
	}

	void WorldEditorInstance::Visit(SelectedMapEntity& selectable)
	{
		static const char* s_noMaterialPreview = "<None>";

		MapEntity& mapEntity = selectable.GetEntity();
		Entity& entity = mapEntity.GetEntity();

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
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
				{
					entity.SetMesh(MeshManager::Get().Load(*static_cast<String*>(payload->Data)));
				}

				ImGui::EndDragDropTarget();
			}
		}
		
		if (ImGui::CollapsingHeader("Materials"))
		{
			for (uint32 i = 0; i < entity.GetNumSubEntities(); ++i)
			{
				SubEntity* sub = entity.GetSubEntity(i);
				ASSERT(sub);

				SubMesh& submesh = entity.GetMesh()->GetSubMesh(i);

				ImGui::PushID(i);

				// Get material
				MaterialPtr material = sub->GetMaterial();
				if (!material)
				{
					material = submesh.GetMaterial();
				}

				// Build preview string
				const char* previewString = s_noMaterialPreview;
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
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmat"))
					{
						sub->SetMaterial(MaterialManager::Get().Load(*static_cast<String*>(payload->Data)));
					}

					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmi"))
					{
						sub->SetMaterial(MaterialManager::Get().Load(*static_cast<String*>(payload->Data)));
					}

					ImGui::EndDragDropTarget();
				}

				ImGui::PopID();
			}
		}
	}

	void WorldEditorInstance::Visit(SelectedTerrainTile& selectable)
	{
		if (ImGui::CollapsingHeader("Tile"))
		{
			static const char* s_noMaterialPreview = "<None>";

			terrain::Tile& tile = selectable.GetTile();

			ImGui::Text("Tile Grid: %d x %d", tile.GetX(), tile.GetY());
			ImGui::Text("Page Grid: %d x %d", tile.GetPage().GetX(), tile.GetPage().GetY());

			// Get material
			MaterialPtr material = tile.GetMaterial();

			// Build preview string
			const char* previewString = s_noMaterialPreview;
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
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmat"))
				{
					tile.SetMaterial(MaterialManager::Get().Load(*static_cast<String*>(payload->Data)));
				}

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmi"))
				{
					tile.SetMaterial(MaterialManager::Get().Load(*static_cast<String*>(payload->Data)));
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

	bool WorldEditorInstance::ReadMVERChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
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

		if (m_worldFileVersion < 1 || m_worldFileVersion > 2)
		{
			ELOG("Detected unsuppoted file format version!");
			return false;
		}

		AddChunkHandler(*meshChunk, false, *this, &WorldEditorInstance::ReadMeshChunk);
		AddChunkHandler(*terrainChunk, false, *this, &WorldEditorInstance::ReadTerrainChunk);

		return reader;
	}

	bool WorldEditorInstance::ReadMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *meshChunk);

		// Only when we have read mesh names we support reading entity chunks otherwise entities would refer to meshes which we don't know about!
		if (m_worldFileVersion >= 2)
		{
			AddChunkHandler(*entityChunk, false, *this, &WorldEditorInstance::ReadEntityChunkV2);
		}
		else
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

	bool WorldEditorInstance::ReadEntityChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
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

		CreateMapEntity(m_meshNames[content.meshNameIndex], content.position, content.rotation, content.scale);

		return reader;
	}

	bool WorldEditorInstance::ReadEntityChunkV2(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
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
		if (!(reader
			>> io::read<uint32>(uniqueId)
			>> io::read<uint32>(meshNameIndex)
			>> io::read<float>(position.x)
			>> io::read<float>(position.y)
			>> io::read<float>(position.z)
			>> io::read<float>(rotation.w)
			>> io::read<float>(rotation.x)
			>> io::read<float>(rotation.y)
			>> io::read<float>(rotation.z)
			>> io::read<float>(scale.x)
			>> io::read<float>(scale.y)
			>> io::read<float>(scale.z)
			))
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

		if (Entity* entity = CreateMapEntity(m_meshNames[meshNameIndex], position, rotation, scale))
		{
			// Apply material overrides
			for (const auto& materialOverride : materialOverrides)
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

	bool WorldEditorInstance::ReadTerrainChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *terrainChunk);

		// Read chunk only once
		RemoveChunkHandler(*terrainChunk);

		String defaultTerrainMaterial;
		reader
			>> io::read<uint8>(m_hasTerrain)
			>> io::read_container<uint16>(defaultTerrainMaterial);

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

	bool WorldEditorInstance::OnReadFinished() noexcept
	{
		m_meshNames.clear();
		RemoveAllChunkHandlers();

		return ChunkReader::OnReadFinished();
	}
}
