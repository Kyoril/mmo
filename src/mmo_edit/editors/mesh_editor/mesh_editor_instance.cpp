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
			if (ImGui::Button("Save"))
			{
				Save();
			}

			ImGui::Separator();

			
			ImGui::InputText("Mesh", &m_importSubmeshFile);

			if (ImGui::CollapsingHeader("Submesh Import Settings"))
			{
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
			}

			if (ImGui::Button("Import Additional Submesh"))
			{
				ImportAdditionalSubmeshes(m_importSubmeshFile);
				m_importSubmeshFile.clear();

				m_entity->SetMesh(m_mesh);
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

						for (int32 i = 0; i < m_entity->GetNumSubEntities(); ++i)
						{
							ImGui::PushID(i); // Use field index as identifier.
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::AlignTextToFramePadding();

							String name = "SubMesh " + std::to_string(i);
							m_entity->GetMesh()->GetSubMeshName(i, name);

							if (ImGui::TreeNode("Object", name.c_str()))
							{
								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								ImGui::AlignTextToFramePadding();
								constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
								ImGui::TreeNodeEx("Field_Name", flags, "Name");

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
								ImGui::TreeNodeEx("Field_Material", flags, "Material");

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

								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								ImGui::AlignTextToFramePadding();
								ImGui::TreeNodeEx("Field_Visible", flags, "Visible");

								ImGui::TableSetColumnIndex(1);
								ImGui::SetNextItemWidth(-FLT_MIN);

								bool visible = m_entity->GetMesh()->GetSubMesh(i).IsVisibleByDefault();
								if (ImGui::Checkbox("##visibleByDefault", &visible))
								{
									m_entity->GetMesh()->GetSubMesh(i).SetVisibleByDefault(visible);
									m_entity->GetSubEntity(i)->SetVisible(visible);
								}

								ImGui::NextColumn();

								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);
								ImGui::AlignTextToFramePadding();
								if (ImGui::TreeNode("Field_Tags", "Tags"))
								{
									ImGui::TableSetColumnIndex(1);

									if (ImGui::Button("Add Tag"))
									{
										m_entity->GetMesh()->GetSubMesh(i).AddTag("New Tag");
									}

									ImGui::NextColumn();

									for (uint8 tagIndex = 0; tagIndex < m_entity->GetMesh()->GetSubMesh(i).TagCount(); ++tagIndex)
									{
										ImGui::PushID(tagIndex);
										ImGui::TableNextRow();
										ImGui::TableSetColumnIndex(0);
										ImGui::AlignTextToFramePadding();
										ImGui::TreeNodeEx("Field", flags, "Tag");
										ImGui::TableSetColumnIndex(1);
										ImGui::SetNextItemWidth(-FLT_MIN);

										String tag = m_entity->GetMesh()->GetSubMesh(i).GetTag(tagIndex);
										const bool tagChanged = ImGui::InputText("##tag", &tag, ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_EnterReturnsTrue);
										ImGui::NextColumn();
										ImGui::PopID();

										if (tagChanged)
										{
											m_entity->GetMesh()->GetSubMesh(i).RemoveTag(m_entity->GetMesh()->GetSubMesh(i).GetTag(tagIndex));
											m_entity->GetMesh()->GetSubMesh(i).AddTag(tag);

											// We break here because the tag list has changed and thus the index might no longer be valid
											break;
										}
									}

									ImGui::TreePop();
								}

								if (ImGui::Button("Delete"))
								{
									m_entity->GetMesh()->DestroySubMesh(i);
									m_entity->SetMesh(m_entity->GetMesh());
									i--;
								}

								ImGui::TableSetColumnIndex(1);
								ImGui::SetNextItemWidth(-FLT_MIN);

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
