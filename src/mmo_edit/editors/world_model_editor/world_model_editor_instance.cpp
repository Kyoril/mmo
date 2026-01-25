// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_editor_instance.h"

#include <algorithm>
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
#include "scene_graph/sub_mesh.h"
#include "graphics/graphics_device.h"
#include "graphics/vertex_declaration.h"
#include "graphics/vertex_index_data.h"
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

	// SelectedGroupMesh implementation
	SelectedGroupMesh::SelectedGroupMesh(WorldModelGroup& group, size_t meshIndex, SceneNode& node)
		: m_group(group)
		, m_meshIndex(meshIndex)
		, m_node(node)
	{
	}

	Vector3 SelectedGroupMesh::GetPosition() const
	{
		if (const auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			return meshRef->position;
		}
		return Vector3::Zero;
	}

	void SelectedGroupMesh::SetPosition(const Vector3& position) const
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->position = position;
		}
		m_node.SetPosition(position);
	}

	Quaternion SelectedGroupMesh::GetOrientation() const
	{
		if (const auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			return meshRef->rotation;
		}
		return Quaternion::Identity;
	}

	void SelectedGroupMesh::SetOrientation(const Quaternion& orientation) const
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->rotation = orientation;
		}
		m_node.SetOrientation(orientation);
	}

	Vector3 SelectedGroupMesh::GetScale() const
	{
		if (const auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			return meshRef->scale;
		}
		return Vector3::UnitScale;
	}

	void SelectedGroupMesh::SetScale(const Vector3& scale) const
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->scale = scale;
		}
		m_node.SetScale(scale);
	}

	void SelectedGroupMesh::Translate(const Vector3& delta)
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->position += delta;
			m_node.Translate(delta);
		}
	}

	void SelectedGroupMesh::Rotate(const Quaternion& delta)
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->rotation = delta * meshRef->rotation;
			m_node.Rotate(delta);
		}
	}

	void SelectedGroupMesh::Scale(const Vector3& delta)
	{
		if (auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			meshRef->scale += delta;
			m_node.SetScale(meshRef->scale);
		}
	}

	void SelectedGroupMesh::Duplicate()
	{
		if (const auto* meshRef = m_group.GetMeshRef(m_meshIndex))
		{
			WorldModelMeshRef newRef = *meshRef;
			newRef.name = meshRef->name + "_copy";
			newRef.position += Vector3(1.0f, 0.0f, 0.0f);  // Offset the copy
			m_group.AddMeshRef(newRef);
		}
	}

	void SelectedGroupMesh::Remove()
	{
		removed();
	}

	WorldModelMeshRef* SelectedGroupMesh::GetMeshRef()
	{
		return m_group.GetMeshRef(m_meshIndex);
	}

	// SelectedChildWMO implementation
	SelectedChildWMO::SelectedChildWMO(WorldModel& worldModel, size_t childIndex, SceneNode& node)
		: m_worldModel(worldModel)
		, m_childIndex(childIndex)
		, m_node(node)
	{
	}

	Vector3 SelectedChildWMO::GetPosition() const
	{
		if (const auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			return childRef->position;
		}
		return Vector3::Zero;
	}

	void SelectedChildWMO::SetPosition(const Vector3& position) const
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->position = position;
		}
		m_node.SetPosition(position);
	}

	Quaternion SelectedChildWMO::GetOrientation() const
	{
		if (const auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			return childRef->rotation;
		}
		return Quaternion::Identity;
	}

	void SelectedChildWMO::SetOrientation(const Quaternion& orientation) const
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->rotation = orientation;
		}
		m_node.SetOrientation(orientation);
	}

	Vector3 SelectedChildWMO::GetScale() const
	{
		if (const auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			return childRef->scale;
		}
		return Vector3::UnitScale;
	}

	void SelectedChildWMO::SetScale(const Vector3& scale) const
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->scale = scale;
		}
		m_node.SetScale(scale);
	}

	void SelectedChildWMO::Translate(const Vector3& delta)
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->position += delta;
			m_node.Translate(delta);
		}
	}

	void SelectedChildWMO::Rotate(const Quaternion& delta)
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->rotation = delta * childRef->rotation;
			m_node.Rotate(delta);
		}
	}

	void SelectedChildWMO::Scale(const Vector3& delta)
	{
		if (auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			childRef->scale += delta;
			m_node.SetScale(childRef->scale);
		}
	}

	void SelectedChildWMO::Duplicate()
	{
		if (const auto* childRef = m_worldModel.GetChildRef(m_childIndex))
		{
			WorldModelChildRef newRef = *childRef;
			newRef.name = childRef->name + "_copy";
			newRef.position += Vector3(5.0f, 0.0f, 0.0f);  // Offset the copy
			m_worldModel.AddChildRef(newRef);
		}
	}

	void SelectedChildWMO::Remove()
	{
		removed();
	}

	WorldModelChildRef* SelectedChildWMO::GetChildRef()
	{
		return m_worldModel.GetChildRef(m_childIndex);
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

		// Create a directional light for proper geometry lighting
		m_lightNode = &m_scene.CreateSceneNode("MainLightNode");
		m_scene.GetRootSceneNode().AddChild(*m_lightNode);
		m_mainLight = &m_scene.CreateLight("MainLight", LightType::Directional);
		m_mainLight->SetDirection(Vector3(-0.5f, -1.0f, -0.3f).NormalizedCopy());
		m_mainLight->SetIntensity(1.0f);
		m_mainLight->SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		m_lightNode->AttachObject(*m_mainLight);

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
				UpdateChildWMOVisualizations();

				// Update mesh ref visualizations for all groups
				for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
				{
					UpdateMeshRefVisualizations(i);
				}
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
		const String meshRefsId = "Meshes##" + GetAssetPath().string();
		const String childWMOsId = "Child WMOs##" + GetAssetPath().string();
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

		// Mesh references panel (for selected group)
		if (ImGui::Begin(meshRefsId.c_str()))
		{
			DrawMeshRefsPanel(m_selectedGroupIndex);
		}
		ImGui::End();

		// Child WMOs panel
		if (ImGui::Begin(childWMOsId.c_str()))
		{
			DrawChildWMOsPanel();
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
				ImGui::Text("Child WMOs: %zu", m_worldModel->GetChildRefs().size());
				
				// Count total mesh refs
				size_t totalMeshRefs = 0;
				for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
				{
					if (const auto* group = m_worldModel->GetGroup(i))
					{
						totalMeshRefs += group->GetMeshRefs().size();
					}
				}
				ImGui::Text("Mesh References: %zu", totalMeshRefs);

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
			ImGui::DockBuilderDockWindow(meshRefsId.c_str(), leftId);
			ImGui::DockBuilderDockWindow(childWMOsId.c_str(), leftId);
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

		// Update all visualizations after loading
		UpdateGroupVisualizations();
		UpdatePortalVisualizations();
		UpdateLightVisualizations();
		UpdateChildWMOVisualizations();

		// Update mesh ref visualizations for all groups
		if (m_worldModel)
		{
			for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
			{
				UpdateMeshRefVisualizations(i);
			}
		}

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

	void WorldModelEditorInstance::AssignMeshToGroup(int32 groupIndex, const String& meshPath)
	{
		if (!m_worldModel || groupIndex < 0 || groupIndex >= static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			ELOG("Invalid group index for mesh assignment");
			return;
		}

		auto* group = m_worldModel->GetGroup(groupIndex);
		if (!group)
		{
			ELOG("Failed to get group at index " << groupIndex);
			return;
		}

		// Load the mesh
		auto mesh = MeshManager::Get().Load(meshPath);
		if (!mesh)
		{
			ELOG("Failed to load mesh: " << meshPath);
			return;
		}

		// Clear existing geometry
		group->GetVertices().clear();
		group->GetNormals().clear();
		group->GetTexCoords().clear();
		group->GetVertexColors().clear();
		group->GetIndices().clear();
		group->GetMaterialIndices().clear();

		AABB boundingBox;
		boundingBox.SetNull();

		uint32 baseVertexIndex = 0;

		// Iterate through all submeshes and extract geometry
		for (uint16 subMeshIdx = 0; subMeshIdx < mesh->GetSubMeshCount(); ++subMeshIdx)
		{
			SubMesh& subMesh = mesh->GetSubMesh(subMeshIdx);
			
			VertexData* vertexData = nullptr;
			if (subMesh.useSharedVertices && mesh->sharedVertexData)
			{
				vertexData = mesh->sharedVertexData.get();
			}
			else if (subMesh.vertexData)
			{
				vertexData = subMesh.vertexData.get();
			}

			if (!vertexData || !vertexData->vertexDeclaration || !vertexData->vertexBufferBinding)
			{
				continue;
			}

			// Find position element
			const VertexElement* posElem = vertexData->vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Position);
			const VertexElement* normElem = vertexData->vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Normal);
			const VertexElement* texElem = vertexData->vertexDeclaration->FindElementBySemantic(VertexElementSemantic::TextureCoordinate);
			const VertexElement* colorElem = vertexData->vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Diffuse);

			if (!posElem)
			{
				continue;
			}

			// Get vertex buffer
			auto vbuf = vertexData->vertexBufferBinding->GetBuffer(posElem->GetSource());
			if (!vbuf)
			{
				continue;
			}

			// Lock vertex buffer for reading
			void* rawVertexPtr = vbuf->Map(LockOptions::ReadOnly);
			if (!rawVertexPtr)
			{
				continue;
			}

			uint8* vertexPtr = static_cast<uint8*>(rawVertexPtr);
			uint32 vertexStart = vertexData->vertexStart;
			uint32 vertexCount = vertexData->vertexCount;
			size_t vertexSize = vbuf->GetVertexSize();

			// Read vertices
			for (uint32 v = 0; v < vertexCount; ++v)
			{
				uint8* basePtr = vertexPtr + (vertexStart + v) * vertexSize;

				// Position
				float* posData = nullptr;
				posElem->BaseVertexPointerToElement(basePtr, &posData);
				Vector3 position(posData[0], posData[1], posData[2]);
				group->GetVertices().push_back(position);
				boundingBox.Combine(position);

				// Normal
				if (normElem)
				{
					float* normData = nullptr;
					normElem->BaseVertexPointerToElement(basePtr, &normData);
					group->GetNormals().push_back(Vector3(normData[0], normData[1], normData[2]));
				}
				else
				{
					group->GetNormals().push_back(Vector3::UnitY);
				}

				// Texture coordinates
				if (texElem)
				{
					float* texData = nullptr;
					texElem->BaseVertexPointerToElement(basePtr, &texData);
					group->GetTexCoords().push_back(Vector3(texData[0], texData[1], 0.0f));
				}
				else
				{
					group->GetTexCoords().push_back(Vector3::Zero);
				}

				// Vertex color
				if (colorElem)
				{
					uint32* colorData = nullptr;
					colorElem->BaseVertexPointerToElement(basePtr, &colorData);
					group->GetVertexColors().push_back(*colorData);
				}
				else
				{
					group->GetVertexColors().push_back(0xFFFFFFFF);
				}
			}

			vbuf->Unmap();

			// Read indices
			if (subMesh.indexData && subMesh.indexData->indexBuffer)
			{
				auto ibuf = subMesh.indexData->indexBuffer;
				void* indexPtr = ibuf->Map(LockOptions::ReadOnly);
				
				if (indexPtr)
				{
					size_t indexStart = subMesh.indexData->indexStart;
					size_t indexCount = subMesh.indexData->indexCount;
					bool use32Bit = ibuf->GetIndexSize() == IndexBufferSize::Index_32;

					for (size_t i = 0; i < indexCount; ++i)
					{
						uint32 index;
						if (use32Bit)
						{
							index = static_cast<uint32*>(indexPtr)[indexStart + i];
						}
						else
						{
							index = static_cast<uint16*>(indexPtr)[indexStart + i];
						}
						group->GetIndices().push_back(baseVertexIndex + index);
						
						// Set material index for each vertex (every 3 indices = 1 triangle)
						if ((i % 3) == 0)
						{
							group->GetMaterialIndices().push_back(subMeshIdx);
						}
					}

					ibuf->Unmap();
				}
			}

			baseVertexIndex = static_cast<uint32>(group->GetVertices().size());
		}

		// Update group bounding box
		if (!boundingBox.IsNull())
		{
			group->SetBoundingBox(boundingBox);
		}

		ILOG("Assigned mesh '" << meshPath << "' to group '" << group->GetName() << "' (" 
			<< group->GetVertices().size() << " vertices, " 
			<< group->GetIndices().size() / 3 << " triangles)");

		UpdateGroupVisualizations();
	}

	void WorldModelEditorInstance::AddMeshRefToGroup(int32 groupIndex, const String& meshPath,
		const Vector3& position, const Quaternion& rotation, const Vector3& scale, const String& name)
	{
		if (!m_worldModel || groupIndex < 0 || groupIndex >= static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			ELOG("Invalid group index for adding mesh reference");
			return;
		}

		auto* group = m_worldModel->GetGroup(groupIndex);
		if (!group)
		{
			ELOG("Failed to get group at index " << groupIndex);
			return;
		}

		// Verify the mesh can be loaded
		auto mesh = MeshManager::Get().Load(meshPath);
		if (!mesh)
		{
			ELOG("Failed to load mesh: " << meshPath);
			return;
		}

		// Create the mesh reference
		WorldModelMeshRef meshRef;
		meshRef.meshPath = meshPath;
		meshRef.position = position;
		meshRef.rotation = rotation;
		meshRef.scale = scale;
		meshRef.visible = true;
		
		if (name.empty())
		{
			// Generate a name from the mesh path
			std::filesystem::path path(meshPath);
			meshRef.name = path.stem().string();
		}
		else
		{
			meshRef.name = name;
		}

		size_t refIndex = group->AddMeshRef(meshRef);
		ILOG("Added mesh reference '" << meshRef.name << "' to group '" << group->GetName() << "' at index " << refIndex);

		// Update bounding box to include the new mesh
		AABB meshAABB = mesh->GetBounds();
		Matrix4 transform;
		transform.MakeTransform(position, scale, rotation);
		meshAABB.Transform(transform);
		
		AABB groupAABB = group->GetBoundingBox();
		if (groupAABB.IsNull())
		{
			group->SetBoundingBox(meshAABB);
		}
		else
		{
			groupAABB.Combine(meshAABB);
			group->SetBoundingBox(groupAABB);
		}

		UpdateMeshRefVisualizations(groupIndex);
	}

	void WorldModelEditorInstance::RemoveMeshRefFromGroup(int32 groupIndex, size_t meshRefIndex)
	{
		if (!m_worldModel || groupIndex < 0 || groupIndex >= static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			ELOG("Invalid group index for removing mesh reference");
			return;
		}

		auto* group = m_worldModel->GetGroup(groupIndex);
		if (!group)
		{
			ELOG("Failed to get group at index " << groupIndex);
			return;
		}

		if (meshRefIndex >= group->GetMeshRefs().size())
		{
			ELOG("Invalid mesh reference index " << meshRefIndex);
			return;
		}

		ILOG("Removing mesh reference at index " << meshRefIndex << " from group '" << group->GetName() << "'");
		group->RemoveMeshRef(meshRefIndex);

		// Update selection if needed
		if (m_selectedMeshRefIndex == static_cast<int32>(meshRefIndex))
		{
			m_selection.Clear();
			m_selectedMeshRefIndex = -1;
		}
		else if (m_selectedMeshRefIndex > static_cast<int32>(meshRefIndex))
		{
			m_selectedMeshRefIndex--;
		}

		UpdateMeshRefVisualizations(groupIndex);
	}

	void WorldModelEditorInstance::AddChildWMO(const String& wmoPath,
		const Vector3& position, const Quaternion& rotation, const Vector3& scale, const String& name)
	{
		if (!m_worldModel)
		{
			ELOG("No world model to add child WMO to");
			return;
		}

		// Create the child reference
		WorldModelChildRef childRef;
		childRef.wmoPath = wmoPath;
		childRef.position = position;
		childRef.rotation = rotation;
		childRef.scale = scale;
		childRef.visible = true;

		if (name.empty())
		{
			// Generate a name from the WMO path
			std::filesystem::path path(wmoPath);
			childRef.name = path.stem().string();
		}
		else
		{
			childRef.name = name;
		}

		size_t refIndex = m_worldModel->AddChildRef(childRef);
		ILOG("Added child WMO reference '" << childRef.name << "' at index " << refIndex);

		UpdateChildWMOVisualizations();
	}

	void WorldModelEditorInstance::RemoveChildWMO(size_t childIndex)
	{
		if (!m_worldModel)
		{
			ELOG("No world model");
			return;
		}

		if (childIndex >= m_worldModel->GetChildRefs().size())
		{
			ELOG("Invalid child WMO index " << childIndex);
			return;
		}

		ILOG("Removing child WMO at index " << childIndex);
		m_worldModel->RemoveChildRef(childIndex);

		// Update selection if needed
		if (m_selectedChildWMOIndex == static_cast<int32>(childIndex))
		{
			m_selection.Clear();
			m_selectedChildWMOIndex = -1;
		}
		else if (m_selectedChildWMOIndex > static_cast<int32>(childIndex))
		{
			m_selectedChildWMOIndex--;
		}

		UpdateChildWMOVisualizations();
	}

	void WorldModelEditorInstance::UpdateMeshRefVisualizations(size_t groupIndex)
	{
		if (!m_worldModel || groupIndex >= m_groupVisualizations.size())
		{
			return;
		}

		auto& groupViz = m_groupVisualizations[groupIndex];
		auto* group = m_worldModel->GetGroup(groupIndex);
		if (!group)
		{
			return;
		}

		// Clear existing mesh ref visualizations
		for (auto& meshRefViz : groupViz.meshRefVisualizations)
		{
			if (meshRefViz.entity)
			{
				if (meshRefViz.node)
				{
					meshRefViz.node->DetachObject(*meshRefViz.entity);
				}
				m_scene.DestroyEntity(*meshRefViz.entity);
				meshRefViz.entity = nullptr;
			}
			if (meshRefViz.node)
			{
				m_scene.DestroySceneNode(*meshRefViz.node);
				meshRefViz.node = nullptr;
			}
		}
		groupViz.meshRefVisualizations.clear();

		// Create visualizations for each mesh reference
		const auto& meshRefs = group->GetMeshRefs();
		for (size_t i = 0; i < meshRefs.size(); ++i)
		{
			const auto& meshRef = meshRefs[i];
			
			MeshRefVisualization viz;
			viz.meshRefIndex = i;
			viz.visible = meshRef.visible;

			// Create scene node under the group node
			String nodeName = "MeshRef_" + std::to_string(groupIndex) + "_" + std::to_string(i);
			viz.node = &m_scene.CreateSceneNode(nodeName);
			viz.node->SetPosition(meshRef.position);
			viz.node->SetOrientation(meshRef.rotation);
			viz.node->SetScale(meshRef.scale);
			
			if (groupViz.node)
			{
				groupViz.node->AddChild(*viz.node);
			}
			else
			{
				m_scene.GetRootSceneNode().AddChild(*viz.node);
			}

			// Load and attach the mesh
			viz.mesh = MeshManager::Get().Load(meshRef.meshPath);
			if (viz.mesh)
			{
				String entityName = "MeshRefEntity_" + std::to_string(groupIndex) + "_" + std::to_string(i) + "_" + std::to_string(m_meshCounter++);
				viz.entity = m_scene.CreateEntity(entityName, viz.mesh);
				viz.entity->SetQueryFlags(1);
				viz.node->AttachObject(*viz.entity);
				
				// Apply material override if specified
				if (!meshRef.materialOverride.empty())
				{
					auto material = MaterialManager::Get().Load(meshRef.materialOverride);
					if (material)
					{
						viz.entity->SetMaterial(material);
					}
				}
			}

			groupViz.meshRefVisualizations.push_back(std::move(viz));
		}
	}

	void WorldModelEditorInstance::UpdateChildWMOVisualizations()
	{
		// Clear existing child WMO visualizations
		for (auto& childViz : m_childWMOVisualizations)
		{
			for (auto* entity : childViz.entities)
			{
				if (childViz.node)
				{
					childViz.node->DetachObject(*entity);
				}
				m_scene.DestroyEntity(*entity);
			}
			childViz.entities.clear();
			
			if (childViz.node)
			{
				m_scene.DestroySceneNode(*childViz.node);
				childViz.node = nullptr;
			}
		}
		m_childWMOVisualizations.clear();

		if (!m_worldModel)
		{
			return;
		}

		// Create visualizations for each child WMO reference
		const auto& childRefs = m_worldModel->GetChildRefs();
		for (size_t i = 0; i < childRefs.size(); ++i)
		{
			const auto& childRef = childRefs[i];
			
			ChildWMOVisualization viz;
			viz.childRefIndex = i;
			viz.visible = childRef.visible;

			// Create scene node
			String nodeName = "ChildWMO_" + std::to_string(i);
			viz.node = &m_scene.CreateSceneNode(nodeName);
			viz.node->SetPosition(childRef.position);
			viz.node->SetOrientation(childRef.rotation);
			viz.node->SetScale(childRef.scale);
			m_scene.GetRootSceneNode().AddChild(*viz.node);

			// TODO: Load and display the child WMO
			// For now, we'll just show a placeholder bounding box or similar
			// Full implementation would recursively load the child WMO and display its geometry

			m_childWMOVisualizations.push_back(std::move(viz));
		}
	}

	void WorldModelEditorInstance::CreatePortal(int32 groupA, int32 groupB, const std::vector<Vector3>& vertices)
	{
		if (!m_worldModel || vertices.size() < 3)
		{
			return;
		}

		Portal& portal = m_worldModel->AddPortal();
		
		// Calculate portal plane and center from vertices
		if (vertices.size() >= 3)
		{
			Vector3 v1 = vertices[1] - vertices[0];
			Vector3 v2 = vertices[2] - vertices[0];
			Vector3 normal = v1.Cross(v2);
			normal.Normalize();

			// Calculate the portal center
			Vector3 center = Vector3::Zero;
			for (const auto& v : vertices)
			{
				center += v;
			}
			center /= static_cast<float>(vertices.size());

			// Calculate portal dimensions from vertices
			float width = (vertices[1] - vertices[0]).GetLength();
			float height = (vertices.size() >= 4) ? (vertices[3] - vertices[0]).GetLength() : (vertices[2] - vertices[1]).GetLength();

			// Calculate rotation from normal (portal faces along -Z by default)
			Quaternion rotation = Quaternion::Identity;
			Vector3 defaultNormal(0, 0, -1);
			if (normal.Dot(defaultNormal) < 0.999f)
			{
				Vector3 axis = defaultNormal.Cross(normal);
				if (axis.GetSquaredLength() > 0.0001f)
				{
					axis.Normalize();
					float dotVal = defaultNormal.Dot(normal);
					if (dotVal < -1.0f) dotVal = -1.0f;
					if (dotVal > 1.0f) dotVal = 1.0f;
					float angle = std::acos(dotVal);
					rotation = Quaternion(Radian(angle), axis);
				}
			}

			// Set the portal transform and dimensions
			portal.SetTransform(center, rotation, Vector3::UnitScale);
			portal.SetDimensions(width, height);
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

	void WorldModelEditorInstance::UpdatePortalGroupConnection(int32 portalIndex, int32 oldGroupA, int32 newGroupA, int32 newGroupB)
	{
		if (!m_worldModel || portalIndex < 0)
		{
			return;
		}

		// First, remove this portal reference from all groups
		for (size_t gi = 0; gi < m_worldModel->GetGroupCount(); ++gi)
		{
			auto* grp = m_worldModel->GetGroup(gi);
			if (grp)
			{
				auto& refs = grp->GetPortalRefs();
				refs.erase(std::remove_if(refs.begin(), refs.end(),
					[portalIndex](const WorldModelPortalRef& ref) {
						return ref.portalIndex == static_cast<uint16>(portalIndex);
					}), refs.end());
			}
		}

		// Now add references to the new groups
		if (newGroupA >= 0 && newGroupA < static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			auto* grpA = m_worldModel->GetGroup(newGroupA);
			if (grpA)
			{
				WorldModelPortalRef refA;
				refA.portalIndex = static_cast<uint16>(portalIndex);
				refA.groupIndex = newGroupB >= 0 ? static_cast<uint16>(newGroupB) : 0;
				refA.side = 1;
				grpA->GetPortalRefs().push_back(refA);
			}
		}

		if (newGroupB >= 0 && newGroupB < static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			auto* grpB = m_worldModel->GetGroup(newGroupB);
			if (grpB)
			{
				WorldModelPortalRef refB;
				refB.portalIndex = static_cast<uint16>(portalIndex);
				refB.groupIndex = newGroupA >= 0 ? static_cast<uint16>(newGroupA) : 0;
				refB.side = -1;
				grpB->GetPortalRefs().push_back(refB);
			}
		}
	}

	void WorldModelEditorInstance::CreateLight(int32 groupIndex, const Vector3& position, uint32 color, float intensity, WorldModelLight::LightType type)
	{
		if (!m_worldModel)
		{
			return;
		}

		WorldModelLight light;
		light.type = type;
		light.useAttenuation = true;
		light.color = color;
		light.position = position;
		light.intensity = intensity;
		light.attenuationStart = 0.0f;
		light.attenuationEnd = 10.0f;
		light.rotation = Quaternion::Identity;

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
			if (vis.meshEntity)
			{
				m_scene.DestroyEntity(*vis.meshEntity);
			}
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

			// Create mesh from group geometry if available
			const auto& vertices = group->GetVertices();
			const auto& indices = group->GetIndices();
			
			if (!vertices.empty() && !indices.empty())
			{
				const auto& normals = group->GetNormals();
				const auto& texCoords = group->GetTexCoords();
				const auto& vertexColors = group->GetVertexColors();

				// Create a manual mesh with unique name
				String meshName = "WMO_GroupMesh_" + std::to_string(m_meshCounter++) + "_" + std::to_string(i);
				MeshPtr groupMesh = MeshManager::Get().CreateManual(meshName);
				vis.mesh = groupMesh;
				
				if (groupMesh)
				{
					SubMesh& subMesh = groupMesh->CreateSubMesh();
					subMesh.useSharedVertices = false;
					
					// Create vertex data
					subMesh.vertexData = std::make_unique<VertexData>(&GraphicsDevice::Get());
					subMesh.vertexData->vertexCount = vertices.size();
					subMesh.vertexData->vertexStart = 0;
					
					// Setup vertex declaration - must match the format expected by Default.hmat
					// Order: Position, Color, Normal, Binormal, Tangent, TexCoord
					VertexDeclaration* decl = subMesh.vertexData->vertexDeclaration;
					uint32 offset = 0;
					offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
					offset += decl->AddElement(0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
					offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
					offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
					offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
					offset += decl->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();
					
					// Vertex struct matching the declaration
					struct VertexStruct
					{
						Vector3 position;
						uint32 color;
						Vector3 normal;
						Vector3 binormal;
						Vector3 tangent;
						float u, v;
					};
					
					// Build vertex buffer data
					std::vector<VertexStruct> vertexData(vertices.size());
					
					for (size_t v = 0; v < vertices.size(); ++v)
					{
						VertexStruct& vert = vertexData[v];
						
						// Position
						vert.position = vertices[v];
						
						// Color (ARGB format)
						vert.color = (v < vertexColors.size()) ? vertexColors[v] : 0xFFFFFFFF;
						
						// Normal
						if (v < normals.size())
						{
							vert.normal = normals[v];
						}
						else
						{
							vert.normal = Vector3::UnitY;
						}
						
						// Binormal - compute from normal
						Vector3 up = (std::abs(vert.normal.y) < 0.99f) ? Vector3::UnitY : Vector3::UnitX;
						vert.tangent = vert.normal.Cross(up).NormalizedCopy();
						vert.binormal = vert.tangent.Cross(vert.normal).NormalizedCopy();
						
						// TexCoord
						if (v < texCoords.size())
						{
							vert.u = texCoords[v].x;
							vert.v = texCoords[v].y;
						}
						else
						{
							vert.u = 0.0f;
							vert.v = 0.0f;
						}
					}
					
					// Create vertex buffer
					VertexBufferPtr vertexBuffer = GraphicsDevice::Get().CreateVertexBuffer(
						vertices.size(), offset, BufferUsage::StaticWriteOnly, vertexData.data());
					subMesh.vertexData->vertexBufferBinding->SetBinding(0, vertexBuffer);
					
					// Create index data
					subMesh.indexData = std::make_unique<IndexData>();
					subMesh.indexData->indexCount = indices.size();
					subMesh.indexData->indexStart = 0;
					subMesh.indexData->indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(
						indices.size(), IndexBufferSize::Index_32, BufferUsage::StaticWriteOnly, indices.data());
					
					// Set mesh bounds
					groupMesh->SetBounds(bbox);
					
					// Set a default material
					subMesh.SetMaterial(MaterialManager::Get().Load("Models/Default.hmat"));
					
					// Create entity from mesh
					vis.meshEntity = m_scene.CreateEntity("GroupEntity_" + std::to_string(i), groupMesh);
					if (vis.meshEntity)
					{
						vis.node->AttachObject(*vis.meshEntity);
					}
				}
			}

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

		// Create visualizations for each portal
		auto& portals = m_worldModel->GetPortals();
		for (size_t i = 0; i < portals.size(); ++i)
		{
			auto& portal = portals[i];
			if (!portal)
			{
				continue;
			}

			PortalVisualization vis;
			vis.node = &m_scene.CreateSceneNode("PortalNode_" + std::to_string(i));
			m_scene.GetRootSceneNode().AddChild(*vis.node);

			// Create portal visualization as a quad outline
			vis.renderable = m_scene.CreateManualRenderObject("PortalVis_" + std::to_string(i));
			vis.renderable->SetQueryFlags(0);
			
			// Set render queue to overlay so it renders on top of geometry
			vis.renderable->SetRenderQueueGroup(Overlay);

			auto lineOp = vis.renderable->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/WorldGrid.hmat"));

			// Get portal vertices
			const auto& worldVerts = portal->GetWorldVertices();
			if (worldVerts.size() >= 4)
			{
				// Draw the portal as a quad outline (cyan color for visibility)
				for (size_t v = 0; v < worldVerts.size(); ++v)
				{
					size_t nextV = (v + 1) % worldVerts.size();
					auto& line = lineOp->AddLine(worldVerts[v], worldVerts[nextV]);
					line.SetColor(0xFF00FFFF); // Cyan
				}

				// Draw diagonal lines to make it more visible
				if (worldVerts.size() == 4)
				{
					auto& diag1 = lineOp->AddLine(worldVerts[0], worldVerts[2]);
					diag1.SetColor(0xFF00FFFF);
					auto& diag2 = lineOp->AddLine(worldVerts[1], worldVerts[3]);
					diag2.SetColor(0xFF00FFFF);
				}
			}
			else
			{
				// Fallback: draw a simple cross at the portal position
				Vector3 pos = portal->GetPosition();
				float size = 1.0f;
				auto& line1 = lineOp->AddLine(pos - Vector3::UnitX * size, pos + Vector3::UnitX * size);
				line1.SetColor(0xFF00FFFF);
				auto& line2 = lineOp->AddLine(pos - Vector3::UnitY * size, pos + Vector3::UnitY * size);
				line2.SetColor(0xFF00FFFF);
				auto& line3 = lineOp->AddLine(pos - Vector3::UnitZ * size, pos + Vector3::UnitZ * size);
				line3.SetColor(0xFF00FFFF);
			}

			vis.node->AttachObject(*vis.renderable);
			m_portalVisualizations.push_back(std::move(vis));
		}
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
			if (vis.iconEntity)
			{
				m_scene.DestroyEntity(*vis.iconEntity);
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
			vis.node->SetOrientation(lightData.rotation);
			m_scene.GetRootSceneNode().AddChild(*vis.node);

			// Create appropriate light type
			LightType sceneType = LightType::Point;
			if (lightData.type == WorldModelLight::LightType::Spot)
			{
				sceneType = LightType::Spot;
			}
			
			vis.light = &m_scene.CreateLight("WMOLight_" + std::to_string(i), sceneType);
			
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

			// Create a visual marker for the light (small sphere/icon)
			vis.iconEntity = m_scene.CreateEntity("LightIcon_" + std::to_string(i), "Editor/Joint.hmsh");
			if (vis.iconEntity)
			{
				vis.iconEntity->SetQueryFlags(0);
				vis.node->AttachObject(*vis.iconEntity);
			}

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
			if (label.empty())
			{
				label = "(unnamed)";
			}
			
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

		// Selected group properties
		if (m_selectedGroupIndex >= 0 && m_selectedGroupIndex < static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			ImGui::Separator();
			ImGui::Text("Selected Group Properties:");

			auto* selectedGroup = m_worldModel->GetGroup(m_selectedGroupIndex);
			if (selectedGroup)
			{
				// Group name editing
				char nameBuffer[256];
				std::strncpy(nameBuffer, selectedGroup->GetName().c_str(), sizeof(nameBuffer) - 1);
				nameBuffer[sizeof(nameBuffer) - 1] = '\0';
				if (ImGui::InputText("Name##group", nameBuffer, sizeof(nameBuffer)))
				{
					selectedGroup->SetName(nameBuffer);
				}

				// Group flags
				bool isInterior = selectedGroup->IsInterior();
				if (ImGui::Checkbox("Interior##group", &isInterior))
				{
					uint32 flags = selectedGroup->GetFlags();
					if (isInterior)
					{
						flags |= 0x2000; // Interior flag
						flags &= ~0x8; // Clear exterior flag
					}
					else
					{
						flags &= ~0x2000;
					}
					selectedGroup->SetFlags(flags);
				}

				bool isExterior = selectedGroup->IsExterior();
				if (ImGui::Checkbox("Exterior##group", &isExterior))
				{
					uint32 flags = selectedGroup->GetFlags();
					if (isExterior)
					{
						flags |= 0x8; // Exterior flag
						flags &= ~0x2000; // Clear interior flag
					}
					else
					{
						flags &= ~0x8;
					}
					selectedGroup->SetFlags(flags);
				}
			}
		}
	}

	void WorldModelEditorInstance::DrawPortalsPanel()
	{
		if (!m_worldModel)
		{
			ImGui::Text("No world model loaded");
			return;
		}

		// Portal creation UI
		if (!m_creatingPortal)
		{
			if (m_worldModel->GetGroupCount() < 2)
			{
				ImGui::TextDisabled("Need at least 2 groups to create a portal");
			}
			else if (ImGui::Button("Create Portal..."))
			{
				m_creatingPortal = true;
				m_portalSourceGroup = -1;
				m_portalTargetGroup = -1;
			}
		}
		else
		{
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "Creating Portal:");
			
			// Source group selection
			ImGui::Text("Source Group:");
			ImGui::SameLine();
			
			// Cache the preview string before BeginCombo to avoid issues
			String sourcePreview = "Select...";
			if (m_portalSourceGroup >= 0 && m_portalSourceGroup < static_cast<int32>(m_worldModel->GetGroupCount()))
			{
				const auto* srcGroup = m_worldModel->GetGroup(m_portalSourceGroup);
				if (srcGroup)
				{
					sourcePreview = srcGroup->GetName();
					if (sourcePreview.empty())
					{
						sourcePreview = "(unnamed)";
					}
				}
			}
			
			if (ImGui::BeginCombo("##SourceGroup", sourcePreview.c_str()))
			{
				for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
				{
					const auto* group = m_worldModel->GetGroup(i);
					if (group && static_cast<int32>(i) != m_portalTargetGroup)
					{
						bool isSelected = m_portalSourceGroup == static_cast<int32>(i);
						// Use ##index suffix to ensure unique ID even if name is empty
						String itemLabel = group->GetName();
						if (itemLabel.empty())
						{
							itemLabel = "(unnamed)";
						}
						itemLabel += "##src" + std::to_string(i);
						if (ImGui::Selectable(itemLabel.c_str(), isSelected))
						{
							m_portalSourceGroup = static_cast<int32>(i);
						}
					}
				}
				ImGui::EndCombo();
			}

			// Target group selection
			ImGui::Text("Target Group:");
			ImGui::SameLine();
			
			// Cache the preview string before BeginCombo to avoid issues
			String targetPreview = "Select...";
			if (m_portalTargetGroup >= 0 && m_portalTargetGroup < static_cast<int32>(m_worldModel->GetGroupCount()))
			{
				const auto* tgtGroup = m_worldModel->GetGroup(m_portalTargetGroup);
				if (tgtGroup)
				{
					targetPreview = tgtGroup->GetName();
					if (targetPreview.empty())
					{
						targetPreview = "(unnamed)";
					}
				}
			}
			
			if (ImGui::BeginCombo("##TargetGroup", targetPreview.c_str()))
			{
				for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
				{
					const auto* group = m_worldModel->GetGroup(i);
					if (group && static_cast<int32>(i) != m_portalSourceGroup)
					{
						bool isSelected = m_portalTargetGroup == static_cast<int32>(i);
						// Use ##index suffix to ensure unique ID even if name is empty
						String itemLabel = group->GetName();
						if (itemLabel.empty())
						{
							itemLabel = "(unnamed)";
						}
						itemLabel += "##tgt" + std::to_string(i);
						if (ImGui::Selectable(itemLabel.c_str(), isSelected))
						{
							m_portalTargetGroup = static_cast<int32>(i);
						}
					}
				}
				ImGui::EndCombo();
			}

			ImGui::Separator();

			// Create button
			bool canCreate = m_portalSourceGroup >= 0 && m_portalTargetGroup >= 0;
			if (!canCreate)
			{
				ImGui::BeginDisabled();
			}
			
			if (ImGui::Button("Create Portal"))
			{
				// Create a default portal between the two groups
				// Use the midpoint between the two group bounding boxes as the portal location
				const auto* srcGroup = m_worldModel->GetGroup(m_portalSourceGroup);
				const auto* tgtGroup = m_worldModel->GetGroup(m_portalTargetGroup);
				
				if (srcGroup && tgtGroup)
				{
					// Calculate portal position at the boundary between groups
					Vector3 srcCenter = srcGroup->GetBoundingBox().GetCenter();
					Vector3 tgtCenter = tgtGroup->GetBoundingBox().GetCenter();
					Vector3 portalCenter = (srcCenter + tgtCenter) * 0.5f;
					
					// Create a simple quad portal
					Vector3 direction = (tgtCenter - srcCenter).NormalizedCopy();
					Vector3 up = Vector3::UnitY;
					Vector3 right = direction.Cross(up).NormalizedCopy();
					if (right.GetSquaredLength() < 0.001f)
					{
						right = Vector3::UnitX;
					}
					up = right.Cross(direction).NormalizedCopy();
					
					// Default portal size of 2x2 units
					float halfWidth = 1.0f;
					float halfHeight = 1.0f;
					
					std::vector<Vector3> vertices;
					vertices.push_back(portalCenter - right * halfWidth - up * halfHeight);
					vertices.push_back(portalCenter + right * halfWidth - up * halfHeight);
					vertices.push_back(portalCenter + right * halfWidth + up * halfHeight);
					vertices.push_back(portalCenter - right * halfWidth + up * halfHeight);
					
					CreatePortal(m_portalSourceGroup, m_portalTargetGroup, vertices);
				}
				
				// Reset state
				m_creatingPortal = false;
				m_portalSourceGroup = -1;
				m_portalTargetGroup = -1;
			}
			
			if (!canCreate)
			{
				ImGui::EndDisabled();
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_creatingPortal = false;
				m_portalSourceGroup = -1;
				m_portalTargetGroup = -1;
			}
		}

		ImGui::Separator();

		// Portal list
		ImGui::Text("Portals:");
		
		const auto& portals = m_worldModel->GetPortals();
		if (portals.empty())
		{
			ImGui::TextDisabled("No portals");
		}
		else
		{
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

		// Delete button
		if (m_selectedPortalIndex >= 0 && m_selectedPortalIndex < static_cast<int32>(portals.size()))
		{
			ImGui::Separator();
			
			// Portal properties
			ImGui::Text("Selected Portal Properties:");
			
			auto& portal = portals[m_selectedPortalIndex];
			if (portal)
			{
				// Position (with InputFloat for precise entry)
				Vector3 pos = portal->GetPosition();
				float posArr[3] = { pos.x, pos.y, pos.z };
				if (ImGui::InputFloat3("Position##portal", posArr, "%.2f"))
				{
					portal->SetTransform(Vector3(posArr[0], posArr[1], posArr[2]), portal->GetRotation(), portal->GetScale());
					UpdatePortalVisualizations();
				}

				// Rotation as Yaw/Pitch/Roll (more intuitive)
				Quaternion rot = portal->GetRotation();
				Matrix3 rotMat;
				rot.ToRotationMatrix(rotMat);
				Radian yaw, pitch, roll;
				rotMat.ToEulerAnglesYXZ(yaw, pitch, roll);
				
				float yawDeg = Degree(yaw).GetValueDegrees();
				float pitchDeg = Degree(pitch).GetValueDegrees();
				float rollDeg = Degree(roll).GetValueDegrees();
				
				bool rotChanged = false;
				if (ImGui::InputFloat("Yaw##portal", &yawDeg, 1.0f, 10.0f, "%.1f"))
				{
					rotChanged = true;
				}
				if (ImGui::InputFloat("Pitch##portal", &pitchDeg, 1.0f, 10.0f, "%.1f"))
				{
					rotChanged = true;
				}
				if (ImGui::InputFloat("Roll##portal", &rollDeg, 1.0f, 10.0f, "%.1f"))
				{
					rotChanged = true;
				}
				
				if (rotChanged)
				{
					// Convert back to quaternion
					Matrix3 newRotMat;
					newRotMat.FromEulerAnglesYXZ(Degree(yawDeg), Degree(pitchDeg), Degree(rollDeg));
					Quaternion newRot;
					newRot.FromRotationMatrix(newRotMat);
					portal->SetTransform(portal->GetPosition(), newRot, portal->GetScale());
					UpdatePortalVisualizations();
				}

				// Dimensions (with InputFloat for precise entry)
				float width = portal->GetWidth();
				float height = portal->GetHeight();
				if (ImGui::InputFloat("Width##portal", &width, 0.1f, 1.0f, "%.2f"))
				{
					if (width > 0.0f)
					{
						portal->SetDimensions(width, height);
						UpdatePortalVisualizations();
					}
				}
				if (ImGui::InputFloat("Height##portal", &height, 0.1f, 1.0f, "%.2f"))
				{
					if (height > 0.0f)
					{
						portal->SetDimensions(width, height);
						UpdatePortalVisualizations();
					}
				}

				// Portal type
				PortalType ptype = portal->GetPortalType();
				int ptypeInt = static_cast<int>(ptype);
				const char* typeNames[] = { "One-Way", "Two-Way" };
				if (ImGui::Combo("Type##portal", &ptypeInt, typeNames, 2))
				{
					portal->SetPortalType(static_cast<PortalType>(ptypeInt));
				}

				// Active state
				bool active = portal->IsActive();
				if (ImGui::Checkbox("Active##portal", &active))
				{
					portal->SetActive(active);
				}

				// Connected groups editing
				ImGui::Separator();
				ImGui::Text("Connected Groups:");

				// Find which groups reference this portal
				int32 groupA = -1, groupB = -1;
				for (size_t gi = 0; gi < m_worldModel->GetGroupCount(); ++gi)
				{
					auto* grp = m_worldModel->GetGroup(gi);
					if (grp)
					{
						for (const auto& ref : grp->GetPortalRefs())
						{
							if (ref.portalIndex == static_cast<uint16>(m_selectedPortalIndex))
							{
								if (groupA < 0)
								{
									groupA = static_cast<int32>(gi);
								}
								else
								{
									groupB = static_cast<int32>(gi);
								}
								break;
							}
						}
					}
				}

				// Group A selection
				String groupAPreview = "(none)";
				if (groupA >= 0 && groupA < static_cast<int32>(m_worldModel->GetGroupCount()))
				{
					const auto* grpA = m_worldModel->GetGroup(groupA);
					if (grpA)
					{
						groupAPreview = grpA->GetName();
						if (groupAPreview.empty())
						{
							groupAPreview = "Group " + std::to_string(groupA);
						}
					}
				}
				
				if (ImGui::BeginCombo("Group A##portalgrp", groupAPreview.c_str()))
				{
					for (size_t gi = 0; gi < m_worldModel->GetGroupCount(); ++gi)
					{
						const auto* grp = m_worldModel->GetGroup(gi);
						if (grp && static_cast<int32>(gi) != groupB)
						{
							String itemLabel = grp->GetName();
							if (itemLabel.empty())
							{
								itemLabel = "Group " + std::to_string(gi);
							}
							itemLabel += "##grpA" + std::to_string(gi);
							
							if (ImGui::Selectable(itemLabel.c_str(), groupA == static_cast<int32>(gi)))
							{
								// Update portal references
								UpdatePortalGroupConnection(m_selectedPortalIndex, groupA, static_cast<int32>(gi), groupB);
							}
						}
					}
					ImGui::EndCombo();
				}

				// Group B selection
				String groupBPreview = "(none)";
				if (groupB >= 0 && groupB < static_cast<int32>(m_worldModel->GetGroupCount()))
				{
					const auto* grpB = m_worldModel->GetGroup(groupB);
					if (grpB)
					{
						groupBPreview = grpB->GetName();
						if (groupBPreview.empty())
						{
							groupBPreview = "Group " + std::to_string(groupB);
						}
					}
				}
				
				if (ImGui::BeginCombo("Group B##portalgrp", groupBPreview.c_str()))
				{
					for (size_t gi = 0; gi < m_worldModel->GetGroupCount(); ++gi)
					{
						const auto* grp = m_worldModel->GetGroup(gi);
						if (grp && static_cast<int32>(gi) != groupA)
						{
							String itemLabel = grp->GetName();
							if (itemLabel.empty())
							{
								itemLabel = "Group " + std::to_string(gi);
							}
							itemLabel += "##grpB" + std::to_string(gi);
							
							if (ImGui::Selectable(itemLabel.c_str(), groupB == static_cast<int32>(gi)))
							{
								// Update portal references
								UpdatePortalGroupConnection(m_selectedPortalIndex, groupA, groupA, static_cast<int32>(gi));
							}
						}
					}
					ImGui::EndCombo();
				}
			}

			ImGui::Separator();
			if (ImGui::Button("Delete Selected Portal"))
			{
				RemovePortal(m_selectedPortalIndex);
			}
		}
	}

	void WorldModelEditorInstance::DrawMeshRefsPanel(int32 groupIndex)
	{
		if (!m_worldModel || groupIndex < 0 || groupIndex >= static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			ImGui::Text("Select a group first");
			return;
		}

		auto* group = m_worldModel->GetGroup(groupIndex);
		if (!group)
		{
			return;
		}

		ImGui::Text("Meshes in '%s':", group->GetName().c_str());
		ImGui::Separator();

		// Add mesh button
		if (ImGui::Button("Add Mesh..."))
		{
			m_showAddMeshRefDialog = true;
			std::memset(m_addMeshRefPath, 0, sizeof(m_addMeshRefPath));
			std::memset(m_addMeshRefName, 0, sizeof(m_addMeshRefName));
		}

		// Drag-drop target for mesh files
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Drop .hmsh files here to add");
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
			{
				const String meshPath = *static_cast<String*>(payload->Data);
				AddMeshRefToGroup(groupIndex, meshPath);
			}
			ImGui::EndDragDropTarget();
		}

		// List mesh references
		const auto& meshRefs = group->GetMeshRefs();
		for (size_t i = 0; i < meshRefs.size(); ++i)
		{
			const auto& meshRef = meshRefs[i];
			ImGui::PushID(static_cast<int>(i));

			bool isSelected = m_selectedMeshRefIndex == static_cast<int32>(i);
			String label = meshRef.name;
			if (label.empty())
			{
				label = meshRef.meshPath;
				// Extract just the filename
				size_t lastSlash = label.find_last_of("/\\");
				if (lastSlash != String::npos)
				{
					label = label.substr(lastSlash + 1);
				}
			}

			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				m_selectedMeshRefIndex = static_cast<int32>(i);
				m_selection.Clear();
				
				// Select this mesh for transform editing
				if (static_cast<size_t>(groupIndex) < m_groupVisualizations.size())
				{
					auto& groupViz = m_groupVisualizations[groupIndex];
					if (i < groupViz.meshRefVisualizations.size() && groupViz.meshRefVisualizations[i].node)
					{
						m_selection.AddSelectable(std::make_unique<SelectedGroupMesh>(*group, i, *groupViz.meshRefVisualizations[i].node));
					}
				}
			}

			// Context menu
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Duplicate"))
				{
					WorldModelMeshRef newRef = meshRef;
					newRef.name = meshRef.name + "_copy";
					newRef.position += Vector3(1.0f, 0.0f, 0.0f);
					group->AddMeshRef(newRef);
					UpdateMeshRefVisualizations(groupIndex);
				}
				if (ImGui::MenuItem("Delete"))
				{
					RemoveMeshRefFromGroup(groupIndex, i);
					ImGui::EndPopup();
					ImGui::PopID();
					break;
				}
				ImGui::EndPopup();
			}

			// Visibility toggle
			ImGui::SameLine();
			if (static_cast<size_t>(groupIndex) < m_groupVisualizations.size() && 
				i < m_groupVisualizations[groupIndex].meshRefVisualizations.size())
			{
				bool visible = m_groupVisualizations[groupIndex].meshRefVisualizations[i].visible;
				if (ImGui::Checkbox("##vis", &visible))
				{
					m_groupVisualizations[groupIndex].meshRefVisualizations[i].visible = visible;
					if (m_groupVisualizations[groupIndex].meshRefVisualizations[i].entity)
					{
						m_groupVisualizations[groupIndex].meshRefVisualizations[i].entity->SetVisible(visible);
					}
				}
			}

			ImGui::PopID();
		}

		// Selected mesh properties
		if (m_selectedMeshRefIndex >= 0 && m_selectedMeshRefIndex < static_cast<int32>(meshRefs.size()))
		{
			ImGui::Separator();
			ImGui::Text("Properties:");

			auto* selectedRef = group->GetMeshRef(m_selectedMeshRefIndex);
			if (selectedRef)
			{
				// Name
				char nameBuffer[256];
				std::strncpy(nameBuffer, selectedRef->name.c_str(), sizeof(nameBuffer) - 1);
				nameBuffer[sizeof(nameBuffer) - 1] = '\0';
				if (ImGui::InputText("Name##meshref", nameBuffer, sizeof(nameBuffer)))
				{
					selectedRef->name = nameBuffer;
				}

				// Position
				float pos[3] = { selectedRef->position.x, selectedRef->position.y, selectedRef->position.z };
				if (ImGui::DragFloat3("Position##meshref", pos, 0.1f))
				{
					selectedRef->position = Vector3(pos[0], pos[1], pos[2]);
					// Update visualization
					if (static_cast<size_t>(groupIndex) < m_groupVisualizations.size())
					{
						auto& groupViz = m_groupVisualizations[groupIndex];
						if (static_cast<size_t>(m_selectedMeshRefIndex) < groupViz.meshRefVisualizations.size() &&
							groupViz.meshRefVisualizations[m_selectedMeshRefIndex].node)
						{
							groupViz.meshRefVisualizations[m_selectedMeshRefIndex].node->SetPosition(selectedRef->position);
						}
					}
				}

				// Scale
				float scl[3] = { selectedRef->scale.x, selectedRef->scale.y, selectedRef->scale.z };
				if (ImGui::DragFloat3("Scale##meshref", scl, 0.01f, 0.01f, 100.0f))
				{
					selectedRef->scale = Vector3(scl[0], scl[1], scl[2]);
					// Update visualization
					if (static_cast<size_t>(groupIndex) < m_groupVisualizations.size())
					{
						auto& groupViz = m_groupVisualizations[groupIndex];
						if (static_cast<size_t>(m_selectedMeshRefIndex) < groupViz.meshRefVisualizations.size() &&
							groupViz.meshRefVisualizations[m_selectedMeshRefIndex].node)
						{
							groupViz.meshRefVisualizations[m_selectedMeshRefIndex].node->SetScale(selectedRef->scale);
						}
					}
				}

				// Material override
				char matBuffer[512];
				std::strncpy(matBuffer, selectedRef->materialOverride.c_str(), sizeof(matBuffer) - 1);
				matBuffer[sizeof(matBuffer) - 1] = '\0';
				if (ImGui::InputText("Material##meshref", matBuffer, sizeof(matBuffer)))
				{
					selectedRef->materialOverride = matBuffer;
					// Apply the material override
					if (static_cast<size_t>(groupIndex) < m_groupVisualizations.size())
					{
						auto& groupViz = m_groupVisualizations[groupIndex];
						if (static_cast<size_t>(m_selectedMeshRefIndex) < groupViz.meshRefVisualizations.size() &&
							groupViz.meshRefVisualizations[m_selectedMeshRefIndex].entity)
						{
							if (!selectedRef->materialOverride.empty())
							{
								auto material = MaterialManager::Get().Load(selectedRef->materialOverride);
								if (material)
								{
									groupViz.meshRefVisualizations[m_selectedMeshRefIndex].entity->SetMaterial(material);
								}
							}
						}
					}
				}

				// Mesh path (read-only)
				ImGui::TextDisabled("Mesh: %s", selectedRef->meshPath.c_str());
			}
		}

		// Add mesh dialog
		if (m_showAddMeshRefDialog)
		{
			ImGui::OpenPopup("Add Mesh Reference");
		}

		if (ImGui::BeginPopupModal("Add Mesh Reference", &m_showAddMeshRefDialog))
		{
			ImGui::InputText("Mesh Path##add", m_addMeshRefPath, sizeof(m_addMeshRefPath));
			ImGui::InputText("Name##add", m_addMeshRefName, sizeof(m_addMeshRefName));

			// Drag-drop target for mesh files
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
				{
					std::string path(static_cast<const char*>(payload->Data), payload->DataSize - 1);
					std::strncpy(m_addMeshRefPath, path.c_str(), sizeof(m_addMeshRefPath) - 1);
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Separator();

			if (ImGui::Button("Add", ImVec2(120, 0)))
			{
				if (std::strlen(m_addMeshRefPath) > 0)
				{
					AddMeshRefToGroup(groupIndex, m_addMeshRefPath, Vector3::Zero, Quaternion::Identity, Vector3::UnitScale, m_addMeshRefName);
					m_showAddMeshRefDialog = false;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				m_showAddMeshRefDialog = false;
			}

			ImGui::EndPopup();
		}
	}

	void WorldModelEditorInstance::DrawChildWMOsPanel()
	{
		if (!m_worldModel)
		{
			ImGui::Text("No world model loaded");
			return;
		}

		ImGui::Text("Child WMO References:");
		ImGui::Separator();

		// Add child WMO button
		if (ImGui::Button("Add Child WMO..."))
		{
			m_showAddChildWMODialog = true;
			std::memset(m_addChildWMOPath, 0, sizeof(m_addChildWMOPath));
			std::memset(m_addChildWMOName, 0, sizeof(m_addChildWMOName));
		}

		// List child WMO references
		const auto& childRefs = m_worldModel->GetChildRefs();
		for (size_t i = 0; i < childRefs.size(); ++i)
		{
			const auto& childRef = childRefs[i];
			ImGui::PushID(static_cast<int>(i));

			bool isSelected = m_selectedChildWMOIndex == static_cast<int32>(i);
			String label = childRef.name;
			if (label.empty())
			{
				label = childRef.wmoPath;
				// Extract just the filename
				size_t lastSlash = label.find_last_of("/\\");
				if (lastSlash != String::npos)
				{
					label = label.substr(lastSlash + 1);
				}
			}

			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				m_selectedChildWMOIndex = static_cast<int32>(i);
				m_selection.Clear();
				
				// Select this child WMO for transform editing
				if (i < m_childWMOVisualizations.size() && m_childWMOVisualizations[i].node)
				{
					m_selection.AddSelectable(std::make_unique<SelectedChildWMO>(*m_worldModel, i, *m_childWMOVisualizations[i].node));
				}
			}

			// Context menu
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Duplicate"))
				{
					WorldModelChildRef newRef = childRef;
					newRef.name = childRef.name + "_copy";
					newRef.position += Vector3(5.0f, 0.0f, 0.0f);
					m_worldModel->AddChildRef(newRef);
					UpdateChildWMOVisualizations();
				}
				if (ImGui::MenuItem("Delete"))
				{
					RemoveChildWMO(i);
					ImGui::EndPopup();
					ImGui::PopID();
					break;
				}
				ImGui::EndPopup();
			}

			// Visibility toggle
			ImGui::SameLine();
			if (i < m_childWMOVisualizations.size())
			{
				bool visible = m_childWMOVisualizations[i].visible;
				if (ImGui::Checkbox("##vis", &visible))
				{
					m_childWMOVisualizations[i].visible = visible;
					// TODO: Update visibility of child WMO entities
				}
			}

			ImGui::PopID();
		}

		// Selected child WMO properties
		if (m_selectedChildWMOIndex >= 0 && m_selectedChildWMOIndex < static_cast<int32>(childRefs.size()))
		{
			ImGui::Separator();
			ImGui::Text("Properties:");

			auto* selectedRef = m_worldModel->GetChildRef(m_selectedChildWMOIndex);
			if (selectedRef)
			{
				// Name
				char nameBuffer[256];
				std::strncpy(nameBuffer, selectedRef->name.c_str(), sizeof(nameBuffer) - 1);
				nameBuffer[sizeof(nameBuffer) - 1] = '\0';
				if (ImGui::InputText("Name##childwmo", nameBuffer, sizeof(nameBuffer)))
				{
					selectedRef->name = nameBuffer;
				}

				// Position
				float pos[3] = { selectedRef->position.x, selectedRef->position.y, selectedRef->position.z };
				if (ImGui::DragFloat3("Position##childwmo", pos, 0.1f))
				{
					selectedRef->position = Vector3(pos[0], pos[1], pos[2]);
					// Update visualization
					if (static_cast<size_t>(m_selectedChildWMOIndex) < m_childWMOVisualizations.size() &&
						m_childWMOVisualizations[m_selectedChildWMOIndex].node)
					{
						m_childWMOVisualizations[m_selectedChildWMOIndex].node->SetPosition(selectedRef->position);
					}
				}

				// Scale
				float scl[3] = { selectedRef->scale.x, selectedRef->scale.y, selectedRef->scale.z };
				if (ImGui::DragFloat3("Scale##childwmo", scl, 0.01f, 0.01f, 100.0f))
				{
					selectedRef->scale = Vector3(scl[0], scl[1], scl[2]);
					// Update visualization
					if (static_cast<size_t>(m_selectedChildWMOIndex) < m_childWMOVisualizations.size() &&
						m_childWMOVisualizations[m_selectedChildWMOIndex].node)
					{
						m_childWMOVisualizations[m_selectedChildWMOIndex].node->SetScale(selectedRef->scale);
					}
				}

				// WMO path (read-only)
				ImGui::TextDisabled("WMO: %s", selectedRef->wmoPath.c_str());
			}
		}

		// Add child WMO dialog
		if (m_showAddChildWMODialog)
		{
			ImGui::OpenPopup("Add Child WMO");
		}

		if (ImGui::BeginPopupModal("Add Child WMO", &m_showAddChildWMODialog))
		{
			ImGui::InputText("WMO Path##add", m_addChildWMOPath, sizeof(m_addChildWMOPath));
			ImGui::InputText("Name##add", m_addChildWMOName, sizeof(m_addChildWMOName));

			// Drag-drop target for WMO files
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
				{
					std::string path(static_cast<const char*>(payload->Data), payload->DataSize - 1);
					std::strncpy(m_addChildWMOPath, path.c_str(), sizeof(m_addChildWMOPath) - 1);
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Separator();

			if (ImGui::Button("Add", ImVec2(120, 0)))
			{
				if (std::strlen(m_addChildWMOPath) > 0)
				{
					AddChildWMO(m_addChildWMOPath, Vector3::Zero, Quaternion::Identity, Vector3::UnitScale, m_addChildWMOName);
					m_showAddChildWMODialog = false;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				m_showAddChildWMODialog = false;
			}

			ImGui::EndPopup();
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
		if (ImGui::Button("Add Point Light"))
		{
			CreateLight(m_selectedGroupIndex, Vector3::Zero, 0xFFFFFFFF, 1.0f, WorldModelLight::LightType::Omni);
		}

		ImGui::SameLine();

		if (ImGui::Button("Add Spot Light"))
		{
			CreateLight(m_selectedGroupIndex, Vector3::Zero, 0xFFFFFFFF, 1.0f, WorldModelLight::LightType::Spot);
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

		auto& lights = m_worldModel->GetLights();
		for (size_t i = 0; i < lights.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));

			bool isSelected = m_selectedLightIndex == static_cast<int32>(i);
			
			// Show light type in label
			const char* typeStr = "Point";
			if (lights[i].type == WorldModelLight::LightType::Spot)
			{
				typeStr = "Spot";
			}
			String label = String(typeStr) + " Light " + std::to_string(i);

			if (ImGui::Selectable(label.c_str(), isSelected))
			{
				m_selectedLightIndex = static_cast<int32>(i);
			}

			ImGui::PopID();
		}

		// Selected light properties
		if (m_selectedLightIndex >= 0 && m_selectedLightIndex < static_cast<int32>(lights.size()))
		{
			ImGui::Separator();
			ImGui::Text("Light Properties:");

			auto& light = lights[m_selectedLightIndex];

			// Light type (Point or Spot only)
			int typeInt = (light.type == WorldModelLight::LightType::Spot) ? 1 : 0;
			const char* typeNames[] = { "Point", "Spot" };
			if (ImGui::Combo("Type##light", &typeInt, typeNames, 2))
			{
				light.type = (typeInt == 1) ? WorldModelLight::LightType::Spot : WorldModelLight::LightType::Omni;
				UpdateLightVisualizations();
			}

			// Position
			float posArr[3] = { light.position.x, light.position.y, light.position.z };
			if (ImGui::InputFloat3("Position##light", posArr, "%.2f"))
			{
				light.position = Vector3(posArr[0], posArr[1], posArr[2]);
				UpdateLightVisualizations();
			}

			// Color picker
			float colorArr[3] = {
				((light.color >> 16) & 0xFF) / 255.0f,
				((light.color >> 8) & 0xFF) / 255.0f,
				(light.color & 0xFF) / 255.0f
			};
			if (ImGui::ColorEdit3("Color##light", colorArr))
			{
				uint32 r = static_cast<uint32>(colorArr[0] * 255.0f) & 0xFF;
				uint32 g = static_cast<uint32>(colorArr[1] * 255.0f) & 0xFF;
				uint32 b = static_cast<uint32>(colorArr[2] * 255.0f) & 0xFF;
				light.color = 0xFF000000 | (r << 16) | (g << 8) | b;
				UpdateLightVisualizations();
			}

			// Intensity
			if (ImGui::InputFloat("Intensity##light", &light.intensity, 0.1f, 1.0f, "%.2f"))
			{
				UpdateLightVisualizations();
			}

			// Range (attenuation end)
			if (ImGui::InputFloat("Range##light", &light.attenuationEnd, 1.0f, 5.0f, "%.1f"))
			{
				if (light.attenuationEnd < 0.1f)
				{
					light.attenuationEnd = 0.1f;
				}
				UpdateLightVisualizations();
			}

			// Direction for spot lights
			if (light.type == WorldModelLight::LightType::Spot)
			{
				ImGui::Separator();
				ImGui::Text("Spot Direction:");
				
				Matrix3 rotMat;
				light.rotation.ToRotationMatrix(rotMat);
				Radian yaw, pitch, roll;
				rotMat.ToEulerAnglesYXZ(yaw, pitch, roll);
				
				float yawDeg = Degree(yaw).GetValueDegrees();
				float pitchDeg = Degree(pitch).GetValueDegrees();
				
				bool rotChanged = false;
				if (ImGui::InputFloat("Yaw##lightdir", &yawDeg, 1.0f, 10.0f, "%.1f"))
				{
					rotChanged = true;
				}
				if (ImGui::InputFloat("Pitch##lightdir", &pitchDeg, 1.0f, 10.0f, "%.1f"))
				{
					rotChanged = true;
				}
				
				if (rotChanged)
				{
					Matrix3 newRotMat;
					newRotMat.FromEulerAnglesYXZ(Degree(yawDeg), Degree(pitchDeg), Radian(0));
					light.rotation.FromRotationMatrix(newRotMat);
					UpdateLightVisualizations();
				}
			}
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

				ImGui::Separator();

				// Geometry assignment
				ImGui::Text("Geometry");
				
				bool hasGeometry = !group->GetVertices().empty();
				if (hasGeometry)
				{
					ImGui::Text("Vertices: %zu", group->GetVertices().size());
					ImGui::Text("Triangles: %zu", group->GetIndices().size() / 3);
					
					if (ImGui::Button("Clear Geometry"))
					{
						group->GetVertices().clear();
						group->GetNormals().clear();
						group->GetTexCoords().clear();
						group->GetVertexColors().clear();
						group->GetIndices().clear();
						group->GetMaterialIndices().clear();
						UpdateGroupVisualizations();
					}
				}
				else
				{
					ImGui::TextDisabled("No geometry assigned");
				}
				
				// Drag-drop target for mesh files
				ImGui::Button("Drag .hmsh file here to assign geometry");
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
					{
						String meshPath = *static_cast<String*>(payload->Data);
						AssignMeshToGroup(m_selectedGroupIndex, meshPath);
					}
					ImGui::EndDragDropTarget();
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
