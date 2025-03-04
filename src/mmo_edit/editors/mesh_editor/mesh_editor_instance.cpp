// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_editor_instance.h"

#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "mesh_editor.h"
#include "editor_host.h"
#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "scene_graph/animation_state.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/scene_node.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include "assimp/LogStream.hpp"
#include "assimp/Logger.hpp"
#include "assimp/DefaultLogger.hpp"
#include "math/aabb_tree.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/skeleton_serializer.h"

namespace mmo
{
	void TraverseBone(Scene& scene, SceneNode& node, Bone& bone)
	{
		// Create node and attach it to the root node
		SceneNode* child = node.CreateChildSceneNode(bone.GetPosition(), bone.GetOrientation());
		child->SetScale(Vector3::UnitScale);

		SceneNode* scaleNode = child->CreateChildSceneNode();
		scaleNode->SetInheritScale(false);
		scaleNode->SetScale(Vector3::UnitScale * 0.01f);
		
		// Attach debug visual
		Entity* entity = scene.CreateEntity("Entity_" + bone.GetName(), "Editor/Joint.hmsh");
		entity->SetRenderQueueGroup(Overlay);
		scaleNode->AttachObject(*entity);

		for(uint32 i = 0; i < bone.GetNumChildren(); ++i)
		{
			TraverseBone(scene, *child, *dynamic_cast<Bone*>(bone.GetChild(i)));
		}
	}

	MeshEditorInstance::MeshEditorInstance(EditorHost& host, MeshEditor& editor, Path asset)
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
			
		m_mesh = MeshManager::Get().Load(m_assetPath.string());
		ASSERT(m_mesh);

		m_entity = m_scene.CreateEntity("Entity", m_mesh);
		if (m_entity)
		{
			m_scene.GetRootSceneNode().AttachObject(*m_entity);
			m_cameraAnchor->SetPosition(m_entity->GetBoundingBox().GetCenter());
			m_cameraNode->SetPosition(Vector3::UnitZ * m_entity->GetBoundingRadius() * 2.0f);
		}

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &MeshEditorInstance::Render);

		// Append axis to node
		m_selectedBoneNode = m_scene.GetRootSceneNode().CreateChildSceneNode();
		m_selectedBoneAxis = std::make_unique<AxisDisplay>(m_scene, "SelectedBoneAxis");
		m_selectedBoneNode->AddChild(m_selectedBoneAxis->GetSceneNode());

		// Debug skeleton rendering
		if (m_entity->HasSkeleton())
		{
			// Render each bone as a debug object
			if (Bone* rootBone = m_entity->GetSkeleton()->GetRootBone())
			{
				SceneNode* skeletonRoot = m_scene.GetRootSceneNode().CreateChildSceneNode("SkeletonRoot");
				TraverseBone(m_scene, *skeletonRoot, *rootBone);

				skeletonRoot->SetVisible(false, true);
			}
		}
	}

	MeshEditorInstance::~MeshEditorInstance()
	{
		if (m_entity)
		{
			m_scene.DestroyEntity(*m_entity);
		}

		m_worldGrid.reset();
		m_axisDisplay.reset();
		m_selectedBoneAxis.reset();

		m_scene.Clear();
	}

	void MeshEditorInstance::Render()
	{
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		if (m_animState && m_playAnimation)
		{
			m_animState->AddTime(ImGui::GetIO().DeltaTime);
		}

		if (m_selectedBone && m_selectedBoneNode)
		{
			m_selectedBoneNode->SetPosition(m_selectedBone->GetDerivedPosition());
			m_selectedBoneNode->SetOrientation(m_selectedBone->GetDerivedOrientation());
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

	void MeshEditorInstance::RenderBoneNode(Bone& bone)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
		if (bone.GetName() == m_selectedBoneName)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}

		if (ImGui::TreeNodeEx(bone.GetName().c_str(), flags))
		{
			if (ImGui::IsItemClicked())
			{
				m_selectedBoneName = bone.GetName();
				m_selectedBone = &bone;
			}

			for (uint32 i = 0; i < bone.GetNumChildren(); ++i)
			{
				if (const auto childBone = dynamic_cast<Bone*>(bone.GetChild(i)))
				{
					RenderBoneNode(*childBone);
				}
			}

			ImGui::TreePop();
		}
	}

	void MeshEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockSpaceId = ImGui::GetID("##model_dockspace_");
		ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();
		const String collisionId = "Collision##" + GetAssetPath().string();
		const String bonesId = "Bones##" + GetAssetPath().string();
		const String animationsId = "Animation##" + GetAssetPath().string();

		// Draw sidebar windows
		DrawDetails(detailsId);

		// Render these only if there is a skeleton
		if (m_entity && m_entity->HasSkeleton())
		{
			DrawBones(bonesId);
			DrawAnimations(animationsId);
		}
		else
		{
			DrawCollision(collisionId);
		}
		
		// Draw viewport
		DrawViewport(viewportId);

		// Init dock layout to dock sidebar windows to the right by default
		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar); // Add empty node
			ImGui::DockBuilderSetNodeSize(dockSpaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockSpaceId;
			const auto sideId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 400.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);
			
			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(animationsId.c_str(), sideId);
			ImGui::DockBuilderDockWindow(bonesId.c_str(), sideId);
			ImGui::DockBuilderDockWindow(collisionId.c_str(), sideId);
			ImGui::DockBuilderDockWindow(detailsId.c_str(), sideId);

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockSpaceId);

		ImGui::PopID();
	}

	void MeshEditorInstance::OnMouseButtonDown(const uint32 button, const uint16 x, const uint16 y)
	{
		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void MeshEditorInstance::OnMouseButtonUp(const uint32 button, const uint16 x, const uint16 y)
	{
		if (button == 0)
		{
			m_leftButtonPressed = false;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = false;
		}
		else if(button == 2)
		{
			m_middleButtonPressed = false;
		}
	}

	void MeshEditorInstance::OnMouseMoved(const uint16 x, const uint16 y)
	{
		// Calculate mouse move delta
		const int16 deltaX = static_cast<int16>(x) - m_lastMouseX;
		const int16 deltaY = static_cast<int16>(y) - m_lastMouseY;

		if (m_leftButtonPressed || m_rightButtonPressed)
		{
			m_cameraAnchor->Yaw(-Degree(deltaX), TransformSpace::World);
			m_cameraAnchor->Pitch(-Degree(deltaY), TransformSpace::Local);
		}

		if (m_middleButtonPressed)
		{
			m_cameraAnchor->Translate(Vector3(0.0f, deltaY * 0.05f, 0.0f), TransformSpace::Local);
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void MeshEditorInstance::Save()
	{
		const auto file = AssetRegistry::CreateNewFile(GetAssetPath().string());
		if (!file)
		{
			ELOG("Failed to open mesh file " << GetAssetPath() << " for writing!");
			return;
		}

		io::StreamSink sink { *file };
		io::Writer writer { sink };
		
		MeshSerializer serializer;
		serializer.Serialize(m_mesh, writer);
		ILOG("Successfully saved mesh");
	}

	void MeshEditorInstance::SetAnimationState(AnimationState* animState)
	{
		if (m_animState == animState)
		{
			return;
		}

		if (m_animState)
		{
			m_animState->SetEnabled(false);
		}

		m_animState = animState;

		if (m_animState)
		{
			m_animState->SetTimePosition(0.0f);
			m_animState->SetEnabled(true);
			m_animState->SetWeight(1.0f);
		}
	}

	void MeshEditorInstance::DrawDetails(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			if (ImGui::Button("Save"))
			{
				Save();
			}

			ImGui::Separator();

			if (ImGui::CollapsingHeader("Sub Meshes", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
				if (ImGui::BeginTable("split", 2, ImGuiTableFlags_Resizable))
				{
					if (m_entity != nullptr)
					{
						const auto files = AssetRegistry::ListFiles();

						for (uint16 i = 0; i < m_entity->GetNumSubEntities(); ++i)
						{
							ImGui::PushID(i); // Use field index as identifier.
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::AlignTextToFramePadding();

							String name = "SubMesh " + std::to_string(i);
							m_entity->GetMesh()->GetSubMeshName(i, name);

							if (const bool nodeOpen = ImGui::TreeNode("Object", name.c_str()))
							{
								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								ImGui::AlignTextToFramePadding();
								constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
								ImGui::TreeNodeEx("Field", flags, "Name");

								ImGui::TableSetColumnIndex(1);
								ImGui::SetNextItemWidth(-FLT_MIN);

								if (ImGui::InputText("##name", &name))
								{
									m_entity->GetMesh()->NameSubMesh(i, name);
								}

								ImGui::NextColumn();

								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								ImGui::AlignTextToFramePadding();
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
										if (!file.ends_with(".hmat") && !file.ends_with(".hmi")) continue;

										if (ImGui::Selectable(file.c_str()))
										{
											m_entity->GetSubEntity(i)->SetMaterial(MaterialManager::Get().Load(file));
											m_mesh->GetSubMesh(i).SetMaterial(m_entity->GetSubEntity(i)->GetMaterial());
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

				ImGui::PopStyleVar();
			}
		}
		ImGui::End();
	}

	void MeshEditorInstance::DrawAnimations(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			if (m_mesh->HasSkeleton())
			{
				// Ask for a name for the new animation
				ImGui::InputText("Animation Name", &m_newAnimationName, ImGuiInputTextFlags_None, nullptr, nullptr);
				ImGui::InputText("FBX Path", &m_animationImportPath, ImGuiInputTextFlags_None, nullptr, nullptr);

				ImGui::BeginDisabled(m_newAnimationName.empty() || m_animationImportPath.empty());
				if (ImGui::Button("Import Animation"))
				{
					// Do the import
					ImportAnimationFromFbx(m_animationImportPath, m_newAnimationName);
				}
				ImGui::EndDisabled();

				ImGui::Separator();

				if (m_mesh->GetSkeleton()->GetNumAnimations() > 0)
				{
					// Draw a popup box with all available animations and select the active one

					static const String s_defaultPreviewString = "(None)";
					const String* previewValue = &s_defaultPreviewString;
					if (m_animState)
					{
						previewValue = &m_animState->GetAnimationName();
					}

					if (ImGui::BeginCombo("Animation", previewValue->c_str()))
					{
						if (ImGui::Selectable(s_defaultPreviewString.c_str()))
						{
							SetAnimationState(nullptr);
						}

						for (uint16 i = 0; i < m_mesh->GetSkeleton()->GetNumAnimations(); ++i)
						{
							const auto& anim = m_mesh->GetSkeleton()->GetAnimation(i);
							if (ImGui::Selectable(anim->GetName().c_str()))
							{
								SetAnimationState(m_entity->GetAnimationState(anim->GetName()));
							}
						}
						ImGui::EndCombo();
					}

					if (m_animState)
					{
						bool looped = m_animState->IsLoop();
						if (ImGui::Checkbox("Looped", &looped))
						{
							m_animState->SetLoop(looped);
						}
					}

					if (ImGui::Checkbox("Play", &m_playAnimation) && m_playAnimation)
					{
						if (m_animState)
						{
							m_animState->SetTimePosition(0.0f);
						}
					}

					if (m_animState != nullptr)
					{
						float timePos = m_animState->GetTimePosition();
						if (ImGui::SliderFloat("Time pos", &timePos, 0.0f, m_animState->GetLength()))
						{
							m_animState->SetTimePosition(timePos);
						}
					}
				}
				else
				{
					ImGui::Text("No animations available");
				}
			}
		}
		ImGui::End();
	}

	void MeshEditorInstance::DrawBones(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			if (m_entity->HasSkeleton())
			{
				ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

				// TODO: Show dropdown for skeleton to be used

				// TODO: Show bone hierarchy in tree view
				const auto& skeleton = m_entity->GetSkeleton();

				Bone* rootBone = skeleton->GetRootBone();
				if (ImGui::BeginChild("Bone Hierarchy"))
				{
					RenderBoneNode(*rootBone);
				}
				ImGui::EndChild();
			}
		}
		ImGui::End();
	}

	void ReadVertexDataPositions(const VertexData& vertexData, std::vector<Vector3>& out_vertexPositions)
	{
		// Get shared vertex data
		const auto buffer = vertexData.vertexBufferBinding->GetBuffer(0);

		auto bufferData = static_cast<uint8*>(buffer->Map(LockOptions::ReadOnly));
		ASSERT(bufferData);

		for (size_t i = 0; i < vertexData.vertexCount; ++i)
		{
			const VertexElement* posElement = vertexData.vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Position);
			float* position = nullptr;
			posElement->BaseVertexPointerToElement(bufferData, &position);

			const float x = *position++;
			const float y = *position++;
			const float z = *position++;
			out_vertexPositions.emplace_back(x, y, z);

			bufferData += vertexData.vertexDeclaration->GetVertexSize(0);
		}

		buffer->Unmap();
	}

	void ReadIndexData(const IndexData& indexData, uint32 offset, std::vector<uint32>& out_indices)
	{
		if (indexData.indexBuffer->GetIndexSize() == IndexBufferSize::Index_16)
		{
			const uint16* indices = reinterpret_cast<uint16*>(indexData.indexBuffer->Map(LockOptions::ReadOnly));
			ASSERT(indices);

			for (size_t i = 0; i < indexData.indexCount; ++i)
			{
				out_indices.push_back((*indices++) + offset);
			}
		}
		else
		{
			const uint32* indices = reinterpret_cast<uint32*>(indexData.indexBuffer->Map(LockOptions::ReadOnly));
			ASSERT(indices);

			for (size_t i = 0; i < indexData.indexCount; ++i)
			{
				out_indices.push_back((*indices++) + offset);
			}
		}

		indexData.indexBuffer->Unmap();
	}

	void MeshEditorInstance::DrawCollision(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			if (ImGui::Button("Save"))
			{
				Save();
			}

			ImGui::Separator();

			if (ImGui::Button("Clear"))
			{
				m_mesh->GetCollisionTree().Clear();
			}

			ImGui::SameLine();

			if (ImGui::Button("Build Complex"))
			{
				m_mesh->GetCollisionTree().Clear();

				// Gather all vertex data
				if (m_mesh->sharedVertexData == nullptr)
				{
					std::vector<Vector3> vertices;
					std::vector<uint32> indices;

					for (uint16 i = 0; i < m_mesh->GetSubMeshCount(); ++i)
					{
						if (!m_includedSubMeshes.contains(i))
						{
							continue;
						}

						SubMesh& sub = m_mesh->GetSubMesh(i);

						const uint32 vertexOffset = vertices.size();
						vertices.reserve(vertices.size() + sub.vertexData->vertexCount);
						indices.reserve(indices.size() + sub.indexData->indexCount);

						ReadVertexDataPositions(*sub.vertexData, vertices);
						ReadIndexData(*sub.indexData, vertexOffset, indices);
					}

					m_mesh->GetCollisionTree().Clear();
					m_mesh->GetCollisionTree().Build(vertices, indices);
				}
			}

			static const char* s_noMaterial = "(No Material)";

			if (ImGui::CollapsingHeader("Meshes To Include", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const AABBTree& tree = m_mesh->GetCollisionTree();
				ImGui::Text("Nodes: %zu", tree.GetNodes().size());

				for (uint16 i = 0; i < m_mesh->GetSubMeshCount(); ++i)
				{
					ImGui::PushID(i);
					bool included = m_includedSubMeshes.contains(i);
					if (ImGui::Checkbox("##include", &included))
					{
						if (included)
						{
							m_includedSubMeshes.insert(i);
						}
						else
						{
							m_includedSubMeshes.erase(i);
						}
					}
					ImGui::SameLine();

					const char* materialName = m_mesh->GetSubMesh(i).GetMaterial() ? m_mesh->GetSubMesh(i).GetMaterial()->GetName().data() : s_noMaterial;
					ImGui::Text("#%u: %s", i + 1, materialName);
					ImGui::PopID();
				}
				
			}
		}
		ImGui::End();
	}

	void MeshEditorInstance::DrawViewport(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
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
				m_cameraNode->Translate(Vector3::UnitZ * ImGui::GetIO().MouseWheel * 0.1f, TransformSpace::Local);
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				m_leftButtonPressed = true;
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Middle))
			{
				m_middleButtonPressed = true;
			}

		}
		ImGui::End();
	}

	void MeshEditorInstance::ImportAnimationFromFbx(const std::filesystem::path& path, const String& animationName)
	{
		Assimp::Importer importer;

		importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

		const aiScene* scene = importer.ReadFile(path.string().c_str(),
			aiProcess_SortByPType
		);

		if (!scene)
		{
			ELOG("Failed to open FBX file: " << importer.GetErrorString());
			return;
		}

		if (!scene->HasAnimations())
		{
			ELOG("FBX file has no animation data!");
			return;
		}

		SetAnimationState(nullptr);

		DLOG("Scene has " << scene->mNumAnimations << " animations");
		for (uint32 i = 0; i < scene->mNumAnimations; ++i)
		{
			const aiAnimation* anim = scene->mAnimations[i];
			DLOG("Animation " << i << ": " << anim->mName.C_Str() << " with " << anim->mNumChannels << " channels");
			DLOG("\tDuration: " << anim->mDuration << " ticks (" << (anim->mDuration / anim->mTicksPerSecond) << " seconds)");

			// Setup an animation evaluator instance
			m_animEvaluator = std::make_unique<AnimEvaluator>(anim);

			if (m_entity->GetSkeleton()->HasAnimation(m_newAnimationName))
			{
				m_entity->GetSkeleton()->RemoveAnimation(m_newAnimationName);
			}

			m_entity->GetSkeleton()->Reset();

			// Create the animation
			Animation& animation = m_entity->GetSkeleton()->CreateAnimation(m_newAnimationName, static_cast<float>(anim->mDuration / anim->mTicksPerSecond));
			animation.SetUseBaseKeyFrame(false, 0.0f, "");
			animation.SetInterpolationMode(Animation::InterpolationMode::Linear);

			for (uint32 channelIndex = 0; channelIndex < anim->mNumChannels; ++channelIndex)
			{
				aiNodeAnim* nodeAnim = anim->mChannels[channelIndex];

				// Try to find the given bone in our skeleton
				Bone* bone = m_entity->GetSkeleton()->GetBone(nodeAnim->mNodeName.C_Str());
				if (!bone)
				{
					ELOG("Unable to find bone " << nodeAnim->mNodeName.C_Str() << " in skeleton, bone animation will not be applied!");
					continue;
				}

				Matrix4 defBonePoseInv;
				defBonePoseInv.MakeInverseTransform(bone->GetPosition(), bone->GetScale(), bone->GetOrientation());

				const aiMatrix4x4 aiBonePoseInv(
					defBonePoseInv[0][0], defBonePoseInv[0][1], defBonePoseInv[0][2], defBonePoseInv[0][3],
					defBonePoseInv[1][0], defBonePoseInv[1][1], defBonePoseInv[1][2], defBonePoseInv[1][3],
					defBonePoseInv[2][0], defBonePoseInv[2][1], defBonePoseInv[2][2], defBonePoseInv[2][3],
					defBonePoseInv[3][0], defBonePoseInv[3][1], defBonePoseInv[3][2], defBonePoseInv[3][3]
				);

				// Create a new node track for the bone
				const uint16 handle = bone->GetHandle();
				NodeAnimationTrack* track = animation.HasNodeTrack(handle) ? animation.GetNodeTrack(handle) : animation.CreateNodeTrack(handle, bone);

				// Iterate through each key type and note the time value
				std::set<double> keyTimes;
				for (unsigned int j = 0; j < nodeAnim->mNumPositionKeys; j++)
				{
					keyTimes.insert(nodeAnim->mPositionKeys[j].mTime / anim->mTicksPerSecond);
				}

				for (unsigned int j = 0; j < nodeAnim->mNumRotationKeys; j++)
				{
					keyTimes.insert(nodeAnim->mRotationKeys[j].mTime / anim->mTicksPerSecond);
				}

				for (unsigned int j = 0; j < nodeAnim->mNumScalingKeys; j++)
				{
					keyTimes.insert(nodeAnim->mScalingKeys[j].mTime / anim->mTicksPerSecond);
				}

				const auto& boneLocalTransforms = m_animEvaluator->GetTransformations();

				// Now for each key, create a keyframe
				for (const auto time : keyTimes)
				{
					m_animEvaluator->Evaluate(time);

					// Update local bone transform
					aiVector3D aiTrans, aiScale;
					aiQuaternion aiRot;

					boneLocalTransforms[channelIndex].Decompose(aiScale, aiRot, aiTrans);
					Vector3 transCopy(aiTrans.x, aiTrans.y, aiTrans.z);
					
					const aiMatrix4x4 pose = aiBonePoseInv * boneLocalTransforms[channelIndex];
					pose.Decompose(aiScale, aiRot, aiTrans);

					Vector3 trans(aiTrans.x, aiTrans.y, aiTrans.z);
					Quaternion rot(aiRot.w, aiRot.x, aiRot.y, aiRot.z);
					Vector3 scale(aiScale.x, aiScale.y, aiScale.z);

					// This line ensures that we set the translation relative to the bind pose instead of relative to the last key frame!
					trans = transCopy - bone->GetPosition();

					auto keyFramePtr = track->CreateNodeKeyFrame(static_cast<float>(time));
					keyFramePtr->SetTranslate(trans);
					keyFramePtr->SetRotation(rot);
					keyFramePtr->SetScale(scale);
				}
			}

			animation.Optimize();
		}

		// Init animation state
		m_entity->GetSkeleton()->InitAnimationState(*m_entity->GetAllAnimationStates());

		// Serialize skeleton
		const std::filesystem::path p = m_mesh->GetSkeleton()->GetName();

		// Create the file name
		const auto filePtr = AssetRegistry::CreateNewFile(p.string());
		if (filePtr == nullptr)
		{
			ELOG("Unable to create skeleton file " << p);
			return;
		}

		io::StreamSink sink{ *filePtr };
		io::Writer writer{ sink };
		SkeletonSerializer serializer;
		serializer.Export(*m_mesh->GetSkeleton(), writer);
		ILOG("Successfully saved animation to skeleton " << p);
	}
}
