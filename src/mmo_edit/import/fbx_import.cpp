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
		Assimp::DefaultLogger::get()->attachStream(m_customLogStream.get(), severity);
	}

	FbxImport::~FbxImport()
	{
		Assimp::DefaultLogger::get()->detachStream(m_customLogStream.get());
	}

	bool FbxImport::LoadScene(const String& filename)
	{
		// Remove existing mesh entries
		m_meshEntries.clear();

		Assimp::Importer importer;

		const aiScene* scene = importer.ReadFile(filename.c_str(),
			aiProcess_CalcTangentSpace |
			aiProcess_Triangulate |
			aiProcess_JoinIdenticalVertices |
			aiProcess_SortByPType |
			aiProcess_MakeLeftHanded |
			aiProcess_FlipUVs | 
			aiProcess_LimitBoneWeights | 
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
			m_meshEntries.push_back(entry);
		}

		MeshEntry& entry = m_meshEntries.front();
		uint32 indexOffset = entry.vertices.size();
		uint32 submeshIndexStart = entry.indices.size();

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
				DLOG("\tINDEX: " << face.mIndices[j] << " ( + " << indexOffset << ")");

				entry.indices.push_back(face.mIndices[j] + indexOffset);
				entry.maxIndex = std::max(entry.maxIndex, entry.indices.back());
				indexCount++;
			}
		}

		DLOG("\tSubmesh " << mesh.mMaterialIndex << " has " << indexCount << " indices");

		SubMeshEntry submesh{};
		aiMaterial* material = scene.mMaterials[mesh.mMaterialIndex];
		submesh.material = material->GetName().C_Str();
		submesh.triangleCount = indexCount / 3;
		submesh.indexOffset = submeshIndexStart;
		entry.subMeshes.push_back(submesh);

		if (mesh.mNumBones > 0)
		{
			DLOG("Detected skeletal mesh with " << mesh.mNumBones << " bones");
		}

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

		return SaveMeshFile(filename.filename().replace_extension().string(), currentAssetPath);
	}

	bool FbxImport::SupportsExtension(const String& extension) const noexcept
	{
		return extension == ".fbx";
	}
}
