// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "model_editor_instance.h"

#include <imgui_internal.h>

#include "model_editor.h"
#include "editor_host.h"
#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	void TraverseBone(Scene& scene, SceneNode& node, Bone& bone)
	{
		// Create node and attach it to the root node
		SceneNode* child = node.CreateChildSceneNode(bone.GetPosition(), bone.GetOrientation());
		child->SetScale(Vector3::UnitScale);

		SceneNode* scaleNode = child->CreateChildSceneNode();
		scaleNode->SetInheritScale(false);
		scaleNode->SetScale(Vector3::UnitScale * 0.03f);
		
		// Attach debug visual
		Entity* entity = scene.CreateEntity("Entity_" + bone.GetName(), "Editor/Joint.hmsh");
		entity->SetRenderQueueGroup(Overlay);
		scaleNode->AttachObject(*entity);

		for(uint32 i = 0; i < bone.GetNumChildren(); ++i)
		{
			TraverseBone(scene, *child, *dynamic_cast<Bone*>(bone.GetChild(i)));
		}
	}

	ModelEditorInstance::ModelEditorInstance(EditorHost& host, ModelEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
		, m_wireFrame(false)
	{
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 35.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-35.0f), Vector3::UnitX));

		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "DebugAxis");
			m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());
			
		m_mesh = std::make_shared<Mesh>("");
		MeshDeserializer deserializer { *m_mesh };

		const auto inputFile = AssetRegistry::OpenFile(GetAssetPath().string());
		if (inputFile)
		{
			io::StreamSource source { *inputFile };
			io::Reader reader { source };
			if (deserializer.Read(reader))
			{
				m_entry = deserializer.GetMeshEntry();
			}
		}
		else
		{
			ELOG("Unable to load mesh file " << GetAssetPath() << ": file not found!");
		}

		m_entity = m_scene.CreateEntity("Entity", m_mesh);
		if (m_entity)
		{
			m_scene.GetRootSceneNode().AttachObject(*m_entity);
			m_cameraAnchor->SetPosition(m_entity->GetBoundingBox().GetCenter());
			m_cameraNode->SetPosition(Vector3::UnitZ * m_entity->GetBoundingRadius() * 2.0f);
		}

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &ModelEditorInstance::Render);

		// Debug skeleton rendering
		if (m_mesh->HasSkeleton())
		{
			// Render each bone as a debug object
			Bone* rootBone = m_mesh->GetSkeleton()->GetRootBone();
			if (rootBone)
			{
				SceneNode* skeletonRoot = m_scene.GetRootSceneNode().CreateChildSceneNode("SkeletonRoot");
				TraverseBone(m_scene, *skeletonRoot, *rootBone);
			}
		}

	}

	ModelEditorInstance::~ModelEditorInstance()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
		}

		m_worldGrid.reset();
		m_axisDisplay.reset();
		m_scene.Clear();
	}

	void ModelEditorInstance::Render()
	{
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		if (auto& skeleton = m_mesh->GetSkeleton())
		{
			if (auto* bone = skeleton->GetBone(1))
			{
				bone->Yaw(Degree(1), TransformSpace::Parent);
				bone->SetManuallyControlled(true);
				bone->Update(true, false);

				Matrix4 offsetMatrix;
				bone->GetOffsetTransform(offsetMatrix);
			}
		}

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
		
		m_viewportRT->Update();
	}

	void RenderBoneNode(Bone& bone)
	{
		if (ImGui::TreeNodeEx(bone.GetName().c_str()))
		{
			for (uint32 i = 0; i < bone.GetNumChildren(); ++i)
			{
				Bone* childBone = dynamic_cast<Bone*>(bone.GetChild(i));
				if (childBone)
				{
					RenderBoneNode(*childBone);
				}
			}

			ImGui::TreePop();
		}
	}

	void ModelEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockspaceId = ImGui::GetID("##model_dockspace_");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();

		if (ImGui::Begin(detailsId.c_str()))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
		    if (ImGui::BeginTable("split", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
		    {
				if (m_entity != nullptr)
				{
					const auto files = AssetRegistry::ListFiles();

					for (size_t i = 0; i < m_entity->GetNumSubEntities(); ++i)
					{
						ImGui::PushID(i); // Use field index as identifier.
						ImGui::TableNextRow();
					    ImGui::TableSetColumnIndex(0);
					    ImGui::AlignTextToFramePadding();
						const bool nodeOpen = ImGui::TreeNode("Object", "SubEntity %zu", i);

					    if (nodeOpen)
					    {
							ImGui::TableNextRow();
				            ImGui::TableSetColumnIndex(0);
				            ImGui::AlignTextToFramePadding();
							constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
				            ImGui::TreeNodeEx("Field", flags, "Material");

							ImGui::TableSetColumnIndex(1);
							ImGui::SetNextItemWidth(-FLT_MIN);

							// Setup combo
							String materialName = "(None)";
							if (m_entity->GetSubEntity(i)->GetMaterial())
							{
								materialName = m_entity->GetSubEntity(i)->GetMaterial()->GetName();
							}

							ImGui::PushID(i);
							if (ImGui::BeginCombo("material", materialName.c_str()))
							{
								// For each material
								for (const auto& file : files)
								{
									if (!file.ends_with(".hmat")) continue;

									if (ImGui::Selectable(file.c_str()))
									{
										m_entity->GetSubEntity(i)->SetMaterial(MaterialManager::Get().Load(file));
									}
								}

								ImGui::EndCombo();
							}
							ImGui::PopID();

							ImGui::NextColumn();
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
				}
				
		        ImGui::EndTable();
		    }


			if (m_mesh->HasSkeleton())
			{
				ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

				// TODO: Show dropdown for skeleton to be used

				// TODO: Show bone hierarchy in tree view
				const auto& skeleton = m_mesh->GetSkeleton();

				Bone* rootBone = skeleton->GetRootBone();
				if (ImGui::BeginChild("Bone Hierarchy"))
				{
					RenderBoneNode(*rootBone);
				}
				ImGui::EndChild();
			}

		    ImGui::PopStyleVar();

			ImGui::Separator();

			if (ImGui::Button("Save"))
			{
				Save();
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

			if (ImGui::IsItemHovered())
			{
				m_cameraNode->Translate(Vector3::UnitZ* ImGui::GetIO().MouseWheel * 0.1f, TransformSpace::Local);
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				m_leftButtonPressed = true;
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

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockspaceId);

		ImGui::PopID();
	}

	void ModelEditorInstance::OnMouseButtonDown(const uint32 button, const uint16 x, const uint16 y)
	{
		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void ModelEditorInstance::OnMouseButtonUp(const uint32 button, const uint16 x, const uint16 y)
	{
		if (button == 0)
		{
			m_leftButtonPressed = false;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = false;
		}
	}

	void ModelEditorInstance::OnMouseMoved(const uint16 x, const uint16 y)
	{
		// Calculate mouse move delta
		const int16 deltaX = static_cast<int16>(x) - m_lastMouseX;
		const int16 deltaY = static_cast<int16>(y) - m_lastMouseY;

		if (m_leftButtonPressed || m_rightButtonPressed)
		{
			m_cameraAnchor->Yaw(-Degree(deltaX), TransformSpace::World);
			m_cameraAnchor->Pitch(-Degree(deltaY), TransformSpace::Local);
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void ModelEditorInstance::Save()
	{
		// Apply materials
		ASSERT(m_entity->GetNumSubEntities() == m_entry.subMeshes.size());
		for (uint16 i = 0; i < m_entity->GetNumSubEntities(); ++i)
		{
			String materialName = "Default";
			if (const auto& material = m_entity->GetSubEntity(i)->GetMaterial())
			{
				materialName = material->GetName();
			}

			m_entry.subMeshes[i].material = materialName;
		}
		
		const auto file = AssetRegistry::CreateNewFile(GetAssetPath().string());
		if (!file)
		{
			ELOG("Failed to open mesh file " << GetAssetPath() << " for writing!");
			return;
		}

		io::StreamSink sink { *file };
		io::Writer writer { sink };
		
		MeshSerializer serializer;
		serializer.ExportMesh(m_entry, writer);

		ILOG("Successfully saved mesh");
	}
}
