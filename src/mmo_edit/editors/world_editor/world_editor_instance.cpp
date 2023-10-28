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
#include "selected_entity.h"

namespace mmo
{
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
		m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "DebugAxis");
			m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());

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

		const auto& camPos = m_camera->GetDerivedPosition();
		const PagePosition pos(camPos.x / (m_worldGrid->GetGridSize() * m_worldGrid->GetLargeGridInterval()), camPos.z / (m_worldGrid->GetGridSize() * m_worldGrid->GetLargeGridInterval()));

		const auto addWork = [&workQueue](const WorldPageLoader::Work &work)
		{
			workQueue.post(work);
		};
		const auto synchronize = [&dispatcher](const WorldPageLoader::Work &work)
		{
			dispatcher.post(work);
		};

		m_visibleSection = std::make_unique<LoadedPageSection>(pos, 1, *this);
		m_pageLoader = std::make_unique<WorldPageLoader>(*m_visibleSection, addWork, synchronize);
		
		m_raySceneQuery = m_scene.CreateRayQuery(Ray(Vector3::Zero, Vector3::UnitZ));
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

	}

	WorldEditorInstance::~WorldEditorInstance()
	{
		// Stop background loading thread
		m_work.reset();
		m_workQueue.stop();
		m_dispatcher.stop();
		m_backgroundLoader.join();

		m_worldGrid.reset();
		m_axisDisplay.reset();
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

		m_cameraAnchor->Translate(m_cameraVelocity * deltaTimeSeconds, TransformSpace::Local);
		m_cameraVelocity *= powf(0.025f, deltaTimeSeconds);
		
		const auto& camPos = m_camera->GetDerivedPosition();
		const PagePosition pos(camPos.x / 533.3333f, camPos.z / 533.3333f);
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

		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);
		
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

			if (ImGui::IsItemHovered())
			{
				m_cameraSpeed = std::max(std::min(m_cameraSpeed + ImGui::GetIO().MouseWheel * 5.0f, 200.0f), 1.0f);

				m_leftButtonPressed = ImGui::IsMouseDown(ImGuiMouseButton_Left);
				m_rightButtonPressed = ImGui::IsMouseDown(ImGuiMouseButton_Right);

				const auto mousePos = ImGui::GetMousePos();
				const auto contentRectMin = ImGui::GetWindowPos();
				m_lastContentRectMin = contentRectMin;
			}

			if (ImGui::BeginDragDropTarget())
		    {
				// We only accept mesh file drops
		        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
		        {
					const String uniqueId = "Entity_" + std::to_string(m_objectIdGenerator.GenerateId());
					Entity* entity = m_scene.CreateEntity(uniqueId, *static_cast<String*>(payload->Data));
					auto& node = m_scene.CreateSceneNode(uniqueId);
					node.AttachObject(*entity);
					m_scene.GetRootSceneNode().AddChild(node);

					m_mapEntities.emplace_back(MapEntity{ &node, entity });
					entity->SetUserObject(&m_mapEntities.back());
		        }
		        ImGui::EndDragDropTarget();
		    }
			
			ImGui::SetItemAllowOverlap();
			ImGui::SetCursorPos(ImVec2(16, 16));
			
			ImGui::Button("Toggle Grid");
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

		const auto mousePos = ImGui::GetMousePos();
		m_transformWidget->OnMousePressed(button, (mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x, (mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
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

		if (!widgetWasActive && button == 0)
		{
			const Ray ray = m_camera->GetCameraToViewportRay(
				(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
				(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y,
				10000.0f);
			m_raySceneQuery->SetRay(ray);
			m_raySceneQuery->SetSortByDistance(true);
			m_raySceneQuery->ClearResult();
			m_raySceneQuery->Execute();

			m_selection.Clear();

			const auto& hitResult = m_raySceneQuery->GetLastResult();
			if (!hitResult.empty())
			{
				m_selection.AddSelectable(std::make_unique<SelectedEntity>(*(Entity*)hitResult[0].movable));
				UpdateDebugAABB(hitResult[0].movable->GetWorldBoundingBox());
			}
			else
			{
				m_debugBoundingBox->Clear();
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
		// Save all map entity names

		// Save all tiles

	}

	void WorldEditorInstance::UpdateDebugAABB(const AABB& aabb)
	{
		m_debugBoundingBox->Clear();

		auto lineListOp = m_debugBoundingBox->AddLineListOperation();

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

	void WorldEditorInstance::OnPageAvailabilityChanged(const PageNeighborhood& page, const bool isAvailable)
	{
		const auto &mainPage = page.GetMainPage();
		const auto &pos = mainPage.GetPosition();

		if (isAvailable)
		{
			DLOG("Page " << mainPage.GetPosition() << " is available!");
		}
		else
		{
			DLOG("Page " << mainPage.GetPosition() << " is unavailable");
		}
	}
}
