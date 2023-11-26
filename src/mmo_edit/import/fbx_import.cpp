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
#include "scene_graph/skeleton_mgr.h"
#include "scene_graph/skeleton_serializer.h"


#ifdef IOS_REF
#	undef  IOS_REF
#	define IOS_REF (*(m_sdkManager->GetIOSettings()))
#endif

namespace mmo
{
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

	bool FbxImport::LoadScene(const String& filename)
	{
		// Remove existing mesh entries
		m_meshEntries.clear();

		Assimp::Importer importer;

		importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

		const aiScene* scene = importer.ReadFile(filename.c_str(),
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
			return false;
		}

		TraverseScene(*scene->mRootNode, *scene);

		return true;
	}

	void FbxImport::TraverseScene(const aiNode& node, const aiScene& scene)
	{
		for (uint32 i = 0; i < node.mNumMeshes; ++i)
		{
			const aiMesh* mesh = scene.mMeshes[node.mMeshes[i]];
			if (!LoadMesh(scene, node, *mesh))
			{
				ELOG("Failed to load mesh!");
			}
		}

		for (uint32 i = 0; i < node.mNumChildren; ++i)
		{
			TraverseScene(*node.mChildren[i], scene);
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
		serializer.ExportMesh(m_meshEntries.front(), writer);

		return true;
	}

	bool FbxImport::LoadMesh(const aiScene& scene, const aiNode& node, const aiMesh& mesh)
	{
		ILOG("Processing mesh node " << node.mName.C_Str() << "...");

		if (!mesh.HasTangentsAndBitangents())
		{
			WLOG("Mesh is missing tangent info");
		}

		Matrix4 transformation = Matrix4(
			&node.mTransformation.a1
		);

		aiNode* parent = node.mParent;
		while (parent)
		{
			transformation = transformation * &parent->mTransformation.a1;
			parent = parent->mParent;
		}

		// Create a new mesh entry
		if (m_meshEntries.empty())
		{
			MeshEntry entry;
			entry.name = node.mName.C_Str();
			entry.maxIndex = 0;

			DLOG("Mesh has " << scene.mNumMaterials << " submeshes");
			if (mesh.HasBones())
			{
				m_skeleton = std::make_shared<Skeleton>("Import");

				CreateNodeTree(scene.mRootNode, nullptr);

				// Initialize bone node map
				for (unsigned int n = 0; n < mesh.mNumBones; ++n)
				{
					const aiBone* bone = mesh.mBones[n];
					m_boneNodesByName[bone->mName.data] = scene.mRootNode->FindNode(bone->mName);
				}

				// Calculate bone transforms
				auto& matrices = GetBoneMatrices(&mesh, &node, 0);

				// First load all joints and their respective vertex assignment info
				DLOG("Detected skeletal mesh with " << mesh.mNumBones << " bones");
				for (uint32 i = 0; i < mesh.mNumBones; ++i)
				{
					aiNode* boneNode = scene.mRootNode->FindNode(mesh.mBones[i]->mName.C_Str());
					if (boneNode)
					{
						const auto boneTrans = Matrix4(&GetLocalTransform(boneNode)/*matrices[i]*/.a1);

						Bone* bone = m_skeleton->CreateBone(mesh.mBones[i]->mName.C_Str());
						ASSERT(bone);
						bone->SetPosition(boneTrans.GetTrans());
						bone->SetOrientation(boneTrans.ExtractQuaternion());
						bone->SetScale(boneTrans.GetScale());
						bone->SetBindingPose();

						for (uint32 j = 0; j < mesh.mBones[i]->mNumWeights; ++j)
						{
							VertexBoneAssignment assignment{};
							assignment.boneIndex = i;
							assignment.vertexIndex = mesh.mBones[i]->mWeights[j].mVertexId;
							assignment.weight = mesh.mBones[i]->mWeights[j].mWeight;
							entry.boneAssignments.push_back(assignment);
						}
					}
				}

				// Resolve hierarchy in a second pass to know which joint is parented to which joint
				for (uint16 i = 0; i < m_skeleton->GetNumBones(); ++i)
				{
					Bone* childBone = m_skeleton->GetBone(i);

					aiNode* node = scene.mRootNode->FindNode(childBone->GetName().c_str());
					if (node != nullptr)
					{
						aiNode* parent = node->mParent;
						while(parent)
						{
							Bone* parentBone = m_skeleton->GetBone(parent->mName.C_Str());
							if (childBone && parentBone)
							{
								parentBone->AddChild(*childBone);
								break;
							}

							parent = parent->mParent;
						}
					}
				}

				// The root bone of the skeleton should use the global transform from the file to apply global transformations correctly
				Bone* root = m_skeleton->GetRootBone();
				if (root)
				{
					const auto rootTransform = Matrix4(&GetGlobalTransform(scene.mRootNode->FindNode(root->GetName().c_str())).a1);
					root->SetPosition(rootTransform.GetTrans());
					root->SetOrientation(rootTransform.ExtractQuaternion());
					root->SetScale(rootTransform.GetScale());
					root->SetBindingPose();
				}
			}

			m_meshEntries.push_back(entry);
		}

		MeshEntry& entry = m_meshEntries.front();
		uint32 indexOffset = entry.vertices.size();
		uint32 subMeshIndexStart = entry.indices.size();

		// Build vertex data
		DLOG("\tSubmesh " << mesh.mMaterialIndex << " has " << mesh.mNumVertices << " vertices");
		const uint32 startVertex = entry.vertices.size();
		const uint32 endVertex = startVertex + mesh.mNumVertices;
		for (uint32 i = startVertex; i < endVertex; ++i)
		{
			const uint32 j = i - startVertex;

			entry.vertices.emplace_back();

			if (mesh.HasPositions())
			{
				entry.vertices[i].position = transformation * Vector3(
					mesh.mVertices[j].x, mesh.mVertices[j].y, mesh.mVertices[j].z);
			}
			else
			{
				entry.vertices[i].position = Vector3::Zero;
			}
			
			entry.vertices[i].color = 0xFFFFFFFF;
			if (mesh.mColors[0])
			{
				entry.vertices[i].color =
					Color(mesh.mColors[0][j].r, mesh.mColors[0][j].g, mesh.mColors[0][j].b, mesh.mColors[0][j].a);
			}

			if (mesh.HasNormals())
			{
				entry.vertices[i].normal = (transformation * Vector3(
					mesh.mNormals[j].x, mesh.mNormals[j].y, mesh.mNormals[j].z)).NormalizedCopy();
			}
			else
			{
				entry.vertices[i].normal = Vector3::UnitY;
			}

			if (mesh.HasTangentsAndBitangents())
			{
				entry.vertices[i].tangent = (transformation * Vector3(
					mesh.mTangents[j].x, mesh.mTangents[j].y, mesh.mTangents[j].z)).NormalizedCopy();

				entry.vertices[i].binormal = (transformation * Vector3(
					mesh.mBitangents[j].x, mesh.mBitangents[j].y, mesh.mBitangents[j].z)).NormalizedCopy();
			}
			else
			{
				entry.vertices[i].tangent = Vector3::UnitX;
				entry.vertices[i].binormal = Vector3::UnitZ;
			}
			
			if (mesh.mTextureCoords[0])
			{
				entry.vertices[i].texCoord = Vector3(
					mesh.mTextureCoords[0][j].x, mesh.mTextureCoords[0][j].y, mesh.mTextureCoords[0][j].z);
			}
			else
			{
				entry.vertices[i].texCoord = Vector3::Zero;
			}
		}

		// Build index data
		uint32 indexCount = 0;
		DLOG("\tSubmesh " << mesh.mMaterialIndex << " has " << mesh.mNumFaces << " faces");
		for (uint32 i = 0; i < mesh.mNumFaces; ++i)
		{
			aiFace face = mesh.mFaces[i];

			for (uint32 j = 0; j < face.mNumIndices; ++j)
			{
				entry.indices.push_back(face.mIndices[j] + indexOffset);
				entry.maxIndex = std::max(entry.maxIndex, entry.indices.back());
				indexCount++;
			}
		}

		DLOG("\tSubmesh " << mesh.mMaterialIndex << " has " << indexCount << " indices");

		SubMeshEntry subMesh{};
		aiMaterial* material = scene.mMaterials[mesh.mMaterialIndex];
		subMesh.material = material->GetName().C_Str();
		subMesh.triangleCount = indexCount / 3;
		subMesh.indexOffset = subMeshIndexStart;
		entry.subMeshes.push_back(subMesh);
		return true;
	}

	bool FbxImport::ImportFromFile(const Path& filename, const Path& currentAssetPath)
	{
		if (!LoadScene(filename.string()))
		{
			return false;
		}

		// TODO: Change this, but for now we will create a vertex and index buffer from the first mesh that was found
		if (m_meshEntries.empty())
		{
			WLOG("FBX has no geometry data!");
			return false;
		}

		const auto filenameWithoutExtension = filename.filename().replace_extension();
		if (!m_meshEntries.front().boneAssignments.empty())
		{
			std::string skeletonName = (currentAssetPath / filenameWithoutExtension).string();
			std::ranges::replace(skeletonName, '\\', '/');

			m_meshEntries.front().skeletonName = skeletonName;
		}

		if (!SaveMeshFile(filenameWithoutExtension.string(), currentAssetPath))
		{
			ELOG("Failed to save mesh file!");
			return false;
		}

		if (m_skeleton)
		{
			return SaveSkeletonFile(filenameWithoutExtension.string(), currentAssetPath);
		}

		return true;
	}

	bool FbxImport::SupportsExtension(const String& extension) const noexcept
	{
		return extension == ".fbx";
	}

	const aiMatrix4x4 IdentityMatrix;

	const aiMatrix4x4& FbxImport::GetLocalTransform(const aiNode* node) const
	{
		NodeMap::const_iterator it = m_nodesByName.find(node);
		if (it == m_nodesByName.end()) {
			return IdentityMatrix;
		}

		return it->second->m_localTransform;
	}

	const aiMatrix4x4& FbxImport::GetGlobalTransform(const aiNode* node) const
	{
		NodeMap::const_iterator it = m_nodesByName.find(node);
		if (it == m_nodesByName.end()) {
			return IdentityMatrix;
		}

		return it->second->m_globalTransform;
	}

	const std::vector<aiMatrix4x4>& FbxImport::GetBoneMatrices(const aiMesh* mesh, const aiNode* pNode, size_t pMeshIndex /* = 0 */)
	{
		// resize array and initialize it with identity matrices
		m_transforms.resize(mesh->mNumBones, aiMatrix4x4());

		// calculate the mesh's inverse global transform
		aiMatrix4x4 globalInverseMeshTransform = GetGlobalTransform(pNode);
		globalInverseMeshTransform.Inverse();

		// Bone matrices transform from mesh coordinates in bind pose to mesh coordinates in skinned pose
		// Therefore the formula is offsetMatrix * currentGlobalTransform * inverseCurrentMeshTransform
		for (size_t a = 0; a < mesh->mNumBones; ++a)
		{
			const aiBone* bone = mesh->mBones[a];
			const aiMatrix4x4& currentGlobalTransform = GetGlobalTransform(m_boneNodesByName[bone->mName.data]);
			m_transforms[a] = globalInverseMeshTransform * currentGlobalTransform * bone->mOffsetMatrix;
		}

		// and return the result
		return m_transforms;
	}

	void FbxImport::CalculateGlobalTransform(SceneAnimNode& internalNode)
	{
		// concatenate all parent transforms to get the global transform for this node
		internalNode.m_globalTransform = internalNode.m_localTransform;
		const SceneAnimNode* node = internalNode.m_parent;
		while (node)
		{
			internalNode.m_globalTransform = node->m_localTransform * internalNode.m_globalTransform;
			node = node->m_parent;
		}
	}

	SceneAnimNode* FbxImport::CreateNodeTree(const aiNode* node, SceneAnimNode* parent)
	{
		// create a node
		auto internalNode = std::make_unique<SceneAnimNode>(node->mName.data);
		internalNode->m_parent = parent;

		// copy its transformation
		internalNode->m_localTransform = node->mTransformation;
		CalculateGlobalTransform(*internalNode);

		// continue for all child nodes and assign the created internal nodes as our children
		for (unsigned int a = 0; a < node->mNumChildren; ++a)
		{
			SceneAnimNode* childNode = CreateNodeTree(node->mChildren[a], internalNode.get());
			internalNode->m_children.push_back(childNode);
		}

		return (m_nodesByName[node] = std::move(internalNode)).get();
	}
}
