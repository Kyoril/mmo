// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "fbx_import.h"

#include <fstream>

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

	FbxImport::FbxImport()
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

	bool FbxImport::ImportFromFile(const Path& filename, const Path& currentAssetPath)
	{
		Assimp::Importer importer;

		const aiScene* scene = importer.ReadFile(filename.string(),
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

        m_mesh = MeshManager::Get().CreateManual((currentAssetPath / filename).string() + ".hmsh");

		GrabNodeNamesFromNode(scene, scene->mRootNode);
		GrabBoneNamesFromNode(scene, scene->mRootNode);

		ComputeNodesDerivedTransform(scene, scene->mRootNode, scene->mRootNode->mTransformation);

        const auto filenameWithoutExtension = filename.filename().replace_extension();
        if (!mBonesByName.empty())
        {
            const std::filesystem::path p = (currentAssetPath / filenameWithoutExtension).string();

            String skeletonName = p.string();
            std::ranges::replace(skeletonName, '\\', '/');
            m_skeleton = std::make_shared<Skeleton>(skeletonName);

            msBoneCount = 0;
            CreateBonesFromNode(scene, scene->mRootNode);
            msBoneCount = 0;
            CreateBoneHierarchy(scene, scene->mRootNode);
        }

        LoadDataFromNode(scene, scene->mRootNode, m_mesh.get());

        if (m_skeleton)
        {
            DLOG("Root bone: " << m_skeleton->GetRootBone()->GetName());
            m_mesh->SetSkeleton(m_skeleton);
        }

        /*for (const auto& subMesh : mesh->GetSubMeshes())
        {
            if (!subMesh->useSharedVertices)
            {
	            if (VertexDeclaration* newDcl = subMesh->vertexData->vertexDeclaration->GetAutoOrganizedDeclaration(
		            mesh->HasSkeleton(), false, false); *newDcl != *(subMesh->vertexData->vertexDeclaration))
                {
                    subMesh->vertexData->ReorganizeBuffers(*newDcl);
                }
            }
        }*/

        // Create the file name
        if (!SaveMeshFile(filenameWithoutExtension.string(), currentAssetPath))
        {
            ELOG("Failed to save mesh file!");
            return false;
        }

        // Serialize skeleton
        if (m_skeleton)
        {
            if (!SaveSkeletonFile(filenameWithoutExtension.string(), currentAssetPath))
            {
                ELOG("Failed to save skeleton file!");
                return false;
            }
        }

        mBonesByName.clear();
        mBoneNodesByName.clear();
        boneMap.clear();
        m_skeleton.reset();

		return true;
	}

	bool FbxImport::SupportsExtension(const String& extension) const noexcept
	{
		return extension == ".fbx";
	}

	bool FbxImport::CreateSubMesh(const String& name, int index, const aiNode* pNode, const aiMesh* mesh,
		const MaterialPtr& material, Mesh* mMesh, AABB& boundingBox) const
	{
        // if animated all submeshes must have bone weights
        if (!mBonesByName.empty() && !mesh->HasBones())
        {
            DLOG("Skipping mesh " << mesh->mName.C_Str() << " with no bone weights");
            return false;
        }

        // now begin the object definition
        // We create a submesh per material
        SubMesh& submesh = mMesh->CreateSubMesh(name + std::to_string(index));
        submesh.useSharedVertices = false;

        submesh.SetMaterial(material);

        // prime pointers to vertex related data
        aiVector3D* vec = mesh->mVertices;
        aiVector3D* norm = mesh->mNormals;
        aiVector3D* binorm = mesh->mBitangents;
        aiVector3D* tang = mesh->mTangents;
        aiVector3D* uv = mesh->mTextureCoords[0];
        aiColor4D* col = mesh->mColors[0];

        // We must create the vertex data, indicating how many vertices there will be
        submesh.vertexData = std::make_unique<VertexData>();
        submesh.vertexData->vertexStart = 0;
        submesh.vertexData->vertexCount = mesh->mNumVertices;

        /*
        switch (mesh->mPrimitiveTypes)
        {
        default:
        case aiPrimitiveType_TRIANGLE:
            submesh.operationType = RenderOperation::OT_TRIANGLE_LIST;
            break;
        case aiPrimitiveType_LINE:
            submesh.operationType = RenderOperation::OT_LINE_LIST;
            break;
        case aiPrimitiveType_POINT:
            submesh.operationType = RenderOperation::OT_POINT_LIST;
            break;
        }
        */

        // We must now declare what the vertex data contains
        VertexDeclaration* declaration = submesh.vertexData->vertexDeclaration;
        static const unsigned short source = 0;
        size_t offset = 0;

        DLOG(mesh->mNumVertices << " vertices");
        offset += declaration->AddElement(source, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
        offset += declaration->AddElement(source, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
        offset += declaration->AddElement(source, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
        offset += declaration->AddElement(source, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
        offset += declaration->AddElement(source, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
        offset += declaration->AddElement(source, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

        Matrix4 aiM = mNodeDerivedTransformByName.find(pNode->mName.data)->second;

        Matrix3 normalMatrix = aiM.Linear().Inverse().Transpose();

        std::vector vertexData(mesh->mNumVertices * (declaration->GetVertexSize(source) / sizeof(float)), 0.0f);
        float* vdata = vertexData.data();

        // Now we get access to the buffer to fill it.  During so we record the bounding box.
        for (size_t i = 0; i < mesh->mNumVertices; ++i)
        {
            // Position
            Vector3 vectorData(vec->x, vec->y, vec->z);
            vectorData = aiM * vectorData;

            *vdata++ = vectorData.x;
            *vdata++ = vectorData.y;
            *vdata++ = vectorData.z;
            boundingBox.Combine(vectorData);
            vec++;

            // Color
            if (col)
            {
	            auto colorValue = reinterpret_cast<uint32*>(vdata++);
                *colorValue = Color(col->r, col->g, col->b, col->a);
            }
            else
            {
                auto colorValue = reinterpret_cast<uint32*>(vdata++);
                *colorValue = Color::White;
            }

            // Normal
            if (norm)
            {
                vectorData = normalMatrix * Vector3(norm->x, norm->y, norm->z);
                vectorData.Normalize();

                *vdata++ = vectorData.x;
                *vdata++ = vectorData.y;
                *vdata++ = vectorData.z;
                norm++;
            }
            else
            {
	            *vdata++ = 0.0f;
				*vdata++ = 1.0f;
				*vdata++ = 0.0f;
            }

            // Binormal
            if (binorm)
            {
                *vdata++ = binorm->x;
                *vdata++ = binorm->y;
                *vdata++ = binorm->z;
                binorm++;
            }
            else
            {
                *vdata++ = 1.0f;
                *vdata++ = 0.0f;
                *vdata++ = 0.0f;
            }

            // Tangent
            if (tang)
            {
                *vdata++ = tang->x;
                *vdata++ = tang->y;
                *vdata++ = tang->z;
                tang++;
            }
            else
            {
                *vdata++ = 0.0f;
                *vdata++ = 0.0f;
                *vdata++ = 1.0f;
            }

            // uvs
            if (uv)
            {
                *vdata++ = uv->x;
                *vdata++ = uv->y;
                uv++;
            }
            else
            {
                *vdata++ = 0.0f;
                *vdata++ = 0.0f;
            }
        }

        VertexBufferPtr buffer = GraphicsDevice::Get().CreateVertexBuffer(
            submesh.vertexData->vertexCount,
            submesh.vertexData->vertexDeclaration->GetVertexSize(source),
            BufferUsage::StaticWriteOnly,
            vertexData.data());
        submesh.vertexData->vertexBufferBinding->SetBinding(source, buffer);

        // set bone weights
        if (mesh->HasBones() && m_skeleton)
        {
            for (uint32 i = 0; i < mesh->mNumBones; i++)
            {
	            if (aiBone* bone = mesh->mBones[i]; nullptr != bone)
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

        if (mesh->mNumFaces == 0)
        {
            return true;
        }

        DLOG(mesh->mNumFaces << " faces");

        aiFace* faces = mesh->mFaces;
        int faceSz = mesh->mPrimitiveTypes == aiPrimitiveType_LINE ? 2 : 3;

        // Creates the index data
        submesh.indexData = std::make_unique<IndexData>();
        submesh.indexData->indexStart = 0;
        submesh.indexData->indexCount = mesh->mNumFaces * faceSz;

        if (mesh->mNumVertices >= 65536) // 32 bit index buffer
        {
            std::vector<uint32> indexDataBuffer(mesh->mNumFaces * faceSz);
            uint32* indexData = indexDataBuffer.data();
            for (size_t i = 0; i < mesh->mNumFaces; ++i)
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
            std::vector<uint16> indexDataBuffer(mesh->mNumFaces* faceSz);
            uint16* indexData = indexDataBuffer.data();
            for (size_t i = 0; i < mesh->mNumFaces; ++i)
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

	void FbxImport::ComputeNodesDerivedTransform(const aiScene* mScene, const aiNode* pNode,
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

	void FbxImport::CreateBonesFromNode(const aiScene* mScene, const aiNode* pNode)
	{
        if (IsNodeNeeded(pNode->mName.data))
        {
            String boneName = pNode->mName.data;
            if (pNode->mNumMeshes == 0)
            {
                Bone* bone = m_skeleton->CreateBone(boneName, msBoneCount);

                aiQuaternion rot;
                aiVector3D pos;
                aiVector3D scale;

                const aiMatrix4x4& aiM = pNode->mTransformation;

                if (!aiM.IsIdentity())
                {
                    aiM.Decompose(scale, rot, pos);
                    bone->SetPosition(Vector3(pos.x, pos.y, pos.z));
                    bone->SetOrientation(Quaternion(rot.w, rot.x, rot.y, rot.z));
                }

                DLOG("(" << msBoneCount << ") Creating bone '" << boneName << "'");
                msBoneCount++;
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
            if (parent && child)
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

	void FbxImport::LoadDataFromNode(const aiScene* mScene, const aiNode* pNode, Mesh* mesh)
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

                CreateSubMesh(pNode->mName.data, idx, pNode, aiMesh, material, mesh, aabb);
            }

            // We must indicate the bounding box
            mesh->SetBounds(aabb);
        }

        // Traverse all child nodes of the current node instance
        for (unsigned int childIdx = 0; childIdx < pNode->mNumChildren; childIdx++)
        {
            const aiNode* pChildNode = pNode->mChildren[childIdx];
            LoadDataFromNode(mScene, pChildNode, mesh);
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
