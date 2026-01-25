// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_editor_instance.h"

#include <imgui_internal.h>

#include "editor_host.h"
#include "world_model_editor.h"
#include "paging/world_page_loader.h"
#include "assets/asset_registry.h"
#include "editors/material_editor/node_editor/node_layout.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/world_model_serializer.h"
#include "selected_map_entity.h"
#include "stream_sink.h"
#include "editors/world_editor/world_editor_instance.h"
#include "scene_graph/mesh_manager.h"
#include "terrain/page.h"
#include "terrain/tile.h"

namespace mmo
{
	// SelectedWorldModelGroup implementation
	SelectedWorldModelGroup::SelectedWorldModelGroup(WorldModelGroup& group, SceneNode& node)
		: m_group(group)
		, m_node(node)
	{
	}

	Vector3 SelectedWorldModelGroup::GetPosition() const
	{
		return m_node.GetPosition();
	}

	void SelectedWorldModelGroup::SetPosition(const Vector3& position) const
	{
		m_node.SetPosition(position);
	}

	Quaternion SelectedWorldModelGroup::GetOrientation() const
	{
		return m_node.GetOrientation();
	}

	void SelectedWorldModelGroup::SetOrientation(const Quaternion& orientation) const
	{
		m_node.SetOrientation(orientation);
	}

	Vector3 SelectedWorldModelGroup::GetScale() const
	{
		return m_node.GetScale();
	}

	void SelectedWorldModelGroup::SetScale(const Vector3& scale) const
	{
		m_node.SetScale(scale);
	}

	void SelectedWorldModelGroup::Translate(const Vector3& delta)
	{
		m_node.Translate(delta);
	}

	void SelectedWorldModelGroup::Rotate(const Quaternion& delta)
	{
		m_node.Rotate(delta);
	}

	void SelectedWorldModelGroup::Scale(const Vector3& delta)
	{
		Vector3 currentScale = m_node.GetScale();
		m_node.SetScale(currentScale + delta);
	}

	void SelectedWorldModelGroup::Remove()
	{
		removed();
	}

	// Chunk definitions
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

	WorldModelEditorInstance::WorldModelEditorInstance(EditorHost& host, WorldModelEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
		, m_wireFrame(false)
	{
		// Create a new empty world model
		m_worldModel = std::make_shared<WorldModel>();
		
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-35.0f), Vector3::UnitX));

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_worldGrid->SetQueryFlags(0);
		m_worldGrid->SetVisible(true);

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &WorldModelEditorInstance::Render);

		m_raySceneQuery = m_scene.CreateRayQuery(Ray(Vector3::Zero, Vector3::UnitZ));
		m_raySceneQuery->SetQueryMask(1);
		m_debugBoundingBox = m_scene.CreateManualRenderObject("__DebugAABB__");

		m_scene.GetRootSceneNode().AttachObject(*m_debugBoundingBox);

		m_transformWidget = std::make_unique<TransformWidget>(m_selection, m_scene, *m_camera);
		m_transformWidget->SetTransformMode(TransformMode::Translate);
		m_transformWidget->copySelection += [this]()
		{
			if (m_selection.IsEmpty())
			{
				return;
			}

			// Copy selection
			for (const auto& selected : m_selection.GetSelectedObjects())
			{
				selected->Duplicate();
			}
		};

		// Try to load existing world model file
		std::unique_ptr<std::istream> streamPtr = AssetRegistry::OpenFile(GetAssetPath().string());
		if (streamPtr)
		{
			io::StreamSource source{ *streamPtr };
			io::Reader reader{ source };

			WorldModelDeserializer deserializer(*m_worldModel);
			if (deserializer.Read(reader))
			{
				ILOG("Successfully loaded world model file: " << GetAssetPath());
				UpdateGroupVisualizations();
				UpdatePortalVisualizations();
				UpdateLightVisualizations();
			}
			else
			{
				// Try legacy format
				AddChunkHandler(*versionChunk, true, *this, &WorldModelEditorInstance::ReadMVERChunk);

				streamPtr = AssetRegistry::OpenFile(GetAssetPath().string());
				if (streamPtr)
				{
					io::StreamSource legacySource{ *streamPtr };
					io::Reader legacyReader{ legacySource };
					if (!Read(legacyReader))
					{
						WLOG("Failed to read world model file as legacy format: " << GetAssetPath());
					}
					else
					{
						ILOG("Successfully read world model file (legacy format)!");
					}
				}
			}
		}
		else
		{
			ILOG("Creating new world model file: " << GetAssetPath());
		}
	}

	WorldModelEditorInstance::~WorldModelEditorInstance()
	{
		m_transformWidget.reset();
		m_mapEntities.clear();
		
		// Clean up visualizations
		for (auto& lightVis : m_lightVisualizations)
		{
			if (lightVis.node)
			{
				m_scene.DestroySceneNode(*lightVis.node);
			}
		}
		m_lightVisualizations.clear();

		for (auto& portalVis : m_portalVisualizations)
		{
			if (portalVis.node)
			{
				m_scene.DestroySceneNode(*portalVis.node);
			}
		}
		m_portalVisualizations.clear();

		for (auto& groupVis : m_groupVisualizations)
		{
			if (groupVis.node)
			{
				m_scene.DestroySceneNode(*groupVis.node);
			}
		}
		m_groupVisualizations.clear();

		m_worldGrid.reset();
		m_scene.Clear();
	}

	void WorldModelEditorInstance::Render()
	{
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

		m_cameraAnchor->Translate(m_cameraVelocity * deltaTimeSeconds, TransformSpace::Local);
		m_cameraVelocity *= powf(0.025f, deltaTimeSeconds);

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

		m_scene.Render(*m_camera, PixelShaderType::Forward);
		m_transformWidget->Update(m_camera);
		
		m_viewportRT->Update();
	}

	void WorldModelEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockspaceId = ImGui::GetID("##worldmodel_dockspace_");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();
		const String worldSettingsId = "Settings##" + GetAssetPath().string();
		const String groupsId = "Groups##" + GetAssetPath().string();
		const String portalsId = "Portals##" + GetAssetPath().string();
		const String doodadsId = "Doodads##" + GetAssetPath().string();
		const String lightsId = "Lights##" + GetAssetPath().string();

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

		// Groups panel
		if (ImGui::Begin(groupsId.c_str()))
		{
			DrawGroupsPanel();
		}
		ImGui::End();

		// Portals panel
		if (ImGui::Begin(portalsId.c_str()))
		{
			DrawPortalsPanel();
		}
		ImGui::End();

		// Doodads panel  
		if (ImGui::Begin(doodadsId.c_str()))
		{
			DrawDoodadsPanel();
		}
		ImGui::End();

		// Lights panel
		if (ImGui::Begin(lightsId.c_str()))
		{
			DrawLightsPanel();
		}
		ImGui::End();

		// Details/Properties panel
		if (ImGui::Begin(detailsId.c_str()))
		{
			DrawPropertiesPanel();
		}
		ImGui::End();
		
		// World model settings panel
		if (ImGui::Begin(worldSettingsId.c_str()))
		{
			ImGui::Text("World Model Settings");
			ImGui::Separator();

			if (m_worldModel)
			{
				// Ambient color
				uint32 ambientColor = m_worldModel->GetAmbientColor();
				float color[4] = {
					((ambientColor >> 16) & 0xFF) / 255.0f,
					((ambientColor >> 8) & 0xFF) / 255.0f,
					(ambientColor & 0xFF) / 255.0f,
					((ambientColor >> 24) & 0xFF) / 255.0f
				};
				if (ImGui::ColorEdit4("Ambient Color", color))
				{
					uint32 newColor = 
						(static_cast<uint32>(color[3] * 255.0f) << 24) |
						(static_cast<uint32>(color[0] * 255.0f) << 16) |
						(static_cast<uint32>(color[1] * 255.0f) << 8) |
						static_cast<uint32>(color[2] * 255.0f);
					m_worldModel->SetAmbientColor(newColor);
				}

				ImGui::Separator();

				// Stats
				ImGui::Text("Groups: %zu", m_worldModel->GetGroupCount());
				ImGui::Text("Portals: %zu", m_worldModel->GetPortals().size());
				ImGui::Text("Lights: %zu", m_worldModel->GetLights().size());
				ImGui::Text("Doodad Sets: %zu", m_worldModel->GetDoodadSets().size());
				ImGui::Text("Doodads: %zu", m_worldModel->GetDoodads().size());

				ImGui::Separator();

				// Display options
				ImGui::Checkbox("Show Group Bounds", &m_showGroupBounds);
				ImGui::Checkbox("Show Portals", &m_showPortals);
				ImGui::Checkbox("Show Lights", &m_showLights);
				ImGui::Checkbox("Show Doodads", &m_showDoodads);
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
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("Viewport", std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y), RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
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
			const auto rightId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 350.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);
			const auto leftId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 280.0f / (ImGui::GetMainViewport()->Size.x - 350.0f), nullptr, &mainId);

			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(detailsId.c_str(), rightId);
			ImGui::DockBuilderDockWindow(worldSettingsId.c_str(), rightId);
			ImGui::DockBuilderDockWindow(groupsId.c_str(), leftId);
			ImGui::DockBuilderDockWindow(portalsId.c_str(), leftId);
			ImGui::DockBuilderDockWindow(doodadsId.c_str(), leftId);
			ImGui::DockBuilderDockWindow(lightsId.c_str(), leftId);
			
			ImGui::DockBuilderFinish(dockspaceId);
			m_initDockLayout = false;
			
			auto* wnd = ImGui::FindWindowByName(viewportId.c_str());
			if (wnd)
			{
				ImGuiDockNode* node = wnd->DockNode;
				if (node)
				{
					node->WantHiddenTabBarToggle = true;
				}
			}
		}

		// New group dialog
		if (m_showNewGroupDialog)
		{
			ImGui::OpenPopup("Create New Group");
			m_showNewGroupDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Group Name", m_newGroupName, sizeof(m_newGroupName));

			ImGui::Separator();
			ImGui::Text("Group Flags:");
			
			bool isInterior = (m_newGroupFlags & static_cast<uint32>(WorldModelGroupFlags::Interior)) != 0;
			if (ImGui::Checkbox("Interior", &isInterior))
			{
				if (isInterior)
				{
					m_newGroupFlags |= static_cast<uint32>(WorldModelGroupFlags::Interior);
					m_newGroupFlags &= ~static_cast<uint32>(WorldModelGroupFlags::Exterior);
				}
				else
				{
					m_newGroupFlags &= ~static_cast<uint32>(WorldModelGroupFlags::Interior);
				}
			}

			bool isExterior = (m_newGroupFlags & static_cast<uint32>(WorldModelGroupFlags::Exterior)) != 0;
			if (ImGui::Checkbox("Exterior", &isExterior))
			{
				if (isExterior)
				{
					m_newGroupFlags |= static_cast<uint32>(WorldModelGroupFlags::Exterior);
					m_newGroupFlags &= ~static_cast<uint32>(WorldModelGroupFlags::Interior);
				}
				else
				{
					m_newGroupFlags &= ~static_cast<uint32>(WorldModelGroupFlags::Exterior);
				}
			}

			bool showSkybox = (m_newGroupFlags & static_cast<uint32>(WorldModelGroupFlags::ShowSkybox)) != 0;
			if (ImGui::Checkbox("Show Skybox", &showSkybox))
			{
				if (showSkybox)
				{
					m_newGroupFlags |= static_cast<uint32>(WorldModelGroupFlags::ShowSkybox);
				}
				else
				{
					m_newGroupFlags &= ~static_cast<uint32>(WorldModelGroupFlags::ShowSkybox);
				}
			}

			bool hasLights = (m_newGroupFlags & static_cast<uint32>(WorldModelGroupFlags::HasLights)) != 0;
			if (ImGui::Checkbox("Has Lights", &hasLights))
			{
				if (hasLights)
				{
					m_newGroupFlags |= static_cast<uint32>(WorldModelGroupFlags::HasLights);
				}
				else
				{
					m_newGroupFlags &= ~static_cast<uint32>(WorldModelGroupFlags::HasLights);
				}
			}

			ImGui::Separator();

			if (ImGui::Button("Create", ImVec2(120, 0)))
			{
				if (strlen(m_newGroupName) > 0)
				{
					CreateGroup(m_newGroupName, m_newGroupFlags);
					m_newGroupName[0] = '\0';
					m_newGroupFlags = 0;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				m_newGroupName[0] = '\0';
				m_newGroupFlags = 0;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::PopID();
	}

	void WorldModelEditorInstance::OnMouseButtonDown(const uint32 button, const uint16 x, const uint16 y)
	{
		m_lastMouseX = x;
		m_lastMouseY = y;

		if (m_hovering)
		{
			const auto mousePos = ImGui::GetMousePos();
			m_transformWidget->OnMousePressed(button, (mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x, (mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
		}
	}

	void WorldModelEditorInstance::OnMouseButtonUp(const uint32 button, const uint16 x, const uint16 y)
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
			if (!widgetWasActive && button == 0 && m_hovering)
			{
				PerformEntitySelectionRaycast(
					(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
					(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
			}
		}
	}

	void WorldModelEditorInstance::OnMouseMoved(const uint16 x, const uint16 y)
	{
		if (!m_transformWidget->IsActive())
		{
			const float deltaTimeSeconds = ImGui::GetCurrentContext()->IO.DeltaTime;

			// Calculate mouse move delta
			const int16 deltaX = static_cast<int16>(x) - m_lastMouseX;
			const int16 deltaY = static_cast<int16>(y) - m_lastMouseY;

			if (m_rightButtonPressed || m_leftButtonPressed)
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

	bool WorldModelEditorInstance::Save()
	{
		if (!m_worldModel)
		{
			ELOG("No world model to save!");
			return false;
		}

		// Update bounding box before saving
		m_worldModel->RecalculateBoundingBox();

		// Open file for writing
		std::unique_ptr<std::ostream> streamPtr = AssetRegistry::CreateNewFile(GetAssetPath().string());
		if (!streamPtr)
		{
			ELOG("Failed to save file '" << GetAssetPath() << "': Unable to open file for writing!");
			return false;
		}

		io::StreamSink sink{ *streamPtr };
		io::Writer writer{ sink };

		// Use the new world model serializer
		WorldModelSerializer serializer;
		serializer.Serialize(*m_worldModel, writer);

		sink.Flush();

		ILOG("Successfully saved world model file " << GetAssetPath());
		return true;
	}

	void WorldModelEditorInstance::UpdateDebugAABB(const AABB& aabb)
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

	void WorldModelEditorInstance::PerformEntitySelectionRaycast(const float viewportX, const float viewportY)
	{
		const Ray ray = m_camera->GetCameraToViewportRay(viewportX, viewportY, 10000.0f);
		m_raySceneQuery->SetRay(ray);
		m_raySceneQuery->SetSortByDistance(true);
		m_raySceneQuery->SetQueryMask(1);
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

	void WorldModelEditorInstance::CreateMapEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale)
	{
		uint32 objectId = m_objectIdGenerator.GenerateId();

		const String uniqueId = "Entity_" + std::to_string(objectId);
		Entity* entity = m_scene.CreateEntity(uniqueId, assetName);
		if (entity)
		{
			entity->SetQueryFlags(1);

			auto& node = m_scene.CreateSceneNode(uniqueId);
			m_scene.GetRootSceneNode().AddChild(node);
			node.AttachObject(*entity);
			node.SetPosition(position);
			node.SetOrientation(orientation);
			node.SetScale(scale);

			const auto& mapEntity = m_mapEntities.emplace_back(std::make_unique<MapEntity>(m_scene, node, *entity, objectId));
			mapEntity->remove.connect(this, &WorldModelEditorInstance::OnMapEntityRemoved);
			entity->SetUserObject(m_mapEntities.back().get());
		}
	}

	void WorldModelEditorInstance::OnMapEntityRemoved(MapEntity& entity)
	{
		m_mapEntities.erase(std::remove_if(m_mapEntities.begin(), m_mapEntities.end(), [&entity](const auto& mapEntity)
		{
			return mapEntity.get() == &entity;
		}), m_mapEntities.end());
	}

	bool WorldModelEditorInstance::ReadMVERChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *versionChunk);

		// Read chunk only once
		RemoveChunkHandler(*versionChunk);

		uint32 version = 0;
		if (!(reader >> version))
		{
			ELOG("Failed to read version chunk!");
			return false;
		}

		if (version != 0x01)
		{
			ELOG("Detected unsuppoted file format version!");
			return false;
		}

		AddChunkHandler(*meshChunk, false, *this, &WorldModelEditorInstance::ReadMeshChunk);

		return reader;
	}

	bool WorldModelEditorInstance::ReadMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		ASSERT(chunkHeader == *meshChunk);

		// Only when we have read mesh names we support reading entity chunks otherwise entities would refer to meshes which we don't know about!
		AddChunkHandler(*entityChunk, false, *this, &WorldModelEditorInstance::ReadEntityChunk);

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

	bool WorldModelEditorInstance::ReadEntityChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
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

	bool WorldModelEditorInstance::OnReadFinished()
	{
		m_meshNames.clear();
		RemoveAllChunkHandlers();

		return ChunkReader::OnReadFinished();
	}

	WorldModelGroup* WorldModelEditorInstance::CreateGroup(const String& name, uint32 flags)
	{
		if (!m_worldModel)
		{
			return nullptr;
		}

		WorldModelGroup& group = m_worldModel->AddGroup();
		group.SetName(name);
		group.SetFlags(flags);

		// Set a default bounding box
		AABB defaultBBox(Vector3(-5.0f, 0.0f, -5.0f), Vector3(5.0f, 5.0f, 5.0f));
		group.SetBoundingBox(defaultBBox);

		UpdateGroupVisualizations();

		ILOG("Created new group: " << name);
		return &group;
	}

	void WorldModelEditorInstance::RemoveGroup(size_t groupIndex)
	{
		if (!m_worldModel || groupIndex >= m_worldModel->GetGroupCount())
		{
			return;
		}

		m_worldModel->RemoveGroup(groupIndex);
		m_selectedGroupIndex = -1;
		m_selection.Clear();

		UpdateGroupVisualizations();
		UpdatePortalVisualizations();
	}

	void WorldModelEditorInstance::CreatePortal(int32 groupA, int32 groupB, const std::vector<Vector3>& vertices)
	{
		if (!m_worldModel || vertices.size() < 3)
		{
			return;
		}

		Portal& portal = m_worldModel->AddPortal();
		
		// Calculate portal plane from vertices
		if (vertices.size() >= 3)
		{
			Vector3 v1 = vertices[1] - vertices[0];
			Vector3 v2 = vertices[2] - vertices[0];
			Vector3 normal = v1.Cross(v2);
			normal.Normalize();

			// Store the portal center
			Vector3 center = Vector3::Zero;
			for (const auto& v : vertices)
			{
				center += v;
			}
			center /= static_cast<float>(vertices.size());
		}

		const size_t portalIndex = m_worldModel->GetPortals().size() - 1;

		// Add portal references to both groups
		if (groupA >= 0 && groupA < static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			WorldModelGroup* group = m_worldModel->GetGroup(groupA);
			if (group)
			{
				auto& portalRefs = group->GetPortalRefs();
				WorldModelPortalRef ref;
				ref.portalIndex = static_cast<uint16>(portalIndex);
				ref.groupIndex = static_cast<uint16>(groupB);
				ref.side = 1;
				portalRefs.push_back(ref);
			}
		}

		if (groupB >= 0 && groupB < static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			WorldModelGroup* group = m_worldModel->GetGroup(groupB);
			if (group)
			{
				auto& portalRefs = group->GetPortalRefs();
				WorldModelPortalRef ref;
				ref.portalIndex = static_cast<uint16>(portalIndex);
				ref.groupIndex = static_cast<uint16>(groupA);
				ref.side = -1;
				portalRefs.push_back(ref);
			}
		}

		UpdatePortalVisualizations();
	}

	void WorldModelEditorInstance::RemovePortal(size_t portalIndex)
	{
		if (!m_worldModel || portalIndex >= m_worldModel->GetPortals().size())
		{
			return;
		}

		m_worldModel->RemovePortal(portalIndex);
		m_selectedPortalIndex = -1;

		// Note: Portal references in groups should also be updated
		// This is a simplified implementation

		UpdatePortalVisualizations();
	}

	void WorldModelEditorInstance::CreateLight(int32 groupIndex, const Vector3& position, uint32 color, float intensity)
	{
		if (!m_worldModel)
		{
			return;
		}

		WorldModelLight light;
		light.type = WorldModelLight::LightType::Omni;
		light.useAttenuation = true;
		light.color = color;
		light.position = position;
		light.intensity = intensity;
		light.attenuationStart = 1.0f;
		light.attenuationEnd = 10.0f;

		auto& lights = m_worldModel->GetLights();
		lights.push_back(light);

		UpdateLightVisualizations();
	}

	void WorldModelEditorInstance::RemoveLight(size_t lightIndex)
	{
		if (!m_worldModel)
		{
			return;
		}

		auto& lights = m_worldModel->GetLights();
		if (lightIndex < lights.size())
		{
			lights.erase(lights.begin() + lightIndex);
			m_selectedLightIndex = -1;
			UpdateLightVisualizations();
		}
	}

	uint32 WorldModelEditorInstance::CreateDoodadSet(const String& name)
	{
		if (!m_worldModel)
		{
			return 0;
		}

		WorldModelDoodadSet set;
		set.name = name;
		set.startIndex = static_cast<uint32>(m_worldModel->GetDoodads().size());
		set.count = 0;

		auto& sets = m_worldModel->GetDoodadSets();
		sets.push_back(set);

		return static_cast<uint32>(sets.size() - 1);
	}

	void WorldModelEditorInstance::AddDoodad(uint32 setIndex, const String& meshPath, const Vector3& position, const Quaternion& rotation, float scale)
	{
		if (!m_worldModel)
		{
			return;
		}

		auto& sets = m_worldModel->GetDoodadSets();
		if (setIndex >= sets.size())
		{
			return;
		}

		// Add doodad name if not already present
		auto& doodadNames = m_worldModel->GetDoodadNames();
		uint32 nameIndex = static_cast<uint32>(doodadNames.size());
		
		// Check if the name already exists
		for (uint32 i = 0; i < doodadNames.size(); ++i)
		{
			if (doodadNames[i] == meshPath)
			{
				nameIndex = i;
				break;
			}
		}

		if (nameIndex == doodadNames.size())
		{
			doodadNames.push_back(meshPath);
		}

		// Create the doodad
		WorldModelDoodad doodad;
		doodad.nameIndex = nameIndex;
		doodad.position = position;
		doodad.rotation = rotation;
		doodad.scale = scale;
		doodad.color = 0xFFFFFFFF;

		// Insert the doodad at the correct position for this set
		auto& doodads = m_worldModel->GetDoodads();
		uint32 insertIndex = sets[setIndex].startIndex + sets[setIndex].count;
		
		if (insertIndex >= doodads.size())
		{
			doodads.push_back(doodad);
		}
		else
		{
			doodads.insert(doodads.begin() + insertIndex, doodad);
			
			// Update start indices for subsequent sets
			for (uint32 i = setIndex + 1; i < sets.size(); ++i)
			{
				sets[i].startIndex++;
			}
		}

		sets[setIndex].count++;
	}

	void WorldModelEditorInstance::UpdateGroupVisualizations()
	{
		// Clear existing visualizations
		for (auto& vis : m_groupVisualizations)
		{
			if (vis.boundingBoxRenderable)
			{
				m_scene.DestroyManualRenderObject(*vis.boundingBoxRenderable);
			}
			if (vis.node)
			{
				m_scene.DestroySceneNode(*vis.node);
			}
		}
		m_groupVisualizations.clear();

		if (!m_worldModel)
		{
			return;
		}

		// Create visualizations for each group
		for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
		{
			const auto* group = m_worldModel->GetGroup(i);
			if (!group)
			{
				continue;
			}

			GroupVisualization vis;
			vis.node = &m_scene.CreateSceneNode("GroupNode_" + std::to_string(i));
			m_scene.GetRootSceneNode().AddChild(*vis.node);

			// Create bounding box visualization
			vis.boundingBoxRenderable = m_scene.CreateManualRenderObject("GroupBBox_" + std::to_string(i));
			vis.boundingBoxRenderable->SetQueryFlags(0);

			const AABB& bbox = group->GetBoundingBox();
			auto lineListOp = vis.boundingBoxRenderable->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/WorldGrid.hmat"));

			// Bottom face
			lineListOp->AddLine(Vector3(bbox.min.x, bbox.min.y, bbox.min.z), Vector3(bbox.max.x, bbox.min.y, bbox.min.z));
			lineListOp->AddLine(Vector3(bbox.max.x, bbox.min.y, bbox.min.z), Vector3(bbox.max.x, bbox.min.y, bbox.max.z));
			lineListOp->AddLine(Vector3(bbox.max.x, bbox.min.y, bbox.max.z), Vector3(bbox.min.x, bbox.min.y, bbox.max.z));
			lineListOp->AddLine(Vector3(bbox.min.x, bbox.min.y, bbox.max.z), Vector3(bbox.min.x, bbox.min.y, bbox.min.z));

			// Top face
			lineListOp->AddLine(Vector3(bbox.min.x, bbox.max.y, bbox.min.z), Vector3(bbox.max.x, bbox.max.y, bbox.min.z));
			lineListOp->AddLine(Vector3(bbox.max.x, bbox.max.y, bbox.min.z), Vector3(bbox.max.x, bbox.max.y, bbox.max.z));
			lineListOp->AddLine(Vector3(bbox.max.x, bbox.max.y, bbox.max.z), Vector3(bbox.min.x, bbox.max.y, bbox.max.z));
			lineListOp->AddLine(Vector3(bbox.min.x, bbox.max.y, bbox.max.z), Vector3(bbox.min.x, bbox.max.y, bbox.min.z));

			// Vertical edges
			lineListOp->AddLine(Vector3(bbox.min.x, bbox.min.y, bbox.min.z), Vector3(bbox.min.x, bbox.max.y, bbox.min.z));
			lineListOp->AddLine(Vector3(bbox.max.x, bbox.min.y, bbox.min.z), Vector3(bbox.max.x, bbox.max.y, bbox.min.z));
			lineListOp->AddLine(Vector3(bbox.max.x, bbox.min.y, bbox.max.z), Vector3(bbox.max.x, bbox.max.y, bbox.max.z));
			lineListOp->AddLine(Vector3(bbox.min.x, bbox.min.y, bbox.max.z), Vector3(bbox.min.x, bbox.max.y, bbox.max.z));

			vis.node->AttachObject(*vis.boundingBoxRenderable);
			vis.visible = true;

			m_groupVisualizations.push_back(std::move(vis));
		}
	}

	void WorldModelEditorInstance::UpdatePortalVisualizations()
	{
		// Clear existing visualizations
		for (auto& vis : m_portalVisualizations)
		{
			if (vis.renderable)
			{
				m_scene.DestroyManualRenderObject(*vis.renderable);
			}
			if (vis.node)
			{
				m_scene.DestroySceneNode(*vis.node);
			}
		}
		m_portalVisualizations.clear();

		if (!m_worldModel)
		{
			return;
		}

		// Portal visualizations would require portal vertex data
		// For now, this is a placeholder for when we have proper portal geometry
	}

	void WorldModelEditorInstance::UpdateLightVisualizations()
	{
		// Clear existing visualizations
		for (auto& vis : m_lightVisualizations)
		{
			if (vis.light)
			{
				m_scene.DestroyLight(*vis.light);
			}
			if (vis.iconRenderable)
			{
				m_scene.DestroyManualRenderObject(*vis.iconRenderable);
			}
			if (vis.node)
			{
				m_scene.DestroySceneNode(*vis.node);
			}
		}
		m_lightVisualizations.clear();

		if (!m_worldModel)
		{
			return;
		}

		const auto& lights = m_worldModel->GetLights();
		for (size_t i = 0; i < lights.size(); ++i)
		{
			const auto& lightData = lights[i];

			LightVisualization vis;
			vis.node = &m_scene.CreateSceneNode("LightNode_" + std::to_string(i));
			vis.node->SetPosition(lightData.position);
			m_scene.GetRootSceneNode().AddChild(*vis.node);

			// Create a light
			vis.light = &m_scene.CreateLight("Light_" + std::to_string(i), LightType::Point);
			
			// Convert color from uint32 to Color
			Vector4 lightColor(
				((lightData.color >> 16) & 0xFF) / 255.0f,
				((lightData.color >> 8) & 0xFF) / 255.0f,
				(lightData.color & 0xFF) / 255.0f,
				1.0f
			);
			vis.light->SetColor(lightColor);
			vis.light->SetIntensity(lightData.intensity);
			vis.light->SetRange(lightData.attenuationEnd);

			vis.node->AttachObject(*vis.light);

			m_lightVisualizations.push_back(std::move(vis));
		}
	}

	void WorldModelEditorInstance::DrawGroupsPanel()
	{
		if (ImGui::Button("New Group"))
		{
			m_showNewGroupDialog = true;
		}

		ImGui::SameLine();
		
		if (ImGui::Button("Delete") && m_selectedGroupIndex >= 0)
		{
			RemoveGroup(m_selectedGroupIndex);
		}

		ImGui::Separator();

		if (!m_worldModel)
		{
			ImGui::Text("No world model loaded");
			return;
		}

		for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
		{
			const auto* group = m_worldModel->GetGroup(i);
			if (!group)
			{
				continue;
			}

			ImGui::PushID(static_cast<int>(i));

			bool isSelected = m_selectedGroupIndex == static_cast<int32>(i);
			String label = group->GetName();
			
			if (group->IsInterior())
			{
				label += " [Interior]";
			}
			else if (group->IsExterior())
			{
				label += " [Exterior]";
			}

			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				m_selectedGroupIndex = static_cast<int32>(i);
			}

			// Visibility toggle
			if (i < m_groupVisualizations.size())
			{
				ImGui::SameLine();
				bool visible = m_groupVisualizations[i].visible;
				if (ImGui::Checkbox("##vis", &visible))
				{
					m_groupVisualizations[i].visible = visible;
					if (m_groupVisualizations[i].boundingBoxRenderable)
					{
						m_groupVisualizations[i].boundingBoxRenderable->SetVisible(visible);
					}
				}
			}

			ImGui::PopID();
		}
	}

	void WorldModelEditorInstance::DrawPortalsPanel()
	{
		if (ImGui::Button("Create Portal"))
		{
			if (m_selectedGroupIndex >= 0)
			{
				m_creatingPortal = true;
				m_portalSourceGroup = m_selectedGroupIndex;
				m_portalVertices.clear();
			}
		}

		if (m_creatingPortal)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "Creating portal from group %d...", m_portalSourceGroup);
			
			if (ImGui::Button("Cancel"))
			{
				m_creatingPortal = false;
				m_portalSourceGroup = -1;
				m_portalVertices.clear();
			}
		}

		ImGui::Separator();

		if (!m_worldModel)
		{
			ImGui::Text("No world model loaded");
			return;
		}

		const auto& portals = m_worldModel->GetPortals();
		for (size_t i = 0; i < portals.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));

			bool isSelected = m_selectedPortalIndex == static_cast<int32>(i);
			String label = "Portal " + std::to_string(i);

			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				m_selectedPortalIndex = static_cast<int32>(i);
			}

			ImGui::PopID();
		}
	}

	void WorldModelEditorInstance::DrawDoodadsPanel()
	{
		if (ImGui::Button("New Doodad Set"))
		{
			CreateDoodadSet("DoodadSet_" + std::to_string(m_worldModel ? m_worldModel->GetDoodadSets().size() : 0));
		}

		ImGui::Separator();

		if (!m_worldModel)
		{
			ImGui::Text("No world model loaded");
			return;
		}

		const auto& sets = m_worldModel->GetDoodadSets();
		for (size_t i = 0; i < sets.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));

			bool isSelected = m_selectedDoodadSetIndex == static_cast<int32>(i);
			String label = sets[i].name + " (" + std::to_string(sets[i].count) + " doodads)";

			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				m_selectedDoodadSetIndex = static_cast<int32>(i);
			}

			ImGui::PopID();
		}

		ImGui::Separator();

		if (m_selectedDoodadSetIndex >= 0 && m_selectedDoodadSetIndex < static_cast<int32>(sets.size()))
		{
			ImGui::Text("Doodads in set '%s':", sets[m_selectedDoodadSetIndex].name.c_str());

			const auto& doodads = m_worldModel->GetDoodads();
			const auto& doodadNames = m_worldModel->GetDoodadNames();
			const auto& set = sets[m_selectedDoodadSetIndex];

			for (uint32 i = 0; i < set.count; ++i)
			{
				uint32 doodadIndex = set.startIndex + i;
				if (doodadIndex >= doodads.size())
				{
					break;
				}

				const auto& doodad = doodads[doodadIndex];
				String name = doodad.nameIndex < doodadNames.size() ? doodadNames[doodad.nameIndex] : "Unknown";
				
				ImGui::BulletText("%s", name.c_str());
			}
		}
	}

	void WorldModelEditorInstance::DrawLightsPanel()
	{
		if (ImGui::Button("Add Light"))
		{
			CreateLight(m_selectedGroupIndex, Vector3::Zero, 0xFFFFFFFF, 1.0f);
		}

		ImGui::SameLine();

		if (ImGui::Button("Delete") && m_selectedLightIndex >= 0)
		{
			RemoveLight(m_selectedLightIndex);
		}

		ImGui::Separator();

		if (!m_worldModel)
		{
			ImGui::Text("No world model loaded");
			return;
		}

		const auto& lights = m_worldModel->GetLights();
		for (size_t i = 0; i < lights.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));

			bool isSelected = m_selectedLightIndex == static_cast<int32>(i);
			String label = "Light " + std::to_string(i);

			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				m_selectedLightIndex = static_cast<int32>(i);
			}

			ImGui::PopID();
		}
	}

	void WorldModelEditorInstance::DrawFogPanel()
	{
		if (!m_worldModel)
		{
			ImGui::Text("No world model loaded");
			return;
		}

		const auto& fogs = m_worldModel->GetFogs();
		
		ImGui::Text("Fog Volumes: %zu", fogs.size());

		// TODO: Implement fog editing UI
	}

	void WorldModelEditorInstance::DrawPropertiesPanel()
	{
		if (ImGui::Button("Save"))
		{
			Save();
		}

		ImGui::Separator();

		// Show selected group properties
		if (m_selectedGroupIndex >= 0 && m_worldModel && m_selectedGroupIndex < static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			auto* group = m_worldModel->GetGroup(m_selectedGroupIndex);
			if (group)
			{
				ImGui::Text("Selected Group: %s", group->GetName().c_str());
				ImGui::Separator();

				// Group flags
				uint32 flags = group->GetFlags();
				
				bool isInterior = (flags & static_cast<uint32>(WorldModelGroupFlags::Interior)) != 0;
				if (ImGui::Checkbox("Interior", &isInterior))
				{
					if (isInterior)
					{
						flags |= static_cast<uint32>(WorldModelGroupFlags::Interior);
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::Exterior);
					}
					else
					{
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::Interior);
					}
					group->SetFlags(flags);
				}

				bool isExterior = (flags & static_cast<uint32>(WorldModelGroupFlags::Exterior)) != 0;
				if (ImGui::Checkbox("Exterior", &isExterior))
				{
					if (isExterior)
					{
						flags |= static_cast<uint32>(WorldModelGroupFlags::Exterior);
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::Interior);
					}
					else
					{
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::Exterior);
					}
					group->SetFlags(flags);
				}

				bool showSkybox = (flags & static_cast<uint32>(WorldModelGroupFlags::ShowSkybox)) != 0;
				if (ImGui::Checkbox("Show Skybox", &showSkybox))
				{
					if (showSkybox)
					{
						flags |= static_cast<uint32>(WorldModelGroupFlags::ShowSkybox);
					}
					else
					{
						flags &= ~static_cast<uint32>(WorldModelGroupFlags::ShowSkybox);
					}
					group->SetFlags(flags);
				}

				ImGui::Separator();

				// Bounding box
				AABB bbox = group->GetBoundingBox();
				bool bboxChanged = false;

				if (ImGui::InputFloat3("Min", bbox.min.Ptr()))
				{
					bboxChanged = true;
				}
				if (ImGui::InputFloat3("Max", bbox.max.Ptr()))
				{
					bboxChanged = true;
				}

				if (bboxChanged)
				{
					group->SetBoundingBox(bbox);
					UpdateGroupVisualizations();
				}

				// Ambient color
				uint32 ambientColor = group->GetAmbientColor();
				float color[4] = {
					((ambientColor >> 16) & 0xFF) / 255.0f,
					((ambientColor >> 8) & 0xFF) / 255.0f,
					(ambientColor & 0xFF) / 255.0f,
					((ambientColor >> 24) & 0xFF) / 255.0f
				};
				if (ImGui::ColorEdit4("Ambient Color", color))
				{
					uint32 newColor =
						(static_cast<uint32>(color[3] * 255.0f) << 24) |
						(static_cast<uint32>(color[0] * 255.0f) << 16) |
						(static_cast<uint32>(color[1] * 255.0f) << 8) |
						static_cast<uint32>(color[2] * 255.0f);
					group->SetAmbientColor(newColor);
				}
			}
		}
		else if (m_selectedLightIndex >= 0 && m_worldModel)
		{
			auto& lights = m_worldModel->GetLights();
			if (m_selectedLightIndex < static_cast<int32>(lights.size()))
			{
				auto& light = lights[m_selectedLightIndex];
				
				ImGui::Text("Selected Light: %d", m_selectedLightIndex);
				ImGui::Separator();

				ImGui::InputFloat3("Position", light.position.Ptr());
				ImGui::InputFloat("Intensity", &light.intensity);
				ImGui::InputFloat("Attenuation Start", &light.attenuationStart);
				ImGui::InputFloat("Attenuation End", &light.attenuationEnd);

				float color[4] = {
					((light.color >> 16) & 0xFF) / 255.0f,
					((light.color >> 8) & 0xFF) / 255.0f,
					(light.color & 0xFF) / 255.0f,
					((light.color >> 24) & 0xFF) / 255.0f
				};
				if (ImGui::ColorEdit4("Color", color))
				{
					light.color =
						(static_cast<uint32>(color[3] * 255.0f) << 24) |
						(static_cast<uint32>(color[0] * 255.0f) << 16) |
						(static_cast<uint32>(color[1] * 255.0f) << 8) |
						static_cast<uint32>(color[2] * 255.0f);
				}

				// Update visualization
				if (m_selectedLightIndex < static_cast<int32>(m_lightVisualizations.size()))
				{
					auto& vis = m_lightVisualizations[m_selectedLightIndex];
					if (vis.node)
					{
						vis.node->SetPosition(light.position);
					}
					if (vis.light)
					{
						Vector4 lightColor(
							((light.color >> 16) & 0xFF) / 255.0f,
							((light.color >> 8) & 0xFF) / 255.0f,
							(light.color & 0xFF) / 255.0f,
							1.0f
						);
						vis.light->SetColor(lightColor);
						vis.light->SetIntensity(light.intensity);
						vis.light->SetRange(light.attenuationEnd);
					}
				}
			}
		}
		else if (!m_selection.IsEmpty())
		{
			Selectable* selected = m_selection.GetSelectedObjects().back().get();

			if (selected->SupportsTranslate() || selected->SupportsRotate() || selected->SupportsScale())
			{
				if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (selected->SupportsTranslate())
					{
						Vector3 position = selected->GetPosition();
						if (ImGui::InputFloat3("Position", position.Ptr()))
						{
							selected->SetPosition(position);
						}
					}

					if (selected->SupportsRotate())
					{
						Rotator rotation = selected->GetOrientation().ToRotator();
						float angles[3] = { rotation.roll.GetValueDegrees(), rotation.yaw.GetValueDegrees(), rotation.pitch.GetValueDegrees() };
						if (ImGui::InputFloat3("Rotation", angles, "%.3f"))
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
		else
		{
			ImGui::Text("No selection");
		}
	}

	void WorldModelEditorInstance::DrawToolbar()
	{
		// Edit mode selection
		if (ImGui::RadioButton("Select", m_editMode == WorldModelEditMode::Select))
		{
			m_editMode = WorldModelEditMode::Select;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Portal", m_editMode == WorldModelEditMode::Portal))
		{
			m_editMode = WorldModelEditMode::Portal;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Doodad", m_editMode == WorldModelEditMode::Doodad))
		{
			m_editMode = WorldModelEditMode::Doodad;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Light", m_editMode == WorldModelEditMode::Light))
		{
			m_editMode = WorldModelEditMode::Light;
		}
	}
}
