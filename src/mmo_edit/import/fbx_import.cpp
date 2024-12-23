// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "fbx_import.h"

#include <fstream>
#include <imgui.h>

#include "editor_host.h"
#include "assimp/LogStream.hpp"
#include "assimp/Logger.hpp"
#include "assimp/DefaultLogger.hpp"

#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "frame_ui/color.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/skeleton_mgr.h"
#include "scene_graph/skeleton_serializer.h"


#ifdef IOS_REF
#	undef  IOS_REF
#	define IOS_REF (*(m_sdkManager->GetIOSettings()))
#endif

namespace mmo
{
    int FbxImport::msBoneCount = 0;

	void FbxImport::CustomAssimpLogStream::write(const char* message)
	{
		ILOG("[ASSIMP] " << message);
	}

	FbxImport::FbxImport(EditorHost& host)
		: m_host(host)
	{
		m_customLogStream = std::make_unique<CustomAssimpLogStream>();

		const unsigned int severity = Assimp::Logger::Info | Assimp::Logger::Err | Assimp::Logger::Warn;
		Assimp::Logger* logger = Assimp::DefaultLogger::create("AssimpLog.txt");
		logger->attachStream(m_customLogStream.get(), severity);
	}

	FbxImport::~FbxImport()
	{
		// Unfortunately manual release is required here before kill otherwise assimp will fuck up memory
		m_customLogStream.release();
		Assimp::DefaultLogger::kill();
	}

	void FbxImport::Draw()
	{
		if (m_showImportFileDialog)
		{
            ImGui::OpenPopup("Model Import Settings");
            m_showImportFileDialog = false;
		}

        if (ImGui::BeginPopupModal("Model Import Settings", nullptr))
        {
            ImGui::Text("Enter a name for the new model:");

            ImGui::InputFloat3("Offset", m_importOffset.Ptr(), "%.3f");
            ImGui::InputFloat3("Scale", m_importScale.Ptr(), "%.3f");

            ImGui::Checkbox("Import tangents", &m_importTangents);

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

            float rotation[3] = { Radian(rollRad).GetValueDegrees(), Radian(yawRad).GetValueDegrees(), Radian(pitchRad).GetValueDegrees()};
            if (ImGui::InputFloat3("Rotation (Roll, Yaw, Pitch)", rotation, "%.3f"))
            {
                Quaternion qRoll(Degree(rotation[0]), Vector3(1, 0, 0));
                Quaternion qPitch(Degree(rotation[2]), Vector3(0, 0, 1));
                Quaternion qYaw(Degree(rotation[1]), Vector3(0, 1, 0));
                m_importRotation = (qYaw * qPitch * qRoll);
                m_importRotation.Normalize();
            }

            if (ImGui::Button("Import"))
            {
                DoImportInternal();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
	}

	bool FbxImport::SaveSkeletonFile(const String& filename, const Path& assetPath)
	{
		const std::filesystem::path p = (assetPath / filename).string() + ".skel";

		// Create the file name
		const auto filePtr = AssetRegistry::CreateNewFile(p.string());
		if (filePtr == nullptr)
		{
			ELOG("Unable to create mesh file " << p);
			return false;
		}

		// Create a mesh header saver
		io::StreamSink sink{ *filePtr };
		io::Writer writer{ sink };
		SkeletonSerializer serializer;
		serializer.Export(*m_skeleton, writer);
		m_skeleton.reset();

		return true;
	}

	bool FbxImport::SaveMeshFile(const String& filename, const Path& assetPath) const
	{
		const std::filesystem::path p = (assetPath / filename).string() + ".hmsh";
		
		// Create the file name
		const auto filePtr = AssetRegistry::CreateNewFile(p.string());
		if (filePtr == nullptr)
		{
			ELOG("Unable to create mesh file " << p);
			return false;
		}
		
		// Create a mesh header saver
		io::StreamSink sink{ *filePtr };
		io::Writer writer{ sink };

		MeshSerializer serializer;
		serializer.Serialize(m_mesh, writer);

		return true;
	}

	bool FbxImport::DoImportInternal()
	{
        // Build transform matrix
        const Matrix4 importTransform =
            Matrix4::GetScale(m_importScale) *
            Matrix4(m_importRotation) *
            Matrix4::GetTrans(m_importOffset);

        Assimp::Importer importer;

        const aiScene* scene = importer.ReadFile(m_fileToImport.string(),
            aiProcess_CalcTangentSpace |
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType |
            aiProcess_FlipUVs |
            aiProcess_GenNormals
        );

        if (!scene)
        {
            ELOG("Failed to open FBX file: " << importer.GetErrorString());
            return false;
        }

        mNodeDerivedTransformByName.clear();

        m_mesh = MeshManager::Get().CreateManual((m_importAssetPath / m_fileToImport).string() + ".hmsh");

        GrabNodeNamesFromNode(scene, scene->mRootNode);
        GrabBoneNamesFromNode(scene, scene->mRootNode);

        ComputeNodesDerivedTransform(scene, scene->mRootNode, scene->mRootNode->mTransformation);

        const auto filenameWithoutExtension = m_fileToImport.filename().replace_extension();
        if (!mBonesByName.empty())
        {
            const std::filesystem::path p = (m_importAssetPath / filenameWithoutExtension).string();

            String skeletonName = p.string();
            std::ranges::replace(skeletonName, '\\', '/');
            m_skeleton = std::make_shared<Skeleton>(skeletonName);

            msBoneCount = 0;
            CreateBonesFromNode(scene, scene->mRootNode);
            msBoneCount = 0;
            CreateBoneHierarchy(scene, scene->mRootNode);
        }

        LoadDataFromNode(scene, scene->mRootNode, m_mesh.get(), importTransform);

        if (m_skeleton)
        {
            m_skeleton->SetBindingPose();

            DLOG("Root bone: " << m_skeleton->GetRootBone()->GetName());
            m_mesh->SetSkeleton(m_skeleton);
        }

        // Create the file name
        if (!SaveMeshFile(filenameWithoutExtension.string(), m_importAssetPath))
        {
            ELOG("Failed to save mesh file!");
            return false;
        }

        // Serialize skeleton
        if (m_skeleton)
        {
            if (!SaveSkeletonFile(filenameWithoutExtension.string(), m_importAssetPath))
            {
                ELOG("Failed to save skeleton file!");
                return false;
            }
        }

        mBonesByName.clear();
        mBoneNodesByName.clear();
        boneMap.clear();
        m_skeleton.reset();

        m_host.assetImported(m_importAssetPath);

        return true;
	}

	bool FbxImport::ImportFromFile(const Path& filename, const Path& currentAssetPath)
    {
        // Remember these
        m_fileToImport = filename;
        m_importAssetPath = currentAssetPath;
        m_showImportFileDialog = true;

        return true;
    }

	bool FbxImport::SupportsExtension(const String& extension) const noexcept
	{
		return extension == ".fbx" || extension == ".gltf" || extension == ".glb";
	}

	bool FbxImport::CreateSubMesh(const String& name, int index, const aiNode* pNode, const aiMesh* aiMesh, const MaterialPtr& material, Mesh* mesh, AABB& boundingBox, const Matrix4& transform) const
	{
        // if animated all submeshes must have bone weights
        if (!mBonesByName.empty() && !aiMesh->HasBones())
        {
            DLOG("Skipping mesh " << aiMesh->mName.C_Str() << " with no bone weights");
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

        Matrix4 aiM = mNodeDerivedTransformByName.find(pNode->mName.data)->second * transform;
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

            // Shall we try to import binormals and tangents and do we even have them?
            if (m_importTangents && binorm && tang)
            {
            	// Binormal
                vectorData = normalMatrix * Vector3(binorm->x, binorm->y, binorm->z).NormalizedCopy();
                vectorData.Normalize();
                dataPointer->binormal = vectorData;
                binorm++;

                // Tangent
                vectorData = normalMatrix * Vector3(tang->x, tang->y, tang->z).NormalizedCopy();
                vectorData.Normalize();
                dataPointer->tangent = vectorData;
                tang++;
            }
            else
            {
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
            }

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
        if (aiMesh->HasBones() && m_skeleton)
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
                        vba.boneIndex = m_skeleton->GetBone(boneName)->GetHandle();
                        vba.weight = aiWeight.mWeight;

                        submesh.AddBoneAssignment(vba);
                    }
                }
            }
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
            std::vector<uint16> indexDataBuffer(aiMesh->mNumFaces* faceSz);
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

	void FbxImport::GrabNodeNamesFromNode(const aiScene* mScene, const aiNode* pNode)
	{
		boneMap.emplace(String(pNode->mName.data), false);
		mBoneNodesByName[pNode->mName.data] = pNode;
		DLOG("Node " << pNode->mName.C_Str() << " found");

		// Traverse all child nodes of the current node instance
		for (unsigned int childIdx = 0; childIdx < pNode->mNumChildren; ++childIdx)
		{
			const aiNode* pChildNode = pNode->mChildren[childIdx];
			GrabNodeNamesFromNode(mScene, pChildNode);
		}
	}

	void FbxImport::GrabBoneNamesFromNode(const aiScene* mScene, const aiNode* pNode)
	{
        if (pNode->mNumMeshes > 0)
        {
            for (unsigned int idx = 0; idx < pNode->mNumMeshes; ++idx)
            {
                const aiMesh* mesh = mScene->mMeshes[pNode->mMeshes[idx]];
                if (!mesh->HasBones())
                {
                    continue;
                }

                for (uint32 i = 0; i < mesh->mNumBones; ++i)
                {
                    const aiBone* bone = mesh->mBones[i];
                    if (!bone)
                    {
                        continue;
                    }

                    mBonesByName[bone->mName.data] = bone;
                    DLOG("(" << i << ") REAL BONE with name: " << bone->mName.C_Str());

                    // flag this node and all parents of this node as needed, until we reach the node
                    // holding the mesh, or the parent.
                    const aiNode* node = mScene->mRootNode->FindNode(bone->mName.data);
                    while (node)
                    {
                        if (node->mName == pNode->mName)
                        {
                            FlagNodeAsNeeded(node->mName.data);
                            break;
                        }
                        if (node->mName == pNode->mParent->mName)
                        {
                            FlagNodeAsNeeded(node->mName.data);
                            break;
                        }

                        // Not a root node, flag this as needed and continue to the parent
                        FlagNodeAsNeeded(node->mName.data);
                        node = node->mParent;
                    }

                    // Flag all children of this node as needed
                    node = mScene->mRootNode->FindNode(bone->mName.data);
                    MarkAllChildNodesAsNeeded(node);
                }
            }
        }

		// Traverse all child nodes of the current node instance
		for (unsigned int childIdx = 0; childIdx < pNode->mNumChildren; childIdx++)
		{
			const aiNode* pChildNode = pNode->mChildren[childIdx];
			GrabBoneNamesFromNode(mScene, pChildNode);
		}
	}

	void FbxImport::ComputeNodesDerivedTransform(const aiScene* mScene, const aiNode* pNode, const aiMatrix4x4& accTransform)
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

    Matrix4 ConvertMatrix(const aiMatrix4x4& aiMat)
	{
        return Matrix4(
            aiMat.a1, aiMat.a2, aiMat.a3, aiMat.a4,
            aiMat.b1, aiMat.b2, aiMat.b3, aiMat.b4,
            aiMat.c1, aiMat.c2, aiMat.c3, aiMat.c4,
            aiMat.d1, aiMat.d2, aiMat.d3, aiMat.d4);
    }

	void FbxImport::CreateBonesFromNode(const aiScene* mScene, const aiNode* pNode)
	{
        if (IsNodeNeeded(pNode->mName.data))
        {
            String boneName = pNode->mName.data;
            if (pNode->mNumMeshes == 0)
            {
                if (m_skeleton->HasBone(boneName))
				{
					DLOG("Bone '" << boneName << "' already exists");
				}
                else
                {
                    Bone* bone = m_skeleton->CreateBone(boneName, msBoneCount);

                    const aiMatrix4x4& aiM = pNode->mTransformation;

                    Matrix4 boneMatrix = ConvertMatrix(aiM);
                    Vector3 scale;
                    Vector3 pos;
                    Quaternion rot;
                    boneMatrix.Decomposition(pos, scale, rot);

                    bone->SetPosition(pos);
                    bone->SetOrientation(rot);

                    DLOG("(" << msBoneCount << ") Creating bone '" << boneName << "'");
                    msBoneCount++;
                }
            }
        }

        // Traverse all child nodes of the current node instance
        for (unsigned int childIdx = 0; childIdx < pNode->mNumChildren; ++childIdx)
        {
            const aiNode* pChildNode = pNode->mChildren[childIdx];
            CreateBonesFromNode(mScene, pChildNode);
        }
	}

	void FbxImport::CreateBoneHierarchy(const aiScene* mScene, const aiNode* pNode)
	{
        if (IsNodeNeeded(pNode->mName.data))
        {
            Bone* parent = 0;
            Bone* child = 0;
            if (pNode->mParent)
            {
                if (m_skeleton->HasBone(pNode->mParent->mName.data))
                {
                    parent = m_skeleton->GetBone(pNode->mParent->mName.data);
                }
            }

            if (m_skeleton->HasBone(pNode->mName.data))
            {
                child = m_skeleton->GetBone(pNode->mName.data);
            }

            if (parent && child && parent != child)
            {
                parent->AddChild(*child);
            }
        }
        // Traverse all child nodes of the current node instance
        for (unsigned int childIdx = 0; childIdx < pNode->mNumChildren; childIdx++)
        {
            const aiNode* pChildNode = pNode->mChildren[childIdx];
            CreateBoneHierarchy(mScene, pChildNode);
        }
	}

	void FbxImport::LoadDataFromNode(const aiScene* mScene, const aiNode* pNode, Mesh* mesh, const Matrix4& transform)
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

            	// TODO: Material creation
                MaterialPtr material = nullptr;

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

	void FbxImport::MarkAllChildNodesAsNeeded(const aiNode* pNode)
	{
        FlagNodeAsNeeded(pNode->mName.data);

        // Traverse all child nodes of the current node instance
        for (unsigned int childIdx = 0; childIdx < pNode->mNumChildren; ++childIdx)
        {
            const aiNode* pChildNode = pNode->mChildren[childIdx];
            MarkAllChildNodesAsNeeded(pChildNode);
        }
	}

	void FbxImport::FlagNodeAsNeeded(const char* name)
	{
		if (const auto it = boneMap.find(String(name)); it != boneMap.end())
        {
            it->second = true;
        }
	}

	bool FbxImport::IsNodeNeeded(const char* name)
	{
		if (const auto it = boneMap.find(String(name)); it != boneMap.end())
        {
            return it->second;
        }

        return false;
	}
}
