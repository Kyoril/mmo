// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "math/vector3.h"

#include "fbxsdk.h"

#include <string>
#include <vector>

namespace mmo
{
	/// Represents a single vertex of a mesh.
	struct Vertex
	{
		/// Position vector
		Vector3 position;
		/// Normal vector
		Vector3 normal;
		/// Texture coordinates
		Vector3 texCoord;
		/// Vertex color
		uint32 color;
	};

	/// Contains mesh data.
	struct MeshEntry
	{
		/// Name of the mesh.
		std::string name;
		/// Vertex data.
		std::vector<Vertex> vertices;
		/// Index data.
		std::vector<uint32> indices;
		/// Max index to determine whether we can use 16 bit index buffers.
		uint32 maxIndex;
	};

	/// This class can be used to extract relevant informations out of an fbx file.
	class FbxImport final
		: public NonCopyable
	{
	public:
		explicit FbxImport();
		~FbxImport();

	public:
		bool LoadScene(const char* pFilename);

		inline const std::vector<MeshEntry>& GetMeshEntries() const noexcept { return m_meshEntries; }

	private:
		void InitializeSdkObjects();
		void DestroySdkObjects();

		void TraverseScene(FbxNode& node);

	private:
		/// A list of mesh entries found in the fbx scene.
		std::vector<MeshEntry> m_meshEntries;
		FbxManager* m_sdkManager = nullptr;
		FbxScene* m_scene = nullptr;
	};
}