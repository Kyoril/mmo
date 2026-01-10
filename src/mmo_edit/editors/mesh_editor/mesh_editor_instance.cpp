// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_editor_instance.h"

#include <algorithm>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "mesh_editor.h"
#include "editor_host.h"
#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "editor_windows/asset_picker_widget.h"
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

	MeshEditorInstance::MeshEditorInstance(EditorHost& host, MeshEditor& editor, PreviewProviderManager& previewManager, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
		, m_previewManager(previewManager)
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

		m_scene.Render(*m_camera, PixelShaderType::Forward);
		
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

	void MeshEditorInstance::LoadDataFromNode(const aiScene* mScene, const aiNode* pNode, Mesh* mesh,
		const Matrix4& transform)
	{
		if (pNode->mNumMeshes > 0)
		{
			AABB aabb = mesh->GetBounds();

			for (unsigned int idx = 0; idx < pNode->mNumMeshes; ++idx)
			{
				const aiMesh* aiMesh = mScene->mMeshes[pNode->mMeshes[idx]];
				DLOG("Submesh " << idx << " for mesh '" << String(pNode->mName.data) << "'");

				// Create a material instance for the mesh.
				const aiMaterial* pAIMaterial = mScene->mMaterials[aiMesh->mMaterialIndex];
				MaterialPtr material = m_scene.GetDefaultMaterial();

				CreateSubMesh(pNode->mName.data, idx, pNode, aiMesh, material, mesh, aabb, transform);
			}

			// We must indicate the bounding box
			mesh->SetBounds(aabb);
		}

		// Traverse all child nodes of the current node instance
		for (unsigned int childIdx = 0; childIdx < pNode->mNumChildren; childIdx++)
		{
			const aiNode* pChildNode = pNode->mChildren[childIdx];
			LoadDataFromNode(mScene, pChildNode, mesh, transform);
		}
	}

	bool MeshEditorInstance::CreateSubMesh(const String& name, int index, const aiNode* pNode, const aiMesh* aiMesh,
		const MaterialPtr& material, Mesh* mesh, AABB& boundingBox, const Matrix4& transform) const
	{
		// if animated all submeshes must have bone weights
		if (!m_mesh->GetSkeleton() && aiMesh->HasBones())
		{
			DLOG("Skipping mesh " << aiMesh->mName.C_Str() << " with bone weights");
			return false;
		}

		// now begin the object definition
		// We create a submesh per material
		SubMesh& submesh = mesh->CreateSubMesh(name + std::to_string(index));
		submesh.useSharedVertices = false;
		submesh.SetMaterial(material);

		// prime pointers to vertex related data
		aiVector3D* vec = aiMesh->mVertices;
		aiVector3D* norm = aiMesh->mNormals;
		aiVector3D* binorm = aiMesh->mBitangents;
		aiVector3D* tang = aiMesh->mTangents;
		aiVector3D* uv = aiMesh->mTextureCoords[0];
		aiColor4D* col = aiMesh->mColors[0];

		// We must create the vertex data, indicating how many vertices there will be
		submesh.vertexData = std::make_unique<VertexData>();
		submesh.vertexData->vertexStart = 0;
		submesh.vertexData->vertexCount = aiMesh->mNumVertices;

		// We must now declare what the vertex data contains
		VertexDeclaration* declaration = submesh.vertexData->vertexDeclaration;
		static constexpr unsigned short source = 0;
		uint32 offset = 0;

		DLOG(aiMesh->mNumVertices << " vertices");
		offset += declaration->AddElement(source, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
		offset += declaration->AddElement(source, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
		offset += declaration->AddElement(source, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
		offset += declaration->AddElement(source, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
		offset += declaration->AddElement(source, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
		offset += declaration->AddElement(source, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

		Matrix4 aiM = mNodeDerivedTransformByName.find(pNode->mName.data)->second* transform;
		Matrix3 normalMatrix = aiM.Linear().Inverse().Transpose();

		std::vector<POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX> vertexData(aiMesh->mNumVertices);
		POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX* dataPointer = vertexData.data();

		// Now we get access to the buffer to fill it. During so we record the bounding box.
		for (size_t i = 0; i < aiMesh->mNumVertices; ++i)
		{
			// Position
			Vector3 vectorData(vec->x, vec->y, vec->z);
			vectorData = aiM * vectorData;

			dataPointer->pos = vectorData;
			vec++;

			boundingBox.Combine(vectorData);

			// Color
			if (col)
			{
				dataPointer->color = Color(col->r, col->g, col->b, col->a);
				col++;
			}
			else
			{
				dataPointer->color = Color::White;
			}

			// Normal
			if (norm)
			{
				vectorData = normalMatrix * Vector3(norm->x, norm->y, norm->z).NormalizedCopy();
				vectorData.Normalize();

				dataPointer->normal = vectorData;
				norm++;
			}
			else
			{
				dataPointer->normal = Vector3::UnitY;
			}

			// Calculate binormal and tangent from normal
			const Vector3 c1 = dataPointer->normal.Cross(Vector3::UnitZ);
			const Vector3 c2 = dataPointer->normal.Cross(Vector3::UnitY);
			if (c1.GetSquaredLength() > c2.GetSquaredLength())
			{
				dataPointer->tangent = c1;
			}
			else
			{
				dataPointer->tangent = c2;
			}
			dataPointer->tangent.Normalize();
			dataPointer->binormal = dataPointer->normal.Cross(dataPointer->tangent);
			dataPointer->binormal.Normalize();

			// uvs
			if (uv)
			{
				dataPointer->uv[0] = uv->x;
				dataPointer->uv[1] = uv->y;
				uv++;
			}
			else
			{
				dataPointer->uv[0] = 0.0f;
				dataPointer->uv[1] = 0.0f;
			}

			dataPointer++;
		}

		VertexBufferPtr buffer = GraphicsDevice::Get().CreateVertexBuffer(
			submesh.vertexData->vertexCount,
			submesh.vertexData->vertexDeclaration->GetVertexSize(source),
			BufferUsage::StaticWriteOnly,
			vertexData.data());
		submesh.vertexData->vertexBufferBinding->SetBinding(source, buffer);

		// set bone weights
		if (aiMesh->HasBones() && m_mesh->GetSkeleton())
		{
			for (uint32 i = 0; i < aiMesh->mNumBones; i++)
			{
				if (aiBone* bone = aiMesh->mBones[i]; nullptr != bone)
				{
					String boneName = bone->mName.data;
					for (uint32 weightIdx = 0; weightIdx < bone->mNumWeights; weightIdx++)
					{
						aiVertexWeight aiWeight = bone->mWeights[weightIdx];

						VertexBoneAssignment vba;
						vba.vertexIndex = aiWeight.mVertexId;
						vba.boneIndex = m_mesh->GetSkeleton()->GetBone(boneName)->GetHandle();
						vba.weight = aiWeight.mWeight;

						submesh.AddBoneAssignment(vba);
					}
				}
			}
		}
		else
		{
			DLOG("Mesh " << aiMesh->mName.C_Str() << " has no bone weights");
		}

		if (aiMesh->mNumFaces == 0)
		{
			return true;
		}

		DLOG(aiMesh->mNumFaces << " faces");

		aiFace* faces = aiMesh->mFaces;
		int faceSz = aiMesh->mPrimitiveTypes == aiPrimitiveType_LINE ? 2 : 3;

		// Creates the index data
		submesh.indexData = std::make_unique<IndexData>();
		submesh.indexData->indexStart = 0;
		submesh.indexData->indexCount = aiMesh->mNumFaces * faceSz;

		if (aiMesh->mNumVertices >= 65536) // 32 bit index buffer
		{
			std::vector<uint32> indexDataBuffer(aiMesh->mNumFaces * faceSz);
			uint32* indexData = indexDataBuffer.data();
			for (size_t i = 0; i < aiMesh->mNumFaces; ++i)
			{
				for (int j = 0; j < faceSz; j++)
				{
					*indexData++ = faces->mIndices[j];
				}

				faces++;
			}

			submesh.indexData->indexBuffer =
				GraphicsDevice::Get().CreateIndexBuffer(
					submesh.indexData->indexCount,
					IndexBufferSize::Index_32,
					BufferUsage::StaticWriteOnly,
					indexDataBuffer.data());
		}
		else // 16 bit index buffer
		{
			std::vector<uint16> indexDataBuffer(aiMesh->mNumFaces * faceSz);
			uint16* indexData = indexDataBuffer.data();
			for (size_t i = 0; i < aiMesh->mNumFaces; ++i)
			{
				for (int j = 0; j < faceSz; j++)
					*indexData++ = faces->mIndices[j];

				faces++;
			}

			submesh.indexData->indexBuffer =
				GraphicsDevice::Get().CreateIndexBuffer(
					submesh.indexData->indexCount,
					IndexBufferSize::Index_16,
					BufferUsage::StaticWriteOnly,
					indexDataBuffer.data());
		}

		return true;
	}

	void MeshEditorInstance::ComputeNodesDerivedTransform(const aiScene* mScene, const aiNode* pNode,
		const aiMatrix4x4& accTransform)
	{
		if (!mNodeDerivedTransformByName.contains(pNode->mName.data))
		{
			mNodeDerivedTransformByName[pNode->mName.data] = Matrix4(accTransform[0]);
		}
		for (unsigned int childIdx = 0; childIdx < pNode->mNumChildren; ++childIdx)
		{
			const aiNode* pChildNode = pNode->mChildren[childIdx];
			ComputeNodesDerivedTransform(mScene, pChildNode, accTransform * pChildNode->mTransformation);
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
		const String timelineId = "Animation Timeline##" + GetAssetPath().string();

		// Draw sidebar windows
		DrawDetails(detailsId);

		// Render these only if there is a skeleton
		if (m_entity && m_entity->HasSkeleton())
		{
			DrawBones(bonesId);
			DrawAnimations(animationsId);
			DrawAnimationTimelineWindow(timelineId);
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
			
			// Split bottom for animation timeline (250px height)
			const auto bottomId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Down, 250.0f / ImGui::GetMainViewport()->Size.y, nullptr, &mainId);
			
			// Split right for details/bones panel (400px width)
			const auto sideId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 400.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);
			
			// Dock windows to appropriate areas
			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(timelineId.c_str(), bottomId);
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

	bool MeshEditorInstance::Save()
	{
		const auto file = AssetRegistry::CreateNewFile(GetAssetPath().string());
		if (!file)
		{
			ELOG("Failed to open mesh file " << GetAssetPath() << " for writing!");
			return false;
		}

		io::StreamSink sink { *file };
		io::Writer writer { sink };
		
		MeshSerializer serializer;
		serializer.Serialize(m_mesh, writer);

		ILOG("Successfully saved mesh " << GetAssetPath());
		return true;
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
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			// Mesh Actions Section
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 0.9f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
			if (ImGui::Button("Save Mesh", ImVec2(200, 0)))
			{
				Save();
			}
			ImGui::PopStyleColor(3);

			ImGui::Spacing();

			// Submesh Import Section
			if (ImGui::CollapsingHeader("Import Submesh"))
			{
				ImGui::Indent();

				ImGui::Text("Mesh File:");
				ImGui::SetNextItemWidth(-1);
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
				ImGui::InputText("##Mesh", &m_importSubmeshFile);
				ImGui::PopStyleColor();

				ImGui::Spacing();

				if (ImGui::TreeNode("Import Settings"))
				{
					ImGui::Spacing();
					ImGui::Text("Transform:");
					ImGui::InputFloat3("Offset", m_importOffset.Ptr(), "%.3f");
					ImGui::InputFloat3("Scale", m_importScale.Ptr(), "%.3f");

					Matrix3 rot;
					m_importRotation.ToRotationMatrix(rot);

					// Compute Pitch
					float pitchRad = std::asin(-rot[0][2]);
					float cosPitch = std::cos(pitchRad);
					float yawRad, rollRad;

					if (std::abs(cosPitch) > std::numeric_limits<float>::epsilon())
					{
						// Compute Yaw
						yawRad = std::atan2(rot[0][1], rot[0][0]);
						// Compute Roll
						rollRad = std::atan2(rot[1][2], rot[2][2]);
					}
					else
					{
						// Gimbal lock case
						yawRad = 0.0f;
						rollRad = std::atan2(-rot[2][1], rot[1][1]);
					}

					float rotation[3] = { Radian(rollRad).GetValueDegrees(), Radian(yawRad).GetValueDegrees(), Radian(pitchRad).GetValueDegrees() };
					if (ImGui::InputFloat3("Rotation (Roll, Yaw, Pitch)", rotation, "%.3f"))
					{
						Quaternion qRoll(Degree(rotation[0]), Vector3(1, 0, 0));
						Quaternion qPitch(Degree(rotation[2]), Vector3(0, 0, 1));
						Quaternion qYaw(Degree(rotation[1]), Vector3(0, 1, 0));
						m_importRotation = (qYaw * qPitch * qRoll);
						m_importRotation.Normalize();
					}

					ImGui::TreePop();
				}

				ImGui::Spacing();

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.5f, 0.8f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.6f, 0.9f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.7f, 1.0f, 1.0f));
				if (ImGui::Button("Import Additional Submesh", ImVec2(-1, 0)))
				{
					ImportAdditionalSubmeshes(m_importSubmeshFile);
					m_importSubmeshFile.clear();
					m_entity->SetMesh(m_mesh);
				}
				ImGui::PopStyleColor(3);

				ImGui::Unindent();
			}

			ImGui::Spacing();

			// Material Slots Section
			if (ImGui::CollapsingHeader("Sub Meshes", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent();

				if (m_entity != nullptr)
				{
					ImGui::TextDisabled("Submeshes: %d", m_entity->GetNumSubEntities());
					ImGui::Spacing();

					static const std::set<String> s_materialExtensions = { ".hmat", ".hmi" };

					for (int32 i = 0; i < m_entity->GetNumSubEntities(); ++i)
					{
						ImGui::PushID(i);

						String name = "SubMesh " + std::to_string(i);
						m_entity->GetMesh()->GetSubMeshName(i, name);

						// Compact collapsible header with unique ID
						ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.27f, 0.27f, 0.3f, 1.0f));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.32f, 0.32f, 0.35f, 1.0f));
						
						String headerId = name + "##" + std::to_string(i);
						const bool isOpen = ImGui::CollapsingHeader(headerId.c_str());
						
						ImGui::PopStyleColor(3);

						if (isOpen)
						{
							ImGui::Indent();

							// Name (compact, inline)
							ImGui::AlignTextToFramePadding();
							ImGui::Text("%d:", i);
							ImGui::SameLine();
							ImGui::SetNextItemWidth(150);
							ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
							if (ImGui::InputText("##name", &name))
							{
								m_entity->GetMesh()->NameSubMesh(i, name);
							}
							ImGui::PopStyleColor();

							ImGui::SameLine();

							// Visibility checkbox (compact)
							bool visible = m_entity->GetMesh()->GetSubMesh(i).IsVisibleByDefault();
							if (ImGui::Checkbox("Visible", &visible))
							{
								m_entity->GetMesh()->GetSubMesh(i).SetVisibleByDefault(visible);
								m_entity->GetSubEntity(i)->SetVisible(visible);
							}

							ImGui::SameLine();

							// Delete button (compact)
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 0.6f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.8f));
							ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
							if (ImGui::SmallButton("Delete"))
							{
								m_entity->GetMesh()->DestroySubMesh(i);
								m_entity->SetMesh(m_entity->GetMesh());
								i--;
								ImGui::PopStyleColor(3);
								ImGui::Unindent();
								ImGui::PopID();
								continue;
							}
							ImGui::PopStyleColor(3);

							// Material picker (with preview)
							ImGui::AlignTextToFramePadding();
							ImGui::Text("Material:");

							String materialPath;
							if (m_entity->GetSubEntity(i)->GetMaterial())
							{
								materialPath = m_entity->GetSubEntity(i)->GetMaterial()->GetName();
							}

							if (AssetPickerWidget::Draw("##material", materialPath, s_materialExtensions, &m_previewManager, nullptr, 64.0f))
							{
								if (!materialPath.empty())
								{
									m_entity->GetSubEntity(i)->SetMaterial(MaterialManager::Get().Load(materialPath));
									m_mesh->GetSubMesh(i).SetMaterial(m_entity->GetSubEntity(i)->GetMaterial());
								}
								else
								{
									m_entity->GetSubEntity(i)->SetMaterial(nullptr);
									m_mesh->GetSubMesh(i).SetMaterial(nullptr);
								}
							}

							ImGui::Separator();

							// Tags
							ImGui::AlignTextToFramePadding();
							ImGui::Text("Tags");
							ImGui::SameLine();
							
							ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.3f, 0.8f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 0.4f, 0.9f));
							ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.8f, 0.5f, 1.0f));
							if (ImGui::SmallButton("+ Add"))
							{
								m_entity->GetMesh()->GetSubMesh(i).AddTag("New Tag");
							}
							ImGui::PopStyleColor(3);
							
							if (m_entity->GetMesh()->GetSubMesh(i).TagCount() > 0)
							{
								for (uint8 tagIndex = 0; tagIndex < m_entity->GetMesh()->GetSubMesh(i).TagCount(); ++tagIndex)
								{
									ImGui::PushID(tagIndex);
									ImGui::SetNextItemWidth(-50);

									String tag = m_entity->GetMesh()->GetSubMesh(i).GetTag(tagIndex);
									ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
									const bool tagChanged = ImGui::InputText("##tag", &tag, ImGuiInputTextFlags_EnterReturnsTrue);
									ImGui::PopStyleColor();

									ImGui::SameLine();
									ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.3f, 0.8f));
									ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.4f, 0.9f));
									ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
									const bool removeClicked = ImGui::Button("X", ImVec2(30, 0));
									ImGui::PopStyleColor(3);

									ImGui::PopID();

									if (tagChanged)
									{
										m_entity->GetMesh()->GetSubMesh(i).RemoveTag(m_entity->GetMesh()->GetSubMesh(i).GetTag(tagIndex));
										m_entity->GetMesh()->GetSubMesh(i).AddTag(tag);
										break;
									}

									if (removeClicked)
									{
										m_entity->GetMesh()->GetSubMesh(i).RemoveTag(m_entity->GetMesh()->GetSubMesh(i).GetTag(tagIndex));
										break;
									}
								}
							}
							else
							{
								ImGui::SameLine();
								ImGui::TextDisabled("(none)");
							}

							ImGui::Unindent();
						}

						if (i < m_entity->GetNumSubEntities() - 1)
						{
							ImGui::Spacing();
						}

						ImGui::PopID();
					}
				}
				else
				{
					ImGui::TextDisabled("No mesh loaded");
				}

				ImGui::Unindent();
			}

			ImGui::PopStyleVar(2);
		}
		ImGui::End();
	}

	void MeshEditorInstance::DrawAnimationTimeline()
	{
		if (!m_animState)
		{
			return;
		}

		const float timelineHeight = 200.0f;
		const float rulerHeight = 30.0f;
		const float trackHeight = 40.0f;
		const float notifyHeight = 20.0f;
		
		ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImVec2 canvasSize = ImVec2(ImGui::GetContentRegionAvail().x, timelineHeight);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Background
		drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), 
			IM_COL32(30, 30, 30, 255));

		const float animLength = m_animState->GetLength();
		const float pixelsPerSecond = (canvasSize.x - 40.0f) / animLength * m_timelineZoom;

		// Draw ruler
		drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + rulerHeight),
			IM_COL32(40, 40, 40, 255));

		// Draw time markers
		const float markerInterval = 1.0f; // 1 second intervals
		for (float t = 0.0f; t <= animLength; t += markerInterval)
		{
			const float x = canvasPos.x + 20.0f + t * pixelsPerSecond;
			if (x >= canvasPos.x && x <= canvasPos.x + canvasSize.x)
			{
				drawList->AddLine(ImVec2(x, canvasPos.y + rulerHeight - 10), 
					ImVec2(x, canvasPos.y + rulerHeight), IM_COL32(150, 150, 150, 255), 1.0f);

				// Draw time label
				char label[32];
				snprintf(label, sizeof(label), "%.1f", t);
				drawList->AddText(ImVec2(x - 10, canvasPos.y + 5), IM_COL32(200, 200, 200, 255), label);
			}
		}

		// Draw sub-markers (frames at 30fps)
		const float frameInterval = 1.0f / 30.0f;
		for (float t = 0.0f; t <= animLength; t += frameInterval)
		{
			const float x = canvasPos.x + 20.0f + t * pixelsPerSecond;
			if (x >= canvasPos.x && x <= canvasPos.x + canvasSize.x)
			{
				drawList->AddLine(ImVec2(x, canvasPos.y + rulerHeight - 5),
					ImVec2(x, canvasPos.y + rulerHeight), IM_COL32(100, 100, 100, 255), 1.0f);
			}
		}

		// Draw playback cursor
		const float currentTime = m_animState->GetTimePosition();
		const float cursorX = canvasPos.x + 20.0f + currentTime * pixelsPerSecond;
		drawList->AddLine(ImVec2(cursorX, canvasPos.y), 
			ImVec2(cursorX, canvasPos.y + canvasSize.y), IM_COL32(255, 100, 100, 255), 2.0f);

		// Draw cursor triangle at top
		drawList->AddTriangleFilled(
			ImVec2(cursorX, canvasPos.y),
			ImVec2(cursorX - 6, canvasPos.y + 12),
			ImVec2(cursorX + 6, canvasPos.y + 12),
			IM_COL32(255, 100, 100, 255));

		// Draw notify track
		const float notifyTrackY = canvasPos.y + rulerHeight + 10.0f;
		drawList->AddRectFilled(
			ImVec2(canvasPos.x, notifyTrackY),
			ImVec2(canvasPos.x + canvasSize.x, notifyTrackY + trackHeight),
			IM_COL32(35, 35, 35, 255));

		drawList->AddText(ImVec2(canvasPos.x + 5, notifyTrackY + 5), 
			IM_COL32(180, 180, 180, 255), "Notifies");

		// Get notifies for current animation
		const String& animName = m_animState->GetAnimationName();
		auto& notifies = m_animationNotifies[animName];

		// Draw notifies
		m_hoveredNotifyIndex = -1;
		for (int i = 0; i < static_cast<int>(notifies.size()); ++i)
		{
			const auto& notify = notifies[i];
			const float notifyX = canvasPos.x + 20.0f + notify.time * pixelsPerSecond;
			const float notifyY = notifyTrackY + 15.0f;

			ImVec2 notifyPos(notifyX - 5, notifyY);
			ImVec2 notifySize(80, notifyHeight);

			// Check if mouse is hovering
			ImVec2 mousePos = ImGui::GetMousePos();
			bool isHovered = mousePos.x >= notifyPos.x && mousePos.x <= notifyPos.x + notifySize.x &&
				mousePos.y >= notifyPos.y && mousePos.y <= notifyPos.y + notifySize.y;

			if (isHovered)
			{
				m_hoveredNotifyIndex = i;
			}

			// Draw notify box
			ImU32 notifyColor = IM_COL32(100, 150, 255, 255);
			if (i == m_selectedNotifyIndex)
			{
				notifyColor = IM_COL32(255, 200, 100, 255);
			}
			else if (isHovered)
			{
				notifyColor = IM_COL32(150, 180, 255, 255);
			}

			drawList->AddRectFilled(notifyPos, ImVec2(notifyPos.x + notifySize.x, notifyPos.y + notifySize.y),
				notifyColor, 3.0f);
			drawList->AddRect(notifyPos, ImVec2(notifyPos.x + notifySize.x, notifyPos.y + notifySize.y),
				IM_COL32(255, 255, 255, 200), 3.0f, 0, 1.5f);

			// Draw notify diamond marker
			drawList->AddCircleFilled(ImVec2(notifyX, notifyY), 5.0f, IM_COL32(255, 255, 255, 255));

			// Draw notify text
			const char* displayText = notify.name.empty() ? notify.type.c_str() : notify.name.c_str();
			drawList->AddText(ImVec2(notifyPos.x + 5, notifyPos.y + 2), IM_COL32(255, 255, 255, 255), displayText);
		}

		// Invisible button for timeline interaction
		ImGui::SetCursorScreenPos(canvasPos);
		ImGui::InvisibleButton("timeline", canvasSize);

		// Handle timeline scrubbing
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			ImVec2 mousePos = ImGui::GetMousePos();
			float newTime = (mousePos.x - canvasPos.x - 20.0f) / pixelsPerSecond;
			newTime = Clamp(newTime, 0.0f, animLength);
			m_animState->SetTimePosition(newTime);
			m_isDraggingTimeline = true;

			// Pause animation while scrubbing
			if (m_playAnimation)
			{
				m_playAnimation = false;
			}
		}
		else
		{
			m_isDraggingTimeline = false;
		}

		// Handle notify selection and dragging
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && m_hoveredNotifyIndex >= 0)
		{
			m_selectedNotifyIndex = m_hoveredNotifyIndex;
			m_isDraggingNotify = true;
		}

		if (m_isDraggingNotify && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			if (m_selectedNotifyIndex >= 0 && m_selectedNotifyIndex < static_cast<int>(notifies.size()))
			{
				ImVec2 mousePos = ImGui::GetMousePos();
				float newTime = (mousePos.x - canvasPos.x - 20.0f) / pixelsPerSecond;
				newTime = Clamp(newTime, 0.0f, animLength);
				notifies[m_selectedNotifyIndex].time = newTime;

				// Re-sort notifies by time
				std::sort(notifies.begin(), notifies.end());
			}
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			m_isDraggingNotify = false;
		}

		// Handle notify deletion
		if (m_selectedNotifyIndex >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete))
		{
			if (m_selectedNotifyIndex < static_cast<int>(notifies.size()))
			{
				notifies.erase(notifies.begin() + m_selectedNotifyIndex);
				m_selectedNotifyIndex = -1;
			}
		}

		// Right-click context menu for adding notifies
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			ImVec2 mousePos = ImGui::GetMousePos();
			float clickTime = (mousePos.x - canvasPos.x - 20.0f) / pixelsPerSecond;
			clickTime = Clamp(clickTime, 0.0f, animLength);

			AnimationNotify newNotify;
			newNotify.name = "Notify";
			newNotify.time = clickTime;
			newNotify.type = "PlaySound";
			notifies.push_back(newNotify);
			std::sort(notifies.begin(), notifies.end());
		}

		ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, canvasPos.y + canvasSize.y));
	}

	void MeshEditorInstance::DrawAnimationTimelineWindow(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			if (m_animState)
			{
				DrawAnimationTimeline();
			}
			else
			{
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Select an animation to view timeline");
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
				// Animation Import Section
				if (ImGui::CollapsingHeader("Import Animation", ImGuiTreeNodeFlags_None))
				{
					ImGui::Indent();
					ImGui::InputText("Animation Name", &m_newAnimationName, ImGuiInputTextFlags_None, nullptr, nullptr);
					ImGui::InputText("FBX Path", &m_animationImportPath, ImGuiInputTextFlags_None, nullptr, nullptr);

					ImGui::BeginDisabled(m_newAnimationName.empty() || m_animationImportPath.empty());
					if (ImGui::Button("Import Animation"))
					{
						// Do the import
						ImportAnimationFromFbx(m_animationImportPath, m_newAnimationName);
					}
					ImGui::EndDisabled();
					ImGui::Unindent();
				}

				ImGui::Separator();

				if (m_mesh->GetSkeleton()->GetNumAnimations() > 0)
				{
					// Animation Selection
					ImGui::Text("Animation Selection");
					static const String s_defaultPreviewString = "(None)";
					const String* previewValue = &s_defaultPreviewString;
					if (m_animState)
					{
						previewValue = &m_animState->GetAnimationName();
					}

					if (ImGui::BeginCombo("##Animation", previewValue->c_str()))
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

					ImGui::Separator();

					// Animation Controls (only show if animation is selected)
					if (m_animState)
					{
						ImGui::Text("Playback Controls");
						
						// Playback buttons (Play/Pause, Stop)
						ImGui::PushStyleColor(ImGuiCol_Button, m_playAnimation ? 
							ImVec4(0.2f, 0.7f, 0.2f, 1.0f) : ImVec4(0.2f, 0.4f, 0.7f, 1.0f));
						
						if (ImGui::Button(m_playAnimation ? "Pause" : "Play", ImVec2(80, 0)))
						{
							m_playAnimation = !m_playAnimation;
							if (m_playAnimation && m_animState->GetTimePosition() >= m_animState->GetLength())
							{
								m_animState->SetTimePosition(0.0f);
							}
						}
						ImGui::PopStyleColor();

						ImGui::SameLine();
						if (ImGui::Button("Stop", ImVec2(80, 0)))
						{
							m_playAnimation = false;
							m_animState->SetTimePosition(0.0f);
						}

						ImGui::SameLine();
						if (ImGui::Button("|<", ImVec2(40, 0)))
						{
							m_animState->SetTimePosition(0.0f);
						}

						ImGui::SameLine();
						if (ImGui::Button(">|", ImVec2(40, 0)))
						{
							m_animState->SetTimePosition(m_animState->GetLength());
						}

						// Animation properties
						ImGui::Spacing();
						bool looped = m_animState->IsLoop();
						if (ImGui::Checkbox("Loop", &looped))
						{
							m_animState->SetLoop(looped);
						}

						ImGui::SameLine();
						float playRate = m_animState->GetPlayRate();
						ImGui::SetNextItemWidth(100);
						if (ImGui::DragFloat("Speed", &playRate, 0.01f, 0.1f, 5.0f, "%.2f"))
						{
							m_animState->SetPlayRate(playRate);
						}

						// Animation info
						ImGui::Spacing();
						ImGui::Text("Length: %.2fs", m_animState->GetLength());
						ImGui::Text("Current: %.2fs (%.1f%%)", 
							m_animState->GetTimePosition(),
							(m_animState->GetTimePosition() / m_animState->GetLength()) * 100.0f);

						ImGui::Separator();

						// Timeline zoom control
						ImGui::Text("Timeline Zoom");
						ImGui::SetNextItemWidth(200);
						ImGui::SliderFloat("##Zoom", &m_timelineZoom, 0.5f, 5.0f, "%.1fx");

						ImGui::Separator();

						// Notify Editor
						if (ImGui::CollapsingHeader("Notify Editor", ImGuiTreeNodeFlags_DefaultOpen))
						{
							const String& animName = m_animState->GetAnimationName();
							auto& notifies = m_animationNotifies[animName];

							ImGui::Text("Selected notify: %s", 
								m_selectedNotifyIndex >= 0 && m_selectedNotifyIndex < static_cast<int>(notifies.size()) ?
								notifies[m_selectedNotifyIndex].name.c_str() : "None");

							if (m_selectedNotifyIndex >= 0 && m_selectedNotifyIndex < static_cast<int>(notifies.size()))
							{
								auto& notify = notifies[m_selectedNotifyIndex];
								
								ImGui::Text("Edit Notify:");
								ImGui::InputText("Name", &notify.name);
								
								const char* types[] = { "PlaySound", "SpawnParticle", "SpawnEffect", "Custom" };
								int currentType = 0;
								for (int i = 0; i < 4; ++i)
								{
									if (notify.type == types[i])
									{
										currentType = i;
										break;
									}
								}
								
								if (ImGui::Combo("Type", &currentType, types, 4))
								{
									notify.type = types[currentType];
								}

								float notifyTime = notify.time;
								if (ImGui::DragFloat("Time", &notifyTime, 0.01f, 0.0f, m_animState->GetLength(), "%.2fs"))
								{
									notify.time = Clamp(notifyTime, 0.0f, m_animState->GetLength());
									std::sort(notifies.begin(), notifies.end());
								}

								if (ImGui::Button("Delete Notify"))
								{
									notifies.erase(notifies.begin() + m_selectedNotifyIndex);
									m_selectedNotifyIndex = -1;
								}
							}

							ImGui::Spacing();
							ImGui::TextWrapped("Tip: Right-click on timeline to add notify, Left-click to select, Drag to move, Delete key to remove");
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

	void MeshEditorInstance::ImportAdditionalSubmeshes(const std::filesystem::path& path)
	{
		// Build transform matrix
		const Matrix4 importTransform =
			Matrix4::GetScale(m_importScale) *
			Matrix4(m_importRotation) *
			Matrix4::GetTrans(m_importOffset);

		Assimp::Importer importer;

		const aiScene* scene = importer.ReadFile(path.string(),
			aiProcess_CalcTangentSpace |
			aiProcess_Triangulate |
			aiProcess_JoinIdenticalVertices |
			aiProcess_SortByPType |
			aiProcess_FlipUVs |
			aiProcess_GenNormals
		);

		if (!scene)
		{
			ELOG("Failed to open file: " << importer.GetErrorString());
			return;
		}

		mNodeDerivedTransformByName.clear();

		ComputeNodesDerivedTransform(scene, scene->mRootNode, scene->mRootNode->mTransformation);

		LoadDataFromNode(scene, scene->mRootNode, m_mesh.get(), importTransform);

	}
}



