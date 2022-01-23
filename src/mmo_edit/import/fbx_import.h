// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "math/vector3.h"

#include "fbxsdk.h"

#include <string>
#include <vector>

#include "import_base.h"

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

	struct MeshUvSet
	{
		std::vector<Vector3> uvs;
	};
	
	struct MeshGeometry
	{
		std::vector<Vector3> positions;
		std::vector<MeshUvSet> uvSets;
		std::vector<Vector3> normals;
		std::vector<uint32> colors;
		std::vector<int32> polygonIndices;
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
		: public ImportBase
		, public NonCopyable
	{
	public:
		/// @brief Creates a new instance of the FbxImport class and initializes it.
		explicit FbxImport();

		/// @brief Destroys an instance of the FbxImport class and performs cleanup.
		~FbxImport() override;
		
	private:
		/// @brief Loads The given FBX file from disk.
		/// @param filename Full path filename of the fbx file to load.
		/// @return true on success, false otherwise.
		bool LoadScene(const String& filename);

		/// @brief Initializes the fbx sdk objects to work with the sdk.
		void InitializeSdkObjects();

		/// @brief Cleanup. Should be called after InitializeSdkObjects.
		void DestroySdkObjects();

		/// @brief Traverses an FbxNode object and all of it's child objects, loading all relevant
		///	       data like geometry and converts them if supported.
		/// @remark This is recursive method.
		/// @param node The node to start traversing from.
		void TraverseScene(FbxNode& node);

		/// @brief Saves the loaded mesh geometry data into the engine's custom mesh file format.
		/// @param filename The file name of the new mesh file without extension and path.
		/// @param assetPath The asset path, relative to the game content root folder, where the file will be located.
		/// @return true on success, false on errors. Errors will produce error logs using ELOG.
		bool SaveMeshFile(const String& filename, const Path& assetPath) const;

		/// @brief Loads a mesh node. Called by TraverseScene when it found a node containing a mesh.
		/// @param node The node.
		/// @param mesh The mesh.
		/// @return true on success, false on error.
		bool LoadMesh(FbxNode& node, FbxMesh& mesh);

	private:
		bool InitializeUvSets(FbxMesh& mesh, MeshGeometry& geometry);

		bool LoadMeshVertexPositions(FbxNode& node, FbxMesh& mesh, MeshGeometry& geometry);
		
		bool LoadMeshPolygons(FbxMesh& mesh, MeshGeometry& geometry);

		void GenerateMeshEntry(MeshEntry& entry, const MeshGeometry& geometry);

		void LoadMeshUvs(FbxMesh& mesh, MeshGeometry& geometry);

		void LoadMeshNormals(FbxNode& node, FbxMesh& mesh, MeshGeometry& geometry);


	public:
		/// @copydoc ImportBase::ImportFromFile
		bool ImportFromFile(const Path& filename, const Path& currentAssetPath) override;
		
		/// @copydoc ImportBase::SupportsExtension
		[[nodiscard]] bool SupportsExtension(const String& extension) const noexcept override;

	private:
		std::vector<MeshEntry> m_meshEntries;
		FbxManager* m_sdkManager = nullptr;
		FbxScene* m_scene = nullptr;
	};
}
