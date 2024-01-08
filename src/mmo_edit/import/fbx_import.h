// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <stack>

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "math/vector3.h"

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags
#include <assimp/LogStream.hpp>

#include <string>
#include <vector>

#include "import_base.h"
#include "scene_graph/mesh_serializer.h"

namespace mmo
{
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

	struct Joint
	{
		Joint* parent = nullptr;
		String name;
		Matrix4 transform;
	};


	struct SceneAnimNode
	{
		std::string m_name;
		SceneAnimNode* m_parent;
		std::vector<SceneAnimNode*> m_children;

		//! most recently calculated local transform
		aiMatrix4x4 m_localTransform;

		//! same, but in world space
		aiMatrix4x4 m_globalTransform;

		//!  index in the current animation's channel array. -1 if not animated.
		int m_channelIndex;

		//! Default construction
		SceneAnimNode()
			: m_name()
			, m_parent(nullptr)
			, m_children()
			, m_localTransform()
			, m_globalTransform()
			, m_channelIndex(-1)
		{
		}

		//! Construction from a given name
		SceneAnimNode(std::string name)
			: m_name(std::move(name))
			, m_parent(nullptr)
			, m_children()
			, m_localTransform()
			, m_globalTransform()
			, m_channelIndex(-1)
		{
		}

		//! Destruct all children recursively
		~SceneAnimNode()
		{
		}
	};

	/// This class can be used to extract relevant informations out of an fbx file.
	class FbxImport final
		: public ImportBase
		, public NonCopyable
	{

		class CustomAssimpLogStream : public Assimp::LogStream
		{
		public:
			virtual ~CustomAssimpLogStream() override = default;

		public:
			void write(const char* message) override;
		};

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

		/// @brief Traverses an FbxNode object and all of it's child objects, loading all relevant
		///	       data like geometry and converts them if supported.
		/// @remark This is recursive method.
		/// @param node The node to start traversing from.
		void TraverseScene(const aiNode& node, const aiScene& scene);

		bool SaveSkeletonFile(const String& filename, const Path& assetPath);

		/// @brief Saves the loaded mesh geometry data into the engine's custom mesh file format.
		/// @param filename The file name of the new mesh file without extension and path.
		/// @param assetPath The asset path, relative to the game content root folder, where the file will be located.
		/// @return true on success, false on errors. Errors will produce error logs using ELOG.
		bool SaveMeshFile(const String& filename, const Path& assetPath) const;

		/// @brief Loads a mesh node. Called by TraverseScene when it found a node containing a mesh.
		/// @param node The node.
		/// @param mesh The mesh.
		/// @return true on success, false on error.
		bool LoadMesh(const aiScene& scene, const aiNode& node, const aiMesh& mesh);

	public:
		/// @copydoc ImportBase::ImportFromFile
		bool ImportFromFile(const Path& filename, const Path& currentAssetPath) override;
		
		/// @copydoc ImportBase::SupportsExtension
		[[nodiscard]] bool SupportsExtension(const String& extension) const noexcept override;

	private:
		const aiMatrix4x4& GetLocalTransform(const aiNode* node) const;

		const aiMatrix4x4& GetGlobalTransform(const aiNode* node) const;

		const std::vector<aiMatrix4x4>& GetBoneMatrices(const aiMesh* mesh, const aiNode* pNode, size_t pMeshIndex);

		static void CalculateGlobalTransform(SceneAnimNode& internalNode);

		SceneAnimNode* CreateNodeTree(const aiNode* node, SceneAnimNode* parent);

	private:
		std::vector<MeshEntry> m_meshEntries;

		typedef std::map<const aiNode*, std::unique_ptr<SceneAnimNode>> NodeMap;
		NodeMap m_nodesByName;

		/** Name to node map to quickly find nodes for given bones by their name */
		typedef std::map<const char*, const aiNode*> BoneMap;
		BoneMap m_boneNodesByName;

		/** Array to return transformations results inside. */
		std::vector<aiMatrix4x4> m_transforms;

		std::unique_ptr<CustomAssimpLogStream> m_customLogStream;

		SkeletonPtr m_skeleton{nullptr};
	};
}
