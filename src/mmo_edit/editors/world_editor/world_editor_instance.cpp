// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "world_editor_instance.h"

#include <imgui_internal.h>

#include "editor_host.h"
#include "world_editor.h"
#include "world_page_loader.h"
#include "assets/asset_registry.h"
#include "editors/material_editor/node_layout.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/scene_node.h"
#include "selected_map_entity.h"
#include "stream_sink.h"

namespace mmo
{
	/// @brief Default value. Object will not be hit by any scene query.
	static constexpr uint32 SceneQueryFlags_None = 0;

	/// @brief Used for map entities.
	static constexpr uint32 SceneQueryFlags_Entity = 1 << 0;

	constexpr uint32 versionHeader = 'MVER';
	constexpr uint32 meshHeader = 'MESH';
	constexpr uint32 entityHeader = 'MENT';

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

		// TODO: Load map file
		std::unique_ptr<std::istream> streamPtr = AssetRegistry::OpenFile(GetAssetPath().string());
		if (!streamPtr)
		{
			ELOG("Failed to load world file '" << GetAssetPath() << "'");
			return;
		}

		io::StreamSource source{ *streamPtr };
		io::Reader reader{ source };

		// Read mver chunk
		uint32 chunkHeader, chunkSize;
		if (!(reader >> io::read<uint32>(chunkHeader) >> io::read<uint32>(chunkSize)))
		{
			ELOG("Failed to read world file: Unexpected end of file");
			return;
		}

		if (chunkHeader != versionHeader)
		{
			ELOG("Failed to read world file: Expected version header chunk, but found chunk '" << log_hex_digit(chunkHeader) << "'");
			return;
		}
		if (chunkSize != sizeof(uint32))
		{
			ELOG("Failed to read world file: Invalid version header chunk size");
			return;
		}

		uint32 version;
		if (!(reader >> io::read<uint32>(version)))
		{
			ELOG("Failed to read world file: Unexpected end of file");
			return;
		}

		if (version != 0x0001)
		{
			ELOG("Failed to read world file: Unsupported version '" << log_hex_digit(version) << "'");
			return;
		}

		// Read mesh name chunk
		if (!(reader >> io::read<uint32>(chunkHeader) >> io::read<uint32>(chunkSize)))
		{
			ELOG("Failed to read world file: Unexpected end of file");
			return;
		}
		if (chunkHeader != meshHeader)
		{
			ELOG("Failed to read world file: Expected mesh header chunk, but found chunk '" << log_hex_digit(chunkHeader) << "'");
			return;
		}

		if (chunkSize <= 0)
		{
			return;
		}

		const size_t contentStart = source.position();
		std::vector<String> meshNames;
		while(source.position() - contentStart < chunkSize)
		{
			String meshName;
			if (!(reader >> io::read_string(meshName)))
			{
				ELOG("Failed to read world file: Unexpected end of file");
				return;
			}

			meshNames.emplace_back(std::move(meshName));
			DLOG("\tMesh name: " << meshNames.back());
		}

		// Read entities
		while(reader)
		{
			if (!(reader >> io::read<uint32>(chunkHeader) >> io::read<uint32>(chunkSize)))
			{
				break;
			}

			if (chunkHeader != entityHeader)
			{
				ELOG("Failed to read world file: Expected entity header chunk, but found chunk '" << log_hex_digit(chunkHeader) << "'");
				return;
			}

			if (chunkSize != sizeof(MapEntityChunkContent))
			{
				ELOG("Failed to read world file: Invalid entity header chunk size: Expected '" << sizeof(MapEntityChunkContent) << "' but got '" << chunkSize << "'");
				return;
			}

			MapEntityChunkContent content;
			reader.readPOD(content);
			if (!reader)
			{
				ELOG("Failed to read world file: Unexpected end of file");
				return;
			}

			if (content.meshNameIndex >= meshNames.size())
			{
				ELOG("Failed to read world file: Invalid mesh name index");
				return;
			}

			CreateMapEntity(meshNames[content.meshNameIndex], content.position, content.rotation, content.scale);
		}

		// Setup terrain
		m_terrain = std::make_unique<terrain::Terrain>(m_scene, m_camera, 64, 64);
		
		m_cloudsEntity = m_scene.CreateEntity("Clouds", "Models/SkySphere.hmsh");
		m_cloudsEntity->SetRenderQueueGroup(SkiesEarly);
		m_cloudsEntity->SetQueryFlags(0);
		m_cloudsNode = &m_scene.CreateSceneNode("Clouds");
		m_cloudsNode->AttachObject(*m_cloudsEntity);
		m_cloudsNode->SetScale(Vector3::UnitScale * 40.0f);
		m_scene.GetRootSceneNode().AddChild(*m_cloudsNode);
		
		ILOG("Successfully read world file!");
	}

	WorldEditorInstance::~WorldEditorInstance()
	{
		// Stop background loading thread
		m_work.reset();
		m_workQueue.stop();
		m_dispatcher.stop();
		m_backgroundLoader.join();

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

		m_cloudsNode->SetPosition(m_camera->GetDerivedPosition());

		m_cameraAnchor->Translate(m_cameraVelocity * deltaTimeSeconds, TransformSpace::Local);
		m_cameraVelocity *= powf(0.025f, deltaTimeSeconds);

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

	void WorldEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockspaceId = ImGui::GetID("##model_dockspace_");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();
		const String worldSettingsId = "World Settings##" + GetAssetPath().string();

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

			if (!m_selection.IsEmpty())
			{
				// Transform rotation into human readable format
				Radian x, y, z;
				Matrix3 rot;
				m_selection.GetSelectedObjects().back()->GetOrientation().ToRotationMatrix(rot);
				rot.ToEulerAnglesXYZ(x, y, z);
				float angles[3] = { x.GetValueDegrees(), y.GetValueDegrees(), z.GetValueDegrees() };

				Vector3 position = m_selection.GetSelectedObjects().back()->GetPosition();
				Vector3 scale = m_selection.GetSelectedObjects().back()->GetScale();

				if (ImGui::InputFloat3("Position", position.Ptr()))
				{
					m_selection.GetSelectedObjects().back()->SetPosition(position);
				}
				if (ImGui::InputFloat3("Rotation", angles))
				{
					rot.FromEulerAnglesXYZ(Degree(angles[0]), Degree(angles[1]), Degree(angles[2]));
					const Quaternion quat{ rot };
					m_selection.GetSelectedObjects().back()->SetOrientation(quat);
				}
				if (ImGui::InputFloat3("Scale", scale.Ptr()))
				{
					m_selection.GetSelectedObjects().back()->SetScale(scale);
				}
			}
		}
		ImGui::End();
		
		if (ImGui::Begin(worldSettingsId.c_str()))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
		    if (ImGui::BeginTable("settings", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
		    {

		        ImGui::EndTable();
		    }
		    ImGui::PopStyleVar();
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
						const float gridSize = m_gridSizes[m_currentGridSizeIndex];

						// Snap position to grid size
						position.x = std::round(position.x / gridSize) * gridSize;
						position.y = std::round(position.y / gridSize) * gridSize;
						position.z = std::round(position.z / gridSize) * gridSize;
					}

					CreateMapEntity(*static_cast<String*>(payload->Data), position, Quaternion::Identity, Vector3::UnitScale);
		        }
		        ImGui::EndDragDropTarget();
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
				m_transformWidget->SetSnapToGrid(m_gridSnap, m_gridSizes[m_currentGridSizeIndex]);
			}
			ImGui::SameLine();

			static const char* gridSizes[] = { "0.1", "0.25", "0.5", "1.0", "1.5", "2.0", "4.0" };
			if (ImGui::BeginCombo("##gridSizes", nullptr, ImGuiComboFlags_NoPreview))
			{
				for (int i = 0; i < 7; ++i)
				{
					const bool isSelected = i == m_currentGridSizeIndex;
					if (ImGui::Selectable(gridSizes[i], isSelected))
					{
						m_currentGridSizeIndex = i;
						m_transformWidget->SetSnapToGrid(m_gridSnap, m_gridSizes[m_currentGridSizeIndex]);
					}
					if (isSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
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

		if (!widgetWasActive && button == 0 && m_hovering)
		{
			PerformEntitySelectionRaycast(
				(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
				(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
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

			if (m_leftButtonPressed || m_rightButtonPressed)
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
			<< io::write<uint32>(versionHeader)
			<< io::write<uint32>(sizeof(uint32))
			<< io::write<uint32>(0x0001);

		uint32 meshSize = 0;
		std::vector<const String*> sortedNames(entityNames.size());
		for (auto& name : entityNames)
		{
			sortedNames[name.second] = &name.first;
			meshSize += name.first.size() + 1;
		}

		// Write mesh names
		writer
			<< io::write<uint32>(meshHeader)
			<< io::write<uint32>(meshSize);
		for (const auto& name : sortedNames)
		{
			writer << io::write_range(*name) << io::write<uint8>(0);
		}

		// Write entities
		for (const auto& ent : m_mapEntities)
		{
			writer
				<< io::write<uint32>(entityHeader)
				<< io::write<uint32>(sizeof(MapEntityChunkContent));

			MapEntityChunkContent content;
			content.meshNameIndex = entityNames[String(ent->GetEntity().GetMesh()->GetName())];
			content.position = ent->GetSceneNode().GetDerivedPosition();
			content.rotation = ent->GetSceneNode().GetDerivedOrientation();
			content.scale = ent->GetSceneNode().GetDerivedScale();
			content.uniqueId = 0;	// TODO: Unique ID
			writer.WritePOD(content);
		}
		
		// TODO
		sink.Flush();

		ILOG("Successfully saved world file " << GetAssetPath());
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
					m_selection.AddSelectable(std::make_unique<SelectedMapEntity>(*mapEntity));
					UpdateDebugAABB(hitResult[0].movable->GetWorldBoundingBox());
				}
			}
		}
	}

	void WorldEditorInstance::CreateMapEntity(const String& assetName, const Vector3& position,
		const Quaternion& orientation, const Vector3& scale)
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
		return PagePosition(static_cast<uint32>(
			32 - floor(camPos.x / terrain::constants::PageSize)),
			32 - static_cast<uint32>(floor(camPos.z / terrain::constants::PageSize)));

	}

	void WorldEditorInstance::OnPageAvailabilityChanged(const PageNeighborhood& page, const bool isAvailable)
	{
		const auto &mainPage = page.GetMainPage();
		const PagePosition &pos = mainPage.GetPosition();

		if (isAvailable)
		{
			DLOG("Page " << mainPage.GetPosition() << " is available!");
			m_terrain->PreparePage(pos.x(), pos.y());
			m_terrain->LoadPage(pos.x(), pos.y());
		}
		else
		{
			DLOG("Page " << mainPage.GetPosition() << " is unavailable");
			m_terrain->UnloadPage(pos.x(), pos.y());
		}
	}
}
