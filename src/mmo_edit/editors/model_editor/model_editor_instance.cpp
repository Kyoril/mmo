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
	template <typename V, typename T> static V Lerp(const V& v0, const V& v1, const T& t)
	{
		return v0 * (1 - t) + v1 * t;
	}

	typedef std::tuple<aiVectorKey*, aiQuatKey*, aiVectorKey*> KeyframeData;
	typedef std::map<float, KeyframeData> KeyframesMap;

	template <int I>
	void GetInterpolationIterators(KeyframesMap& keyframes, const KeyframesMap::iterator it, KeyframesMap::reverse_iterator& front, KeyframesMap::iterator& back)
	{
		front = KeyframesMap::reverse_iterator(it);

		++front;
		for (; front != keyframes.rend(); ++front)
		{
			if (std::get<I>(front->second) != nullptr)
			{
				break;
			}
		}

		back = it;
		++back;
		for (; back != keyframes.end(); ++back)
		{
			if (std::get<I>(back->second) != nullptr)
			{
				break;
			}
		}
	}

	aiVector3D GetTranslate(KeyframesMap& keyframes, KeyframesMap::iterator it, double ticksPerSecond)
	{
		aiVectorKey* translateKey = std::get<0>(it->second);
		aiVector3D vect;
		if (translateKey)
		{
			vect = translateKey->mValue;
		}
		else
		{
			KeyframesMap::reverse_iterator front;
			KeyframesMap::iterator back;

			GetInterpolationIterators<0>(keyframes, it, front, back);

			KeyframesMap::reverse_iterator rend = keyframes.rend();
			KeyframesMap::iterator end = keyframes.end();
			aiVectorKey* frontKey = nullptr;
			aiVectorKey* backKey = nullptr;

			if (front != rend)
				frontKey = std::get<0>(front->second);

			if (back != end)
				backKey = std::get<0>(back->second);

			// got 2 keys can interpolate
			if (frontKey && backKey)
			{
				float prop =
					(float)(((double)it->first - frontKey->mTime) / (backKey->mTime - frontKey->mTime));
				prop /= ticksPerSecond;
				vect = Lerp(frontKey->mValue, backKey->mValue, prop);
			}

			else if (frontKey)
			{
				vect = frontKey->mValue;
			}
			else if (backKey)
			{
				vect = backKey->mValue;
			}
		}

		return vect;
	}

	aiQuaternion GetRotate(KeyframesMap& keyframes, KeyframesMap::iterator it, double ticksPerSecond)
	{
		aiQuatKey* rotationKey = std::get<1>(it->second);
		aiQuaternion rot;
		if (rotationKey)
		{
			rot = rotationKey->mValue;
		}
		else
		{
			KeyframesMap::reverse_iterator front;
			KeyframesMap::iterator back;

			GetInterpolationIterators<1>(keyframes, it, front, back);

			KeyframesMap::reverse_iterator rend = keyframes.rend();
			KeyframesMap::iterator end = keyframes.end();
			aiQuatKey* frontKey = nullptr;
			aiQuatKey* backKey = nullptr;

			if (front != rend)
				frontKey = std::get<1>(front->second);

			if (back != end)
				backKey = std::get<1>(back->second);

			// got 2 keys can interpolate
			if (frontKey && backKey)
			{
				float prop =
					(float)(((double)it->first - frontKey->mTime) / (backKey->mTime - frontKey->mTime));
				prop /= ticksPerSecond;
				aiQuaternion::Interpolate(rot, frontKey->mValue, backKey->mValue, prop);
			}

			else if (frontKey)
			{
				rot = frontKey->mValue;
			}
			else if (backKey)
			{
				rot = backKey->mValue;
			}
		}

		return rot;
	}

	aiVector3D GetScale(KeyframesMap& keyframes, KeyframesMap::iterator it, double ticksPerSecond)
	{
		aiVectorKey* scaleKey = std::get<2>(it->second);
		aiVector3D vect(1, 1, 1);
		if (scaleKey)
		{
			vect = scaleKey->mValue;
		}
		else
		{
			KeyframesMap::reverse_iterator front;
			KeyframesMap::iterator back;

			GetInterpolationIterators<2>(keyframes, it, front, back);

			KeyframesMap::reverse_iterator rend = keyframes.rend();
			KeyframesMap::iterator end = keyframes.end();
			aiVectorKey* frontKey = nullptr;
			aiVectorKey* backKey = nullptr;

			if (front != rend)
				frontKey = std::get<2>(front->second);

			if (back != end)
				backKey = std::get<2>(back->second);

			// got 2 keys can interpolate
			if (frontKey && backKey)
			{
				float prop =
					(float)(((double)it->first - frontKey->mTime) / (backKey->mTime - frontKey->mTime));
				prop /= ticksPerSecond;
				vect = Lerp(frontKey->mValue, backKey->mValue, prop);
			}

			else if (frontKey)
			{
				vect = frontKey->mValue;
			}
			else if (backKey)
			{
				vect = backKey->mValue;
			}
		}

		return vect;
	}


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
			animation.SetUseBaseKeyFrame(true, 0.0f, "");

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

				Matrix4 defBonePoseInv;
				defBonePoseInv.MakeInverseTransform(bone->GetPosition(), bone->GetScale(), bone->GetOrientation());

				// Create a new node track for the bone
				const uint16 handle = bone->GetHandle();
				NodeAnimationTrack* track = animation.HasNodeTrack(handle) ? animation.GetNodeTrack(handle) : animation.CreateNodeTrack(handle, bone);

				// We need translate, rotate and scale for each keyframe in the track
				KeyframesMap keyframes;

				for (unsigned int j = 0; j < nodeAnim->mNumPositionKeys; j++)
				{
					keyframes[static_cast<float>(nodeAnim->mPositionKeys[j].mTime / anim->mTicksPerSecond)] =
						KeyframeData(&(nodeAnim->mPositionKeys[j]), nullptr, nullptr);
				}

				for (unsigned int j = 0; j < nodeAnim->mNumRotationKeys; j++)
				{
					if (auto it =
						keyframes.find(static_cast<float>(nodeAnim->mRotationKeys[j].mTime / anim->mTicksPerSecond)); it != keyframes.end())
					{
						std::get<1>(it->second) = &(nodeAnim->mRotationKeys[j]);
					}
					else
					{
						keyframes[static_cast<float>(nodeAnim->mRotationKeys[j].mTime / anim->mTicksPerSecond)] =
							KeyframeData(nullptr, &(nodeAnim->mRotationKeys[j]), nullptr);
					}
				}

				for (unsigned int j = 0; j < nodeAnim->mNumScalingKeys; j++)
				{
					if (auto it =
						keyframes.find(static_cast<float>(nodeAnim->mScalingKeys[j].mTime / anim->mTicksPerSecond)); it != keyframes.end())
					{
						std::get<2>(it->second) = &(nodeAnim->mScalingKeys[j]);
					}
					else
					{
						keyframes[static_cast<float>(nodeAnim->mRotationKeys[j].mTime / anim->mTicksPerSecond)] =
							KeyframeData(nullptr, nullptr, &(nodeAnim->mScalingKeys[j]));
					}
				}

				auto it = keyframes.begin();
				for (auto itEnd = keyframes.end(); it != itEnd; ++it)
				{
					aiVector3D aiTrans = GetTranslate(keyframes, it, anim->mTicksPerSecond);

					Vector3 trans(aiTrans.x, aiTrans.y, aiTrans.z);

					aiQuaternion aiRot = GetRotate(keyframes, it, anim->mTicksPerSecond);
					Quaternion rot(aiRot.w, aiRot.x, aiRot.y, aiRot.z);

					aiVector3D aiScale = GetScale(keyframes, it, anim->mTicksPerSecond);
					Vector3 scale(aiScale.x, aiScale.y, aiScale.z);

					Vector3 transCopy = trans;

					Matrix4 fullTransform;
					fullTransform.MakeTransform(trans, scale, rot);

					Matrix4 poseToKey = defBonePoseInv * fullTransform;
					poseToKey.Decomposition(trans, scale, rot);

					auto keyFramePtr = track->CreateNodeKeyFrame(it->first);

					// weirdness with the root bone, But this seems to work
					if (m_mesh->GetSkeleton()->GetRootBone()->GetName() == bone->GetName())
					{
						trans = transCopy - bone->GetPosition();
					}

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
