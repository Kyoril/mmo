// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "model_editor_instance.h"

#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "model_editor.h"
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

		if (const auto inputFile = AssetRegistry::OpenFile(GetAssetPath().string()))
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
		if (m_entity->HasSkeleton())
		{
			// Render each bone as a debug object
			if (Bone* rootBone = m_entity->GetSkeleton()->GetRootBone())
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

		if (m_animState)
		{
			m_animState->AddTime(ImGui::GetIO().DeltaTime);
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

	void RenderBoneNode(const Bone& bone)
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

		const auto dockSpaceId = ImGui::GetID("##model_dockspace_");
		ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();
		const String bonesId = "Bones##" + GetAssetPath().string();
		const String animationsId = "Animation##" + GetAssetPath().string();

		// Draw sidebar windows
		DrawDetails(detailsId);
		DrawBones(bonesId);
		DrawAnimations(animationsId);

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
			ImGui::DockBuilderDockWindow(detailsId.c_str(), sideId);

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockSpaceId);

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

	void ModelEditorInstance::SetAnimationState(AnimationState* animState)
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
			m_animState->SetLoop(true);
			m_animState->SetEnabled(true);
			m_animState->SetWeight(1.0f);
		}
	}

	void ModelEditorInstance::DrawDetails(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
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

						if (const bool nodeOpen = ImGui::TreeNode("Object", "SubEntity %zu", i))
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

			ImGui::PopStyleVar();

			ImGui::Separator();

			if (ImGui::Button("Save"))
			{
				Save();
			}
		}
		ImGui::End();

	}

	void ModelEditorInstance::DrawAnimations(const String& id)
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
				}
				else
				{
					ImGui::Text("No animations available");
				}
			}
		}
		ImGui::End();
	}

	void ModelEditorInstance::DrawBones(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
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
		}
	ImGui::End();

	}

	void ModelEditorInstance::DrawViewport(const String& id)
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
		}
		ImGui::End();
	}

	void ModelEditorInstance::ImportAnimationFromFbx(const std::filesystem::path& path, const String& animationName)
	{
		Assimp::Importer importer;

		importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

		const aiScene* scene = importer.ReadFile(path.string().c_str(),
			aiProcess_CalcTangentSpace |
			aiProcess_Triangulate |
			aiProcess_JoinIdenticalVertices |
			aiProcess_SortByPType |
			aiProcess_MakeLeftHanded |
			aiProcess_FlipUVs |
			aiProcess_LimitBoneWeights |
			aiProcess_PopulateArmatureData |
			aiProcess_GenNormals |
			aiProcess_FlipWindingOrder);

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
		for (int i = 0; i < scene->mNumAnimations; ++i)
		{
			const aiAnimation* anim = scene->mAnimations[i];
			DLOG("Animation " << i << ": " << anim->mName.C_Str() << " with " << anim->mNumChannels << " channels");
			DLOG("\tDuration: " << anim->mDuration << " ticks (" << (anim->mDuration / anim->mTicksPerSecond) << " seconds)");

			if (m_entity->GetSkeleton()->HasAnimation(m_newAnimationName))
			{
				m_entity->GetSkeleton()->RemoveAnimation(m_newAnimationName);
			}

			// Create the animation
			Animation& animation = m_entity->GetSkeleton()->CreateAnimation(m_newAnimationName, static_cast<float>(anim->mDuration / anim->mTicksPerSecond));
			animation.SetUseBaseKeyFrame(true, 0.0f, m_newAnimationName);

			for (int channelIndex = 0; channelIndex < anim->mNumChannels; ++channelIndex)
			{
				aiNodeAnim* nodeAnim = anim->mChannels[channelIndex];

				DLOG("\tBone " << nodeAnim->mNodeName.C_Str());

				// Try to find the given bone in our skeleton
				Bone* bone = m_entity->GetSkeleton()->GetBone(nodeAnim->mNodeName.C_Str());
				if (!bone)
				{
					ELOG("Unable to find bone " << nodeAnim->mNodeName.C_Str() << " in skeleton, bone animation will not be applied!");
					continue;
				}

				// Create a new node track for the bone
				const uint16 handle = bone->GetHandle();
				NodeAnimationTrack* track = animation.HasNodeTrack(handle) ? animation.GetNodeTrack(handle) : animation.CreateNodeTrack(handle, bone);

				std::map<double, TransformKeyFrame*> keyFramesByTime;

				for (unsigned posKeyIndex = 0; posKeyIndex < nodeAnim->mNumPositionKeys; ++posKeyIndex)
				{
					const aiVectorKey& posKey = nodeAnim->mPositionKeys[posKeyIndex];
					DLOG("\t\tPOS #" << posKeyIndex << ": " << posKey.mTime / anim->mTicksPerSecond << " -> " << posKey.mValue.x << ", " << posKey.mValue.y << ", " << posKey.mValue.z)

					if (keyFramesByTime.contains(posKey.mTime))
					{
						keyFramesByTime[posKey.mTime]->SetTranslate(Vector3(posKey.mValue.x, posKey.mValue.y, posKey.mValue.z));
						continue;
					}

					auto keyFramePtr = track->CreateNodeKeyFrame(static_cast<float>(posKey.mTime / anim->mTicksPerSecond));
					keyFramePtr->SetTranslate(Vector3(posKey.mValue.x, posKey.mValue.y, posKey.mValue.z));

					keyFramesByTime[posKey.mTime] = keyFramePtr.get();
				}

				for (unsigned rotKeyIndex = 0; rotKeyIndex < nodeAnim->mNumRotationKeys; ++rotKeyIndex)
				{
					const aiQuatKey& rotKey = nodeAnim->mRotationKeys[rotKeyIndex];
					const Quaternion rot(rotKey.mValue.w, rotKey.mValue.x, rotKey.mValue.y, rotKey.mValue.z);
					DLOG("\t\tROT #" << rotKeyIndex << ": " << rotKey.mTime / anim->mTicksPerSecond << " -> " << rot.GetRoll().GetValueDegrees() << ", " << rot.GetYaw().GetValueDegrees() << ", " << rot.GetPitch().GetValueDegrees());

					if (keyFramesByTime.contains(rotKey.mTime))
					{
						keyFramesByTime[rotKey.mTime]->SetRotation(rot);
						continue;
					}

					auto keyFramePtr = track->CreateNodeKeyFrame(static_cast<float>(rotKey.mTime / anim->mTicksPerSecond));
					keyFramePtr->SetRotation(rot);

					keyFramesByTime[rotKey.mTime] = keyFramePtr.get();
				}

				for (unsigned scaleKeyIndex = 0; scaleKeyIndex < nodeAnim->mNumScalingKeys; ++scaleKeyIndex)
				{
					const aiVectorKey& scaleKey = nodeAnim->mScalingKeys[scaleKeyIndex];
					DLOG("\t\tSCALE #" << scaleKeyIndex << ": " << scaleKey.mTime / anim->mTicksPerSecond << " -> " << scaleKey.mValue.x << ", " << scaleKey.mValue.y << ", " << scaleKey.mValue.z);

					if (keyFramesByTime.contains(scaleKey.mTime))
					{
						keyFramesByTime[scaleKey.mTime]->SetScale(Vector3(scaleKey.mValue.x, scaleKey.mValue.y, scaleKey.mValue.z));
						continue;
					}

					auto keyFramePtr = track->CreateNodeKeyFrame(static_cast<float>(scaleKey.mTime / anim->mTicksPerSecond));
					keyFramePtr->SetScale(Vector3(scaleKey.mValue.x, scaleKey.mValue.y, scaleKey.mValue.z));

					keyFramesByTime[scaleKey.mTime] = keyFramePtr.get();
				}

				track->Optimize();
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
