// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
		bool SaveSkeletonFile(const String& filename, const Path& assetPath);

		/// @brief Saves the loaded mesh geometry data into the engine's custom mesh file format.
		/// @param filename The file name of the new mesh file without extension and path.
		/// @param assetPath The asset path, relative to the game content root folder, where the file will be located.
		/// @return true on success, false on errors. Errors will produce error logs using ELOG.
		bool SaveMeshFile(const String& filename, const Path& assetPath) const;

	public:
		/// @copydoc ImportBase::ImportFromFile
		bool ImportFromFile(const Path& filename, const Path& currentAssetPath) override;
		
		/// @copydoc ImportBase::SupportsExtension
		[[nodiscard]] bool SupportsExtension(const String& extension) const noexcept override;

	private:
		bool CreateSubMesh(const String& name, int index, const aiNode* pNode, const aiMesh* aiMesh, const MaterialPtr& material, Mesh* mesh, AABB& boundingBox, const Matrix4& transform) const;
		void GrabNodeNamesFromNode(const aiScene* mScene, const aiNode* pNode);
		void GrabBoneNamesFromNode(const aiScene* mScene, const aiNode* pNode);
		void ComputeNodesDerivedTransform(const aiScene* mScene, const aiNode* pNode, const aiMatrix4x4& accTransform);
		void CreateBonesFromNode(const aiScene* mScene, const aiNode* pNode);
		void CreateBoneHierarchy(const aiScene* mScene, const aiNode* pNode);
		void LoadDataFromNode(const aiScene* mScene, const aiNode* pNode, Mesh* mesh, const Matrix4& transform);
		void MarkAllChildNodesAsNeeded(const aiNode* pNode);
		void FlagNodeAsNeeded(const char* name);
		bool IsNodeNeeded(const char* name);

		std::unique_ptr<CustomAssimpLogStream> m_customLogStream;

		typedef std::map<String, bool> boneMapType;
		boneMapType boneMap;
		int mLoaderParams;

		String mCustomAnimationName;

		typedef std::map<String, const aiNode*> BoneNodeMap;
		BoneNodeMap mBoneNodesByName;

		typedef std::map<String, const aiBone*> BoneMap;
		BoneMap mBonesByName;

		typedef std::map<String, Matrix4> NodeTransformMap;
		NodeTransformMap mNodeDerivedTransformByName;

		MeshPtr m_mesh;
		SkeletonPtr m_skeleton;

		static int msBoneCount;

		float mTicksPerSecond;
		float mAnimationSpeedModifier;
	};
}
