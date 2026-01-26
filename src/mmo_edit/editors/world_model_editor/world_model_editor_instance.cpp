// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_editor_instance.h"
#include "world_model_selectables.h"
#include "panels/world_model_groups_panel.h"
#include "panels/world_model_lights_panel.h"
#include "panels/world_model_portals_panel.h"
#include "panels/world_model_doodads_panel.h"
#include "panels/world_model_properties_panel.h"

#include <algorithm>
#include <sstream>
#include <imgui_internal.h>

#include "editor_host.h"
#include "world_model_editor.h"
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
#include "graphics/graphics_device.h"
#include "terrain/page.h"
#include "terrain/tile.h"

namespace mmo
{
	// Chunk definitions
	static const ChunkMagic versionChunk = MakeChunkMagic('MVER');
	static const ChunkMagic meshChunk = MakeChunkMagic('MESH');
	static const ChunkMagic entityChunk = MakeChunkMagic('MENT');

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

		// Create deferred renderer for proper lighting support
		m_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), m_scene, 1920, 1080);

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

			auto& selectedObjects = m_selection.GetSelectedObjects();
			if (selectedObjects.empty())
			{
				return;
			}

			// Handle mesh duplication - duplicate stays at current position, original continues moving
			if (auto* selectedMesh = dynamic_cast<SelectedGroupMesh*>(selectedObjects.back().get()))
			{
				WorldModelGroup& group = selectedMesh->GetGroup();
				const size_t meshIndex = selectedMesh->GetMeshIndex();
				
				// Perform the duplication
				selectedMesh->Duplicate();

				// Update visualizations to create the scene node for the new mesh
				// This rebuilds ALL mesh ref nodes, invalidating the current selection's node pointer
				for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
				{
					if (m_worldModel->GetGroup(i) == &group)
					{
						UpdateMeshRefVisualizations(i);
						
						// Update the node pointer in the selection to point to the new node
						if (i < m_groupVisualizations.size())
						{
							auto& groupViz = m_groupVisualizations[i];
							if (meshIndex < groupViz.meshRefVisualizations.size() &&
								groupViz.meshRefVisualizations[meshIndex].node)
							{
								selectedMesh->UpdateNode(groupViz.meshRefVisualizations[meshIndex].node);
							}
						}
						break;
					}
				}
				// Selection stays on original - transform continues
			}
			else if (auto* selectedLight = dynamic_cast<SelectedWorldModelLight*>(selectedObjects.back().get()))
			{
				const size_t lightIndex = selectedLight->GetLightIndex();
				
				// Perform the duplication
				selectedLight->Duplicate();

				// Update light visualizations - this rebuilds ALL light nodes
				UpdateLightVisualizations();
				
				// Update the node pointer in the selection to point to the new node
				if (lightIndex < m_lightVisualizations.size() && m_lightVisualizations[lightIndex].node)
				{
					selectedLight->UpdateNode(m_lightVisualizations[lightIndex].node);
				}
				// Selection stays on original - transform continues
			}
			else
			{
				// Generic duplication for other types
				for (const auto& selected : selectedObjects)
				{
					selected->Duplicate();
				}
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
				
				// Sync ambient color to scene for deferred renderer
				const uint32 ambientColor = m_worldModel->GetAmbientColor();
				m_scene.SetAmbientColor(Vector3(
					((ambientColor >> 16) & 0xFF) / 255.0f,
					((ambientColor >> 8) & 0xFF) / 255.0f,
					(ambientColor & 0xFF) / 255.0f
				));
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

		if (!m_deferredRenderer) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		// Update portal culling preview if enabled
		if (m_previewPortalCulling && m_worldModel)
		{
			UpdatePortalCullingPreview();
		}

		auto& gx = GraphicsDevice::Get();

		// Render using the deferred renderer for proper lighting support
		gx.CaptureState();
		gx.Reset();
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);
		m_camera->SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);

		m_deferredRenderer->Render(m_scene, *m_camera);
		m_transformWidget->Update(m_camera);
		
		gx.RestoreState();
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
			GroupsPanelCallbacks groupsCallbacks;
			groupsCallbacks.onRemoveGroup = [this](size_t index) { RemoveGroup(index); };
			groupsCallbacks.onSelectGroup = [this](int32 index) 
			{ 
				m_selectedGroupIndex = index;
				m_selection.Clear();
				if (index >= 0 && index < static_cast<int32>(m_groupVisualizations.size()) &&
					m_groupVisualizations[index].node)
				{
					auto* group = m_worldModel->GetGroup(index);
					if (group)
					{
						m_selection.AddSelectable(std::make_unique<SelectedWorldModelGroup>(
							*group, *m_groupVisualizations[index].node));
					}
				}
			};
			groupsCallbacks.onSetGroupVisibility = [this](size_t index, bool visible) 
			{
				if (index < m_groupVisualizations.size())
				{
					m_groupVisualizations[index].visible = visible;
					if (m_groupVisualizations[index].node)
					{
						m_groupVisualizations[index].node->SetVisible(visible, true);
					}
				}
			};

			DrawGroupsPanel(m_worldModel.get(), m_groupVisualizations, m_selectedGroupIndex, m_groupsPanelState, groupsCallbacks);
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
			PortalCreationState portalCreationState;
			portalCreationState.creatingPortal = m_creatingPortal;
			portalCreationState.sourceGroup = m_portalSourceGroup;
			portalCreationState.targetGroup = m_portalTargetGroup;

			PortalsPanelCallbacks portalCallbacks;
			portalCallbacks.onCreatePortal = [this](int32 groupA, int32 groupB, const std::vector<Vector3>& vertices) 
			{
				CreatePortal(groupA, groupB, vertices);
			};
			portalCallbacks.onSelectPortal = [this](int32 index) 
			{
				m_selectedPortalIndex = index;
			};
			portalCallbacks.onRemovePortal = [this](int32 index) 
			{
				RemovePortal(static_cast<size_t>(index));
				if (m_selectedPortalIndex >= index)
				{
					m_selectedPortalIndex = std::max(-1, m_selectedPortalIndex - 1);
				}
			};
			portalCallbacks.onPortalChanged = [this]() 
			{
				UpdatePortalVisualizations();
			};
			portalCallbacks.onFocusPosition = [this](const Vector3& pos) 
			{
				m_cameraAnchor->SetPosition(pos);
				m_cameraVelocity = Vector3::Zero;
			};
			portalCallbacks.onUpdatePortalGroupConnection = [this](int32 portalIndex, int32 oldGroupA, int32 newGroupA, int32 newGroupB)
			{
				UpdatePortalGroupConnection(portalIndex, oldGroupA, newGroupA, newGroupB);
			};

			DrawPortalsPanel(m_worldModel.get(), m_selectedPortalIndex, portalCreationState, portalCallbacks);

			m_creatingPortal = portalCreationState.creatingPortal;
			m_portalSourceGroup = portalCreationState.sourceGroup;
			m_portalTargetGroup = portalCreationState.targetGroup;
		}
		ImGui::End();

		// Doodads panel  
		if (ImGui::Begin(doodadsId.c_str()))
		{
			DoodadsPanelCallbacks doodadCallbacks;
			doodadCallbacks.onCreateDoodadSet = [this](const String& name) 
			{
				CreateDoodadSet(name);
			};

			DrawDoodadsPanel(m_worldModel.get(), m_selectedDoodadSetIndex, doodadCallbacks);
		}
		ImGui::End();

		// Lights panel
		if (ImGui::Begin(lightsId.c_str()))
		{
			LightsPanelState lightsState;
			lightsState.selectedLightIndex = m_selectedLightIndex;

			LightsPanelCallbacks lightsCallbacks;
			lightsCallbacks.onCreateLight = [this](int32 groupIndex, const Vector3& pos, uint32 color, float intensity, WorldModelLight::LightType type) 
			{
				CreateLight(m_selectedGroupIndex, pos, color, intensity, type);
			};
			lightsCallbacks.onSelectLight = [this](int32 index) 
			{
				m_selectedLightIndex = index;
				UpdateSelectedLightVisualization();
				
				m_selection.Clear();
				if (m_selectedLightIndex >= 0 && m_selectedLightIndex < static_cast<int32>(m_lightVisualizations.size()) &&
					m_lightVisualizations[m_selectedLightIndex].node)
				{
					m_selection.AddSelectable(std::make_unique<SelectedWorldModelLight>(
						*m_worldModel, static_cast<size_t>(m_selectedLightIndex), *m_lightVisualizations[m_selectedLightIndex].node));
				}
			};
			lightsCallbacks.onRemoveLight = [this](int32 index) { RemoveLight(index); };
			lightsCallbacks.onLightChanged = [this]() { UpdateLightVisualizations(); };
			lightsCallbacks.onFocusPosition = [this](const Vector3& pos) 
			{
				m_cameraAnchor->SetPosition(pos);
				m_cameraVelocity = Vector3::Zero;
			};
			lightsCallbacks.onDuplicateLight = [this](size_t index) 
			{
				auto& lights = m_worldModel->GetLights();
				if (index < lights.size())
				{
					WorldModelLight newLight = lights[index];
					newLight.position += Vector3(1.0f, 0.0f, 0.0f);
					lights.push_back(newLight);
					UpdateLightVisualizations();
				}
			};

			DrawLightsPanel(m_worldModel.get(), lightsState, lightsCallbacks);
			m_selectedLightIndex = lightsState.selectedLightIndex;
		}
		ImGui::End();

		// Details/Properties panel
		if (ImGui::Begin(detailsId.c_str()))
		{
			PropertiesPanelCallbacks propCallbacks;
			propCallbacks.onSave = [this]() 
			{
				Save();
			};
			propCallbacks.onUpdateGroupVisualizations = [this]() 
			{
				UpdateGroupVisualizations();
			};

			DrawPropertiesPanel(m_worldModel.get(), m_selectedGroupIndex, propCallbacks);
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
					
					// Also sync to scene for deferred renderer
					m_scene.SetAmbientColor(Vector3(color[0], color[1], color[2]));
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
				if (ImGui::Checkbox("Show Lights", &m_showLights))
				{
					UpdateLightMarkerVisibility();
				}
				if (m_showLights)
				{
					ImGui::Indent();
					if (ImGui::Checkbox("Show Light Markers", &m_showLightMarkers))
					{
						UpdateLightMarkerVisibility();
					}
					ImGui::Unindent();
				}
				ImGui::Checkbox("Show Doodads", &m_showDoodads);
				
				ImGui::Separator();
				if (ImGui::Checkbox("Preview Portal Culling", &m_previewPortalCulling))
				{
					if (!m_previewPortalCulling)
					{
						// Show all groups when disabling preview
						for (auto& groupVis : m_groupVisualizations)
						{
							groupVis.visible = true;
							if (groupVis.meshEntity)
							{
								groupVis.meshEntity->SetVisible(true);
							}
							for (auto& meshRefVis : groupVis.meshRefVisualizations)
							{
								if (meshRefVis.entity)
								{
									meshRefVis.entity->SetVisible(true);
								}
							}
						}
						m_lastCameraGroupIndex = -1;
						m_freezePortalCulling = false;
					}
				}
				if (m_previewPortalCulling)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("(Group: %d)", m_lastCameraGroupIndex);
					
					ImGui::Indent();
					ImGui::Checkbox("Freeze Culling", &m_freezePortalCulling);
					if (m_freezePortalCulling)
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(Frozen)");
					}
					
					// Show debug info
					if (!m_portalCullingDebugInfo.empty())
					{
						ImGui::TextWrapped("%s", m_portalCullingDebugInfo.c_str());
					}
					ImGui::Unindent();
				}
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
			
			if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
			{
				if (m_deferredRenderer && availableSpace.x > 0 && availableSpace.y > 0)
				{
					m_deferredRenderer->Resize(static_cast<uint32>(availableSpace.x), static_cast<uint32>(availableSpace.y));
				}
				m_lastAvailViewportSize = availableSpace;
			}

			// Render the deferred renderer's output into the window as image object
			if (m_deferredRenderer && m_deferredRenderer->GetFinalRenderTarget())
			{
				ImGui::Image(m_deferredRenderer->GetFinalRenderTarget()->GetTextureObject(), availableSpace);
			};
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
			
			// Split left dock into upper and lower sections for better organization
			ImGuiID leftUpperId, leftLowerId;
			leftLowerId = ImGui::DockBuilderSplitNode(leftId, ImGuiDir_Down, 0.45f, nullptr, &leftUpperId);

			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(detailsId.c_str(), rightId);
			ImGui::DockBuilderDockWindow(worldSettingsId.c_str(), rightId);
			
			// Upper left: Groups, Portals, Doodads, Lights
			ImGui::DockBuilderDockWindow(groupsId.c_str(), leftUpperId);
			ImGui::DockBuilderDockWindow(portalsId.c_str(), leftUpperId);
			ImGui::DockBuilderDockWindow(doodadsId.c_str(), leftUpperId);
			ImGui::DockBuilderDockWindow(lightsId.c_str(), leftUpperId);
			
			// Lower left: Mesh refs and Child WMOs (separate dock group)
			ImGui::DockBuilderDockWindow(meshRefsId.c_str(), leftLowerId);
			ImGui::DockBuilderDockWindow(childWMOsId.c_str(), leftLowerId);
			
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

	String WorldModelEditorInstance::GenerateUniqueMeshRefName(const WorldModelGroup& group, const String& baseName)
	{
		const auto& meshRefs = group.GetMeshRefs();
		
		// Check if base name is already unique
		bool baseExists = false;
		for (const auto& ref : meshRefs)
		{
			if (ref.name == baseName)
			{
				baseExists = true;
				break;
			}
		}
		
		if (!baseExists)
		{
			return baseName;
		}
		
		// Find the highest existing suffix number
		int maxSuffix = 0;
		String baseNameLower = baseName;
		std::transform(baseNameLower.begin(), baseNameLower.end(), baseNameLower.begin(), ::tolower);
		
		for (const auto& ref : meshRefs)
		{
			String refName = ref.name;
			String refNameLower = refName;
			std::transform(refNameLower.begin(), refNameLower.end(), refNameLower.begin(), ::tolower);
			
			// Check if this name starts with our base name
			if (refNameLower.find(baseNameLower) == 0)
			{
				// Check for numeric suffix pattern: "baseName_N" or "baseName (N)"
				if (refName.length() > baseName.length())
				{
					String suffix = refName.substr(baseName.length());
					
					// Try "_N" format
					if (suffix.length() > 1 && suffix[0] == '_')
					{
						try
						{
							int num = std::stoi(suffix.substr(1));
							maxSuffix = std::max(maxSuffix, num);
						}
						catch (...) {}
					}
					// Try " (N)" format  
					else if (suffix.length() > 3 && suffix.substr(0, 2) == " (")
					{
						size_t endParen = suffix.find(')');
						if (endParen != String::npos)
						{
							try
							{
								int num = std::stoi(suffix.substr(2, endParen - 2));
								maxSuffix = std::max(maxSuffix, num);
							}
							catch (...) {}
						}
					}
				}
				else if (refNameLower == baseNameLower)
				{
					// Exact match counts as suffix 0
					maxSuffix = std::max(maxSuffix, 0);
				}
			}
		}
		
		return baseName + "_" + std::to_string(maxSuffix + 1);
	}

	String WorldModelEditorInstance::GenerateUniqueChildWMOName(const String& baseName)
	{
		if (!m_worldModel)
		{
			return baseName;
		}

		const auto& childRefs = m_worldModel->GetChildRefs();
		
		// Check if base name is already unique
		bool baseExists = false;
		for (const auto& ref : childRefs)
		{
			if (ref.name == baseName)
			{
				baseExists = true;
				break;
			}
		}
		
		if (!baseExists)
		{
			return baseName;
		}
		
		// Find the highest existing suffix number
		int maxSuffix = 0;
		String baseNameLower = baseName;
		std::transform(baseNameLower.begin(), baseNameLower.end(), baseNameLower.begin(), ::tolower);
		
		for (const auto& ref : childRefs)
		{
			String refName = ref.name;
			String refNameLower = refName;
			std::transform(refNameLower.begin(), refNameLower.end(), refNameLower.begin(), ::tolower);
			
			if (refNameLower.find(baseNameLower) == 0)
			{
				if (refName.length() > baseName.length())
				{
					String suffix = refName.substr(baseName.length());
					
					if (suffix.length() > 1 && suffix[0] == '_')
					{
						try
						{
							int num = std::stoi(suffix.substr(1));
							maxSuffix = std::max(maxSuffix, num);
						}
						catch (...) {}
					}
					else if (suffix.length() > 3 && suffix.substr(0, 2) == " (")
					{
						size_t endParen = suffix.find(')');
						if (endParen != String::npos)
						{
							try
							{
								int num = std::stoi(suffix.substr(2, endParen - 2));
								maxSuffix = std::max(maxSuffix, num);
							}
							catch (...) {}
						}
					}
				}
				else if (refNameLower == baseNameLower)
				{
					maxSuffix = std::max(maxSuffix, 0);
				}
			}
		}
		
		return baseName + "_" + std::to_string(maxSuffix + 1);
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
		
		// Generate a unique name
		String baseName;
		if (name.empty())
		{
			// Generate a base name from the mesh path
			std::filesystem::path path(meshPath);
			baseName = path.stem().string();
		}
		else
		{
			baseName = name;
		}
		meshRef.name = GenerateUniqueMeshRefName(*group, baseName);

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

		// Clear multi-selection
		m_selectedMeshRefIndices.clear();

		UpdateMeshRefVisualizations(groupIndex);
	}

	void WorldModelEditorInstance::RemoveMultipleMeshRefsFromGroup(int32 groupIndex, const std::set<size_t>& meshRefIndices)
	{
		if (!m_worldModel || groupIndex < 0 || groupIndex >= static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			ELOG("Invalid group index for removing mesh references");
			return;
		}

		auto* group = m_worldModel->GetGroup(groupIndex);
		if (!group)
		{
			ELOG("Failed to get group at index " << groupIndex);
			return;
		}

		if (meshRefIndices.empty())
		{
			return;
		}

		ILOG("Removing " << meshRefIndices.size() << " mesh references from group '" << group->GetName() << "'");

		// Remove in reverse order to preserve indices
		std::vector<size_t> sortedIndices(meshRefIndices.begin(), meshRefIndices.end());
		std::sort(sortedIndices.rbegin(), sortedIndices.rend());

		for (size_t idx : sortedIndices)
		{
			if (idx < group->GetMeshRefs().size())
			{
				group->RemoveMeshRef(idx);
			}
		}

		// Clear selections
		m_selection.Clear();
		m_selectedMeshRefIndex = -1;
		m_selectedMeshRefIndices.clear();

		UpdateMeshRefVisualizations(groupIndex);
	}

	void WorldModelEditorInstance::MoveMeshRefsToGroup(int32 sourceGroupIndex, int32 targetGroupIndex, const std::set<size_t>& meshRefIndices)
	{
		if (!m_worldModel)
		{
			ELOG("No world model for moving mesh references");
			return;
		}

		if (sourceGroupIndex < 0 || sourceGroupIndex >= static_cast<int32>(m_worldModel->GetGroupCount()) ||
			targetGroupIndex < 0 || targetGroupIndex >= static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			ELOG("Invalid group indices for moving mesh references");
			return;
		}

		if (sourceGroupIndex == targetGroupIndex)
		{
			return;
		}

		auto* sourceGroup = m_worldModel->GetGroup(sourceGroupIndex);
		auto* targetGroup = m_worldModel->GetGroup(targetGroupIndex);
		if (!sourceGroup || !targetGroup)
		{
			ELOG("Failed to get source or target group");
			return;
		}

		if (meshRefIndices.empty())
		{
			return;
		}

		ILOG("Moving " << meshRefIndices.size() << " mesh references from group '" << sourceGroup->GetName() 
			<< "' to group '" << targetGroup->GetName() << "'");

		// Collect mesh refs to move (in sorted order to ensure consistent behavior)
		std::vector<WorldModelMeshRef> refsToMove;
		std::vector<size_t> sortedIndices(meshRefIndices.begin(), meshRefIndices.end());
		std::sort(sortedIndices.begin(), sortedIndices.end());

		for (size_t idx : sortedIndices)
		{
			if (idx < sourceGroup->GetMeshRefs().size())
			{
				WorldModelMeshRef ref = sourceGroup->GetMeshRefs()[idx];
				// Generate unique name in target group
				ref.name = GenerateUniqueMeshRefName(*targetGroup, ref.name.empty() ? 
					ref.meshPath.substr(ref.meshPath.find_last_of("/\\") + 1) : ref.name);
				refsToMove.push_back(ref);
			}
		}

		// Add to target group
		for (const auto& ref : refsToMove)
		{
			targetGroup->AddMeshRef(ref);
		}

		// Remove from source group (in reverse order to preserve indices)
		std::sort(sortedIndices.rbegin(), sortedIndices.rend());
		for (size_t idx : sortedIndices)
		{
			if (idx < sourceGroup->GetMeshRefs().size())
			{
				sourceGroup->RemoveMeshRef(idx);
			}
		}

		// Clear selections
		m_selection.Clear();
		m_selectedMeshRefIndex = -1;
		m_selectedMeshRefIndices.clear();

		// Update visualizations for both groups
		UpdateMeshRefVisualizations(sourceGroupIndex);
		UpdateMeshRefVisualizations(targetGroupIndex);
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

		// Generate a unique name
		String baseName;
		if (name.empty())
		{
			// Generate a base name from the WMO path
			std::filesystem::path path(wmoPath);
			baseName = path.stem().string();
		}
		else
		{
			baseName = name;
		}
		childRef.name = GenerateUniqueChildWMOName(baseName);

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

			// Clear existing mesh ref visualizations
			for (auto& meshRefViz : vis.meshRefVisualizations)
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
			vis.meshRefVisualizations.clear();
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
			if (vis.rangeRenderable)
			{
				m_scene.DestroyManualRenderObject(*vis.rangeRenderable);
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
		
		// Update range visualization for selected light
		UpdateSelectedLightVisualization();
	}

	void WorldModelEditorInstance::UpdateSelectedLightVisualization()
	{
		// Clear existing range visualizations
		for (auto& vis : m_lightVisualizations)
		{
			if (vis.rangeRenderable)
			{
				m_scene.DestroyManualRenderObject(*vis.rangeRenderable);
				vis.rangeRenderable = nullptr;
			}
		}

		// Only create range visualization for selected light
		if (m_selectedLightIndex < 0 || m_selectedLightIndex >= static_cast<int32>(m_lightVisualizations.size()))
		{
			return;
		}

		if (!m_worldModel)
		{
			return;
		}

		const auto& lights = m_worldModel->GetLights();
		if (m_selectedLightIndex >= static_cast<int32>(lights.size()))
		{
			return;
		}

		const auto& lightData = lights[m_selectedLightIndex];
		auto& vis = m_lightVisualizations[m_selectedLightIndex];

		vis.rangeRenderable = m_scene.CreateManualRenderObject("LightRange_" + std::to_string(m_selectedLightIndex));
		vis.rangeRenderable->SetQueryFlags(0);

		// Get light color for the range visualization
		const uint32 lightColor = 
			0xFF000000 |
			((lightData.color >> 16) & 0xFF) |
			(lightData.color & 0xFF00) |
			((lightData.color & 0xFF) << 16);

		const int segments = 32;

		if (lightData.type == WorldModelLight::LightType::Omni)
		{
			// Point light - draw 3 circles (one for each axis) at the light's range
			const float range = lightData.attenuationEnd;
			auto lineOp = vis.rangeRenderable->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/WorldGrid.hmat"));

			// XY plane circle (around Z axis)
			for (int i = 0; i < segments; ++i)
			{
				const float angle1 = (static_cast<float>(i) / segments) * 2.0f * 3.14159265f;
				const float angle2 = (static_cast<float>(i + 1) / segments) * 2.0f * 3.14159265f;
				Vector3 p1(std::cos(angle1) * range, std::sin(angle1) * range, 0.0f);
				Vector3 p2(std::cos(angle2) * range, std::sin(angle2) * range, 0.0f);
				auto& line = lineOp->AddLine(p1, p2);
				line.SetColor(lightColor);
			}

			// XZ plane circle (around Y axis)
			for (int i = 0; i < segments; ++i)
			{
				const float angle1 = (static_cast<float>(i) / segments) * 2.0f * 3.14159265f;
				const float angle2 = (static_cast<float>(i + 1) / segments) * 2.0f * 3.14159265f;
				Vector3 p1(std::cos(angle1) * range, 0.0f, std::sin(angle1) * range);
				Vector3 p2(std::cos(angle2) * range, 0.0f, std::sin(angle2) * range);
				auto& line = lineOp->AddLine(p1, p2);
				line.SetColor(lightColor);
			}

			// YZ plane circle (around X axis)
			for (int i = 0; i < segments; ++i)
			{
				const float angle1 = (static_cast<float>(i) / segments) * 2.0f * 3.14159265f;
				const float angle2 = (static_cast<float>(i + 1) / segments) * 2.0f * 3.14159265f;
				Vector3 p1(0.0f, std::cos(angle1) * range, std::sin(angle1) * range);
				Vector3 p2(0.0f, std::cos(angle2) * range, std::sin(angle2) * range);
				auto& line = lineOp->AddLine(p1, p2);
				line.SetColor(lightColor);
			}
		}
		else if (lightData.type == WorldModelLight::LightType::Spot)
		{
			// Spot light - draw outer cone
			const float range = lightData.attenuationEnd;
			const float outerAngle = 45.0f; // Default outer angle in degrees
			const float outerRadius = range * std::tan(outerAngle * 0.5f * 3.14159265f / 180.0f);
			
			auto lineOp = vis.rangeRenderable->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/WorldGrid.hmat"));

			// Draw cone edges (4 lines from apex to base circle)
			for (int i = 0; i < 4; ++i)
			{
				const float angle = (static_cast<float>(i) / 4) * 2.0f * 3.14159265f;
				Vector3 basePoint(std::cos(angle) * outerRadius, std::sin(angle) * outerRadius, -range);
				auto& line = lineOp->AddLine(Vector3::Zero, basePoint);
				line.SetColor(lightColor);
			}

			// Draw base circle
			for (int i = 0; i < segments; ++i)
			{
				const float angle1 = (static_cast<float>(i) / segments) * 2.0f * 3.14159265f;
				const float angle2 = (static_cast<float>(i + 1) / segments) * 2.0f * 3.14159265f;
				Vector3 p1(std::cos(angle1) * outerRadius, std::sin(angle1) * outerRadius, -range);
				Vector3 p2(std::cos(angle2) * outerRadius, std::sin(angle2) * outerRadius, -range);
				auto& line = lineOp->AddLine(p1, p2);
				line.SetColor(lightColor);
			}
		}

		vis.node->AttachObject(*vis.rangeRenderable);
	}

	void WorldModelEditorInstance::UpdateLightMarkerVisibility()
	{
		for (auto& vis : m_lightVisualizations)
		{
			if (vis.iconEntity)
			{
				vis.iconEntity->SetVisible(m_showLights && m_showLightMarkers);
			}
		}
	}

	void WorldModelEditorInstance::UpdatePortalCullingPreview()
	{
		if (!m_worldModel || !m_camera)
		{
			return;
		}

		// If culling is frozen, don't update visibility
		if (m_freezePortalCulling)
		{
			return;
		}

		// Determine which group the camera is currently in
		Vector3 cameraPos = m_camera->GetDerivedPosition();
		int32 currentGroupIndex = -1;
		float smallestVolume = std::numeric_limits<float>::max();

		// Check each group's bounding box to find which one contains the camera
		// If multiple groups contain the camera, prefer the one with the smallest
		// bounding box volume (most specific/inner group)
		for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
		{
			const auto* group = m_worldModel->GetGroup(i);
			if (group && group->GetBoundingBox().Intersects(cameraPos))
			{
				const AABB& bbox = group->GetBoundingBox();
				Vector3 size = bbox.max - bbox.min;
				float volume = size.x * size.y * size.z;
				
				if (volume < smallestVolume)
				{
					smallestVolume = volume;
					currentGroupIndex = static_cast<int32>(i);
				}
			}
		}

		m_lastCameraGroupIndex = currentGroupIndex;

		// Build debug info
		std::ostringstream debugStream;
		debugStream << "Groups: " << m_worldModel->GetGroupCount() << ", Portals: " << m_worldModel->GetPortals().size() << "\n";

		// Determine which groups are visible through portal culling
		std::vector<int32> visibleGroups;
		std::vector<bool> visitedGroups(m_worldModel->GetGroupCount(), false);
		
		if (currentGroupIndex < 0)
		{
			// Camera is not in any group - show all exterior groups and any
			// interior groups whose bounding box is visible in the camera frustum
			std::vector<int32> groupsToProcess;
			
			int exteriorCount = 0;
			int visibleBBoxCount = 0;
			for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
			{
				const auto* group = m_worldModel->GetGroup(i);
				if (group)
				{
					// Always include exterior groups
					if (group->IsExterior())
					{
						exteriorCount++;
						visibleGroups.push_back(static_cast<int32>(i));
						visitedGroups[i] = true;
						groupsToProcess.push_back(static_cast<int32>(i));
					}
					// Also include any group whose bounding box is visible in the frustum
					else if (m_camera->IsVisible(group->GetBoundingBox()))
					{
						visibleBBoxCount++;
						visibleGroups.push_back(static_cast<int32>(i));
						visitedGroups[i] = true;
						groupsToProcess.push_back(static_cast<int32>(i));
					}
				}
			}
			debugStream << "Outside: " << exteriorCount << " exterior, " << visibleBBoxCount << " bbox visible\n";
			
			// Now traverse through portals from visible groups to find more visible groups
			while (!groupsToProcess.empty())
			{
				int32 groupIndex = groupsToProcess.back();
				groupsToProcess.pop_back();

				if (groupIndex < 0 || groupIndex >= static_cast<int32>(visitedGroups.size()))
				{
					continue;
				}

				// Get connected groups through portals
				const auto* group = m_worldModel->GetGroup(groupIndex);
				if (!group)
				{
					continue;
				}

				for (const auto& portalRef : group->GetPortalRefs())
				{
					int32 targetGroup = portalRef.groupIndex;
					if (targetGroup >= 0 && targetGroup < static_cast<int32>(visitedGroups.size()) && !visitedGroups[targetGroup])
					{
						// Check if the portal is active
						if (portalRef.portalIndex < m_worldModel->GetPortals().size())
						{
							const auto& portal = m_worldModel->GetPortals()[portalRef.portalIndex];
							if (portal && portal->IsActive())
							{
								// For editor preview, show connected groups if the portal is visible
								// OR if the target group's bounding box is visible
								AABB portalBBox = portal->GetWorldBounds();
								const auto* targetGroupPtr = m_worldModel->GetGroup(targetGroup);
								
								bool portalVisible = !portalBBox.IsNull() && m_camera->IsVisible(portalBBox);
								bool targetVisible = targetGroupPtr && m_camera->IsVisible(targetGroupPtr->GetBoundingBox());
								
								if (portalVisible || targetVisible)
								{
									visitedGroups[targetGroup] = true;
									visibleGroups.push_back(targetGroup);
									groupsToProcess.push_back(targetGroup);
								}
							}
						}
					}
				}
			}
		}
		else
		{
			// Perform portal-based visibility culling from inside a group
			std::vector<int32> groupsToProcess;
			groupsToProcess.push_back(currentGroupIndex);
			
			const auto* currentGroup = m_worldModel->GetGroup(currentGroupIndex);
			if (currentGroup)
			{
				debugStream << "Inside group " << currentGroupIndex << ": " << currentGroup->GetPortalRefs().size() << " portal refs\\n";
			}

			while (!groupsToProcess.empty())
			{
				int32 groupIndex = groupsToProcess.back();
				groupsToProcess.pop_back();

				if (groupIndex < 0 || groupIndex >= static_cast<int32>(visitedGroups.size()))
				{
					continue;
				}

				if (visitedGroups[groupIndex])
				{
					continue;
				}

				visitedGroups[groupIndex] = true;
				visibleGroups.push_back(groupIndex);

				// Get connected groups through portals
				const auto* group = m_worldModel->GetGroup(groupIndex);
				if (!group)
				{
					continue;
				}

				for (const auto& portalRef : group->GetPortalRefs())
				{
					int32 targetGroup = portalRef.groupIndex;
					if (targetGroup >= 0 && targetGroup < static_cast<int32>(visitedGroups.size()) && !visitedGroups[targetGroup])
					{
						// Check if the portal is active
						if (portalRef.portalIndex < m_worldModel->GetPortals().size())
						{
							const auto& portal = m_worldModel->GetPortals()[portalRef.portalIndex];
							if (portal && portal->IsActive())
							{
								// When inside a group, only check portal visibility
								// The target group is only visible if we can see through the portal
								AABB portalBBox = portal->GetWorldBounds();
								bool portalVisible = !portalBBox.IsNull() && m_camera->IsVisible(portalBBox);
								
								debugStream << "  Portal " << portalRef.portalIndex << " -> grp " << targetGroup 
								           << " (pVis:" << portalVisible << ")\\n";
								
								if (portalVisible)
								{
									groupsToProcess.push_back(targetGroup);
								}
							}
							else
							{
								debugStream << "  Portal " << portalRef.portalIndex << " inactive\\n";
							}
						}
						else
						{
							debugStream << "  Invalid portal index: " << portalRef.portalIndex << "\\n";
						}
					}
				}
			}
		}

		debugStream << "Visible: " << visibleGroups.size() << "/" << m_worldModel->GetGroupCount() << " groups";
		m_portalCullingDebugInfo = debugStream.str();

		// Update visibility of all groups
		for (size_t i = 0; i < m_groupVisualizations.size(); ++i)
		{
			bool isVisible = std::find(visibleGroups.begin(), visibleGroups.end(), static_cast<int32>(i)) != visibleGroups.end();
			
			auto& groupVis = m_groupVisualizations[i];
			groupVis.visible = isVisible;

			if (groupVis.meshEntity)
			{
				groupVis.meshEntity->SetVisible(isVisible);
			}

			for (auto& meshRefVis : groupVis.meshRefVisualizations)
			{
				if (meshRefVis.entity)
				{
					meshRefVis.entity->SetVisible(isVisible);
				}
			}
		}
	}

	void WorldModelEditorInstance::DrawMeshRefsPanel(int32 groupIndex)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));

		if (!m_worldModel || groupIndex < 0 || groupIndex >= static_cast<int32>(m_worldModel->GetGroupCount()))
		{
			ImGui::TextDisabled("Select a group to manage meshes");
			ImGui::PopStyleVar(2);
			return;
		}

		auto* group = m_worldModel->GetGroup(groupIndex);
		if (!group)
		{
			ImGui::PopStyleVar(2);
			return;
		}

		// Header with group name and mesh count
		const auto& meshRefs = group->GetMeshRefs();
		ImGui::Text("Group: %s", group->GetName().empty() ? "(unnamed)" : group->GetName().c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("(%zu meshes)", meshRefs.size());

		ImGui::Spacing();

		// Action buttons
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 0.8f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.4f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.8f, 0.5f, 1.0f));
		if (ImGui::Button("+ Add Mesh", ImVec2(-1, 0)))
		{
			m_showAddMeshRefDialog = true;
			std::memset(m_addMeshRefPath, 0, sizeof(m_addMeshRefPath));
			std::memset(m_addMeshRefName, 0, sizeof(m_addMeshRefName));
		}
		ImGui::PopStyleColor(3);

		// Drag-drop hint
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.18f, 0.5f));
		ImGui::BeginChild("DragDropHint", ImVec2(-1, 24), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Drop .hmsh files here to add");
		ImGui::EndChild();
		ImGui::PopStyleColor();
		
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
			{
				const String meshPath = *static_cast<String*>(payload->Data);
				AddMeshRefToGroup(groupIndex, meshPath);
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Search filter
		ImGui::Text("Search:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		ImGui::InputTextWithHint("##meshSearch", "Filter meshes...", m_meshRefSearchFilter, sizeof(m_meshRefSearchFilter));
		ImGui::PopStyleColor();

		ImGui::Spacing();

		// Mesh list with collapsible items
		if (ImGui::CollapsingHeader("Mesh List", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			if (meshRefs.empty())
			{
				ImGui::TextDisabled("No meshes in this group");
			}
			else
			{
				// Convert search filter to lowercase for case-insensitive matching
				String searchLower = m_meshRefSearchFilter;
				std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

				int visibleCount = 0;
				for (size_t i = 0; i < meshRefs.size(); ++i)
				{
					const auto& meshRef = meshRefs[i];

					// Get display label
					String label = meshRef.name;
					if (label.empty())
					{
						label = meshRef.meshPath;
						size_t lastSlash = label.find_last_of("/\\");
						if (lastSlash != String::npos)
						{
							label = label.substr(lastSlash + 1);
						}
					}

					// Apply search filter
					if (!searchLower.empty())
					{
						String labelLower = label;
						std::transform(labelLower.begin(), labelLower.end(), labelLower.begin(), ::tolower);
						String pathLower = meshRef.meshPath;
						std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);

						if (labelLower.find(searchLower) == String::npos && 
							pathLower.find(searchLower) == String::npos)
						{
							continue;
						}
					}

					visibleCount++;
					ImGui::PushID(static_cast<int>(i));

					bool isSelected = m_selectedMeshRefIndices.count(i) > 0;

					// Alternating row colors for better readability
					if (visibleCount % 2 == 0)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.2f, 1.0f));
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));
					}

					// Visibility checkbox first
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
						ImGui::SameLine();
					}

					// Selectable item with multi-select support
					if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
					{
						const bool ctrlPressed = ImGui::GetIO().KeyCtrl;
						const bool shiftPressed = ImGui::GetIO().KeyShift;

						if (ctrlPressed)
						{
							// Toggle selection of this item
							if (m_selectedMeshRefIndices.count(i) > 0)
							{
								m_selectedMeshRefIndices.erase(i);
							}
							else
							{
								m_selectedMeshRefIndices.insert(i);
							}
						}
						else if (shiftPressed && m_selectedMeshRefIndex >= 0)
						{
							// Range selection from last selected to current
							size_t start = static_cast<size_t>(m_selectedMeshRefIndex);
							size_t end = i;
							if (start > end)
							{
								std::swap(start, end);
							}
							for (size_t j = start; j <= end; ++j)
							{
								m_selectedMeshRefIndices.insert(j);
							}
						}
						else
						{
							// Single selection - clear previous and select only this
							m_selectedMeshRefIndices.clear();
							m_selectedMeshRefIndices.insert(i);
						}

						m_selectedMeshRefIndex = static_cast<int32>(i);
						m_selection.Clear();
						
						if (static_cast<size_t>(groupIndex) < m_groupVisualizations.size())
						{
							auto& groupViz = m_groupVisualizations[groupIndex];
							if (i < groupViz.meshRefVisualizations.size() && groupViz.meshRefVisualizations[i].node)
							{
								m_selection.AddSelectable(std::make_unique<SelectedGroupMesh>(*group, i, *groupViz.meshRefVisualizations[i].node));
							}
						}
					}

					ImGui::PopStyleColor();

					// Context menu
					if (ImGui::BeginPopupContextItem())
					{
						const bool hasMultiSelection = m_selectedMeshRefIndices.size() > 1;
						
						if (hasMultiSelection)
						{
							ImGui::TextDisabled("(%zu items selected)", m_selectedMeshRefIndices.size());
							ImGui::Separator();
						}

						if (ImGui::MenuItem("Focus (F)"))
						{
							m_cameraAnchor->SetPosition(meshRef.position);
							m_cameraVelocity = Vector3::Zero;
						}
						ImGui::Separator();

						// Move to group submenu (only for multi-group WMOs)
						if (m_worldModel->GetGroupCount() > 1)
						{
							if (ImGui::BeginMenu(hasMultiSelection ? "Move Selected to Group" : "Move to Group"))
							{
								for (size_t gi = 0; gi < m_worldModel->GetGroupCount(); ++gi)
								{
									if (static_cast<int32>(gi) == groupIndex)
									{
										continue; // Skip current group
									}
									
									auto* targetGroup = m_worldModel->GetGroup(static_cast<int32>(gi));
									if (targetGroup)
									{
										String groupLabel = targetGroup->GetName().empty() ? 
											"Group " + std::to_string(gi) : targetGroup->GetName();
										if (ImGui::MenuItem(groupLabel.c_str()))
										{
											if (hasMultiSelection)
											{
												MoveMeshRefsToGroup(groupIndex, static_cast<int32>(gi), m_selectedMeshRefIndices);
											}
											else
											{
												std::set<size_t> singleSelection;
												singleSelection.insert(i);
												MoveMeshRefsToGroup(groupIndex, static_cast<int32>(gi), singleSelection);
											}
											ImGui::EndMenu();
											ImGui::EndPopup();
											ImGui::PopID();
											break;
										}
									}
								}
								ImGui::EndMenu();
							}
							ImGui::Separator();
						}

						if (!hasMultiSelection && ImGui::MenuItem("Duplicate"))
						{
							WorldModelMeshRef newRef = meshRef;
							newRef.name = GenerateUniqueMeshRefName(*group, meshRef.name.empty() ? label : meshRef.name);
							newRef.position += Vector3(1.0f, 0.0f, 0.0f);
							group->AddMeshRef(newRef);
							UpdateMeshRefVisualizations(groupIndex);
						}
						ImGui::Separator();
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
						if (hasMultiSelection)
						{
							if (ImGui::MenuItem("Delete Selected"))
							{
								RemoveMultipleMeshRefsFromGroup(groupIndex, m_selectedMeshRefIndices);
								ImGui::PopStyleColor();
								ImGui::EndPopup();
								ImGui::PopID();
								break;
							}
						}
						else
						{
							if (ImGui::MenuItem("Delete"))
							{
								RemoveMeshRefFromGroup(groupIndex, i);
								ImGui::PopStyleColor();
								ImGui::EndPopup();
								ImGui::PopID();
								break;
							}
						}
						ImGui::PopStyleColor();
						ImGui::EndPopup();
					}

					// Tooltip with full path
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Path: %s", meshRef.meshPath.c_str());
						ImGui::Text("Position: %.2f, %.2f, %.2f", meshRef.position.x, meshRef.position.y, meshRef.position.z);
						ImGui::EndTooltip();
					}

					ImGui::PopID();
				}

				if (visibleCount == 0 && !searchLower.empty())
				{
					ImGui::TextDisabled("No meshes match '%s'", m_meshRefSearchFilter);
				}
			}

			ImGui::Unindent();
		}

		// Multi-selection actions section
		if (m_selectedMeshRefIndices.size() > 1)
		{
			ImGui::Spacing();

			if (ImGui::CollapsingHeader("Bulk Actions", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent();

				ImGui::TextDisabled("%zu meshes selected", m_selectedMeshRefIndices.size());
				ImGui::Spacing();

				// Select All / Deselect All buttons
				if (ImGui::Button("Select All", ImVec2(100, 0)))
				{
					m_selectedMeshRefIndices.clear();
					for (size_t idx = 0; idx < meshRefs.size(); ++idx)
					{
						m_selectedMeshRefIndices.insert(idx);
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Deselect All", ImVec2(100, 0)))
				{
					m_selectedMeshRefIndices.clear();
					m_selectedMeshRefIndex = -1;
				}

				ImGui::Spacing();

				// Move to group combo
				if (m_worldModel->GetGroupCount() > 1)
				{
					ImGui::Text("Move to Group:");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(150);
					if (ImGui::BeginCombo("##moveTargetGroup", "Select group..."))
					{
						for (size_t gi = 0; gi < m_worldModel->GetGroupCount(); ++gi)
						{
							if (static_cast<int32>(gi) == groupIndex)
							{
								continue;
							}

							auto* targetGroup = m_worldModel->GetGroup(static_cast<int32>(gi));
							if (targetGroup)
							{
								String groupLabel = targetGroup->GetName().empty() ? 
									"Group " + std::to_string(gi) : targetGroup->GetName();
								if (ImGui::Selectable(groupLabel.c_str()))
								{
									MoveMeshRefsToGroup(groupIndex, static_cast<int32>(gi), m_selectedMeshRefIndices);
								}
							}
						}
						ImGui::EndCombo();
					}

					ImGui::Spacing();
				}

				// Delete selected button
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
				if (ImGui::Button("Delete Selected", ImVec2(-1, 0)))
				{
					RemoveMultipleMeshRefsFromGroup(groupIndex, m_selectedMeshRefIndices);
				}
				ImGui::PopStyleColor(3);

				ImGui::Unindent();
			}
		}

		// Selected mesh properties section (only show for single selection)
		if (m_selectedMeshRefIndices.size() == 1 && m_selectedMeshRefIndex >= 0 && m_selectedMeshRefIndex < static_cast<int32>(meshRefs.size()))
		{
			ImGui::Spacing();

			if (ImGui::CollapsingHeader("Selected Mesh Properties", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent();

				auto* selectedRef = group->GetMeshRef(m_selectedMeshRefIndex);
				if (selectedRef)
				{
					// Name field
					ImGui::Text("Name:");
					ImGui::SameLine();
					char nameBuffer[256];
					std::strncpy(nameBuffer, selectedRef->name.c_str(), sizeof(nameBuffer) - 1);
					nameBuffer[sizeof(nameBuffer) - 1] = '\0';
					ImGui::SetNextItemWidth(-1);
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
					if (ImGui::InputText("##meshrefName", nameBuffer, sizeof(nameBuffer)))
					{
						selectedRef->name = nameBuffer;
					}
					ImGui::PopStyleColor();

					ImGui::Spacing();

					// Transform section
					ImGui::Text("Transform");
					ImGui::Separator();

					// Position
					float pos[3] = { selectedRef->position.x, selectedRef->position.y, selectedRef->position.z };
					if (ImGui::DragFloat3("Position##meshref", pos, 0.1f))
					{
						selectedRef->position = Vector3(pos[0], pos[1], pos[2]);
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

					ImGui::Spacing();

					// Material section
					ImGui::Text("Material Override");
					ImGui::Separator();
					char matBuffer[512];
					std::strncpy(matBuffer, selectedRef->materialOverride.c_str(), sizeof(matBuffer) - 1);
					matBuffer[sizeof(matBuffer) - 1] = '\0';
					ImGui::SetNextItemWidth(-1);
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
					if (ImGui::InputText("##meshrefMat", matBuffer, sizeof(matBuffer)))
					{
						selectedRef->materialOverride = matBuffer;
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
					ImGui::PopStyleColor();

					ImGui::Spacing();

					// Source mesh (read-only)
					ImGui::TextDisabled("Source: %s", selectedRef->meshPath.c_str());

					ImGui::Spacing();

					// Action buttons
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.9f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
					if (ImGui::Button("Delete Selected", ImVec2(-1, 0)))
					{
						RemoveMeshRefFromGroup(groupIndex, m_selectedMeshRefIndex);
					}
					ImGui::PopStyleColor(3);
				}

				ImGui::Unindent();
			}
		}

		ImGui::PopStyleVar(2);

		// Add mesh dialog
		if (m_showAddMeshRefDialog)
		{
			ImGui::OpenPopup("Add Mesh Reference");
		}

		if (ImGui::BeginPopupModal("Add Mesh Reference", &m_showAddMeshRefDialog, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Mesh Path:");
			ImGui::SetNextItemWidth(350);
			ImGui::InputText("##addMeshPath", m_addMeshRefPath, sizeof(m_addMeshRefPath));

			ImGui::Spacing();

			ImGui::Text("Display Name (optional):");
			ImGui::SetNextItemWidth(350);
			ImGui::InputText("##addMeshName", m_addMeshRefName, sizeof(m_addMeshRefName));
			ImGui::TextDisabled("Leave empty to auto-generate from mesh filename");

			// Drag-drop target
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
				{
					std::string path(static_cast<const char*>(payload->Data), payload->DataSize - 1);
					std::strncpy(m_addMeshRefPath, path.c_str(), sizeof(m_addMeshRefPath) - 1);
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.4f, 0.9f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.8f, 0.5f, 1.0f));
			if (ImGui::Button("Add", ImVec2(120, 0)))
			{
				if (std::strlen(m_addMeshRefPath) > 0)
				{
					AddMeshRefToGroup(groupIndex, m_addMeshRefPath, Vector3::Zero, Quaternion::Identity, Vector3::UnitScale, m_addMeshRefName);
					m_showAddMeshRefDialog = false;
				}
			}
			ImGui::PopStyleColor(3);

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
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));

		if (!m_worldModel)
		{
			ImGui::TextDisabled("No world model loaded");
			ImGui::PopStyleVar(2);
			return;
		}

		// Header with count
		const auto& childRefs = m_worldModel->GetChildRefs();
		ImGui::Text("Child WMO References");
		ImGui::SameLine();
		ImGui::TextDisabled("(%zu)", childRefs.size());

		ImGui::Spacing();

		// Add child WMO button
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 0.8f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.4f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.8f, 0.5f, 1.0f));
		if (ImGui::Button("+ Add Child WMO", ImVec2(-1, 0)))
		{
			m_showAddChildWMODialog = true;
			std::memset(m_addChildWMOPath, 0, sizeof(m_addChildWMOPath));
			std::memset(m_addChildWMOName, 0, sizeof(m_addChildWMOName));
		}
		ImGui::PopStyleColor(3);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Search filter
		ImGui::Text("Search:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
		ImGui::InputTextWithHint("##childWmoSearch", "Filter child WMOs...", m_childWMOSearchFilter, sizeof(m_childWMOSearchFilter));
		ImGui::PopStyleColor();

		ImGui::Spacing();

		// List child WMO references
		if (ImGui::CollapsingHeader("Child WMO List", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			if (childRefs.empty())
			{
				ImGui::TextDisabled("No child WMOs");
			}
			else
			{
				// Convert search filter to lowercase for case-insensitive matching
				String searchLower = m_childWMOSearchFilter;
				std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

				int visibleCount = 0;
				for (size_t i = 0; i < childRefs.size(); ++i)
				{
					const auto& childRef = childRefs[i];

					// Get display label
					String label = childRef.name;
					if (label.empty())
					{
						label = childRef.wmoPath;
						size_t lastSlash = label.find_last_of("/\\");
						if (lastSlash != String::npos)
						{
							label = label.substr(lastSlash + 1);
						}
					}

					// Apply search filter
					if (!searchLower.empty())
					{
						String labelLower = label;
						std::transform(labelLower.begin(), labelLower.end(), labelLower.begin(), ::tolower);
						String pathLower = childRef.wmoPath;
						std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);

						if (labelLower.find(searchLower) == String::npos && 
							pathLower.find(searchLower) == String::npos)
						{
							continue;
						}
					}

					visibleCount++;
					ImGui::PushID(static_cast<int>(i));

					bool isSelected = m_selectedChildWMOIndex == static_cast<int32>(i);

					// Alternating row colors
					if (visibleCount % 2 == 0)
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.2f, 1.0f));
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));
					}

					// Visibility checkbox first
					if (i < m_childWMOVisualizations.size())
					{
						bool visible = m_childWMOVisualizations[i].visible;
						if (ImGui::Checkbox("##vis", &visible))
						{
							m_childWMOVisualizations[i].visible = visible;
						}
						ImGui::SameLine();
					}

					// Selectable item
					if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
					{
						m_selectedChildWMOIndex = static_cast<int32>(i);
						m_selection.Clear();
						
						if (i < m_childWMOVisualizations.size() && m_childWMOVisualizations[i].node)
						{
							m_selection.AddSelectable(std::make_unique<SelectedChildWMO>(*m_worldModel, i, *m_childWMOVisualizations[i].node));
						}
					}

					ImGui::PopStyleColor();

					// Context menu
					if (ImGui::BeginPopupContextItem())
					{
						if (ImGui::MenuItem("Focus (F)"))
						{
							m_cameraAnchor->SetPosition(childRef.position);
							m_cameraVelocity = Vector3::Zero;
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Duplicate"))
						{
							WorldModelChildRef newRef = childRef;
							newRef.name = GenerateUniqueChildWMOName(childRef.name.empty() ? label : childRef.name);
							newRef.position += Vector3(5.0f, 0.0f, 0.0f);
							m_worldModel->AddChildRef(newRef);
							UpdateChildWMOVisualizations();
						}
						ImGui::Separator();
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
						if (ImGui::MenuItem("Delete"))
						{
							RemoveChildWMO(i);
							ImGui::PopStyleColor();
							ImGui::EndPopup();
							ImGui::PopID();
							break;
						}
						ImGui::PopStyleColor();
						ImGui::EndPopup();
					}

					// Tooltip with full path
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Path: %s", childRef.wmoPath.c_str());
						ImGui::Text("Position: %.2f, %.2f, %.2f", childRef.position.x, childRef.position.y, childRef.position.z);
						ImGui::EndTooltip();
					}

					ImGui::PopID();
				}

				if (visibleCount == 0 && !searchLower.empty())
				{
					ImGui::TextDisabled("No child WMOs match '%s'", m_childWMOSearchFilter);
				}
			}

			ImGui::Unindent();
		}

		// Selected child WMO properties
		if (m_selectedChildWMOIndex >= 0 && m_selectedChildWMOIndex < static_cast<int32>(childRefs.size()))
		{
			ImGui::Spacing();

			if (ImGui::CollapsingHeader("Selected Child WMO Properties", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent();

				auto* selectedRef = m_worldModel->GetChildRef(m_selectedChildWMOIndex);
				if (selectedRef)
				{
					// Name field
					ImGui::Text("Name:");
					ImGui::SameLine();
					char nameBuffer[256];
					std::strncpy(nameBuffer, selectedRef->name.c_str(), sizeof(nameBuffer) - 1);
					nameBuffer[sizeof(nameBuffer) - 1] = '\0';
					ImGui::SetNextItemWidth(-1);
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
					if (ImGui::InputText("##childWmoName", nameBuffer, sizeof(nameBuffer)))
					{
						selectedRef->name = nameBuffer;
					}
					ImGui::PopStyleColor();

					ImGui::Spacing();

					// Transform section
					ImGui::Text("Transform");
					ImGui::Separator();

					// Position
					float pos[3] = { selectedRef->position.x, selectedRef->position.y, selectedRef->position.z };
					if (ImGui::DragFloat3("Position##childwmo", pos, 0.1f))
					{
						selectedRef->position = Vector3(pos[0], pos[1], pos[2]);
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
						if (static_cast<size_t>(m_selectedChildWMOIndex) < m_childWMOVisualizations.size() &&
							m_childWMOVisualizations[m_selectedChildWMOIndex].node)
						{
							m_childWMOVisualizations[m_selectedChildWMOIndex].node->SetScale(selectedRef->scale);
						}
					}

					ImGui::Spacing();

					// Source path (read-only)
					ImGui::TextDisabled("Source: %s", selectedRef->wmoPath.c_str());

					ImGui::Spacing();

					// Action buttons
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.8f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.9f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
					if (ImGui::Button("Delete Selected", ImVec2(-1, 0)))
					{
						RemoveChildWMO(m_selectedChildWMOIndex);
					}
					ImGui::PopStyleColor(3);
				}

				ImGui::Unindent();
			}
		}

		ImGui::PopStyleVar(2);

		// Add child WMO dialog
		if (m_showAddChildWMODialog)
		{
			ImGui::OpenPopup("Add Child WMO");
		}

		if (ImGui::BeginPopupModal("Add Child WMO", &m_showAddChildWMODialog, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("WMO Path:");
			ImGui::SetNextItemWidth(350);
			ImGui::InputText("##addWmoPath", m_addChildWMOPath, sizeof(m_addChildWMOPath));

			ImGui::Spacing();

			ImGui::Text("Display Name (optional):");
			ImGui::SetNextItemWidth(350);
			ImGui::InputText("##addWmoName", m_addChildWMOName, sizeof(m_addChildWMOName));
			ImGui::TextDisabled("Leave empty to auto-generate from WMO filename");

			// Drag-drop target
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
				{
					std::string path(static_cast<const char*>(payload->Data), payload->DataSize - 1);
					std::strncpy(m_addChildWMOPath, path.c_str(), sizeof(m_addChildWMOPath) - 1);
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.4f, 0.9f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.8f, 0.5f, 1.0f));
			if (ImGui::Button("Add", ImVec2(120, 0)))
			{
				if (std::strlen(m_addChildWMOPath) > 0)
				{
					AddChildWMO(m_addChildWMOPath, Vector3::Zero, Quaternion::Identity, Vector3::UnitScale, m_addChildWMOName);
					m_showAddChildWMODialog = false;
				}
			}
			ImGui::PopStyleColor(3);

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				m_showAddChildWMODialog = false;
			}

			ImGui::EndPopup();
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
