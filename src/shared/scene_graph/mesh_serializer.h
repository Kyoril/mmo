// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/chunk_reader.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <vector>
#include <span>

#include "mesh.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class Mesh;
	
	namespace mesh_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_1 = 0x0100,
			Version_0_2 = 0x0200
		};	
	}

	typedef mesh_version::Type MeshVersion;
	
	/// Represents a single vertex of a mesh.
	struct Vertex final
	{
		/// Position vector
		Vector3 position{};

		/// Normal vector
		Vector3 normal{};
		
		/// Binormal vector
		Vector3 binormal{};

		/// Tangent vector
		Vector3 tangent{};

		/// Texture coordinates
		Vector3 texCoord{};

		/// Vertex color
		uint32 color { 0xffffffff };

		Vertex() = default;
		~Vertex() = default;

		Vertex(const Vertex& other)
		{
			position = other.position;
			normal = other.normal;
			binormal = other.binormal;
			tangent = other.tangent;
			texCoord = other.texCoord;
			color = other.color;
		}

		Vertex& operator=(const Vertex& other)
		{
			position = other.position;
			normal = other.normal;
			binormal = other.binormal;
			tangent = other.tangent;
			texCoord = other.texCoord;
			color = other.color;
			return *this;
		}
	};
	
	struct SubMeshEntry final
	{
		String material { "Default" };
		uint32 indexOffset { 0 };
		uint32 triangleCount { 0 };

		SubMeshEntry() = default;
		~SubMeshEntry() = default;

		SubMeshEntry(const SubMeshEntry& other)
		{
			material = other.material;
			indexOffset = other.indexOffset;
			triangleCount = other.triangleCount;
		}

		SubMeshEntry& operator=(const SubMeshEntry& other)
		{
			material = other.material;
			indexOffset = other.indexOffset;
			triangleCount = other.triangleCount;
			return *this;
		}
	};

	/// Contains mesh data.
	struct MeshEntry final
	{
		/// Name of the mesh.
		std::string name{};

		std::string skeletonName{};

		/// Vertex data.
		std::vector<Vertex> vertices{};

		/// Index data.
		std::vector<uint32> indices{};

		std::vector<VertexBoneAssignment> boneAssignments{};

		/// Max index to determine whether we can use 16 bit index buffers.
		uint32 maxIndex { 0 };

		std::vector<SubMeshEntry> subMeshes{};

		MeshEntry() = default;
		~MeshEntry() = default;

		MeshEntry(const MeshEntry& other)
		{
			name = other.name;
			maxIndex = other.maxIndex;
			skeletonName = other.skeletonName;

			std::copy(other.vertices.begin(), other.vertices.end(), std::back_inserter(vertices));
			std::copy(other.indices.begin(), other.indices.end(), std::back_inserter(indices));
			std::copy(other.subMeshes.begin(), other.subMeshes.end(), std::back_inserter(subMeshes));
			std::copy(other.boneAssignments.begin(), other.boneAssignments.end(), std::back_inserter(boneAssignments));
		}

		MeshEntry& operator=(const MeshEntry& other)
		{
			vertices.clear();
			indices.clear();
			subMeshes.clear();
			boneAssignments.clear();

			name = other.name;
			maxIndex = other.maxIndex;
			skeletonName = other.skeletonName;

			std::copy(other.vertices.begin(), other.vertices.end(), std::back_inserter(vertices));
			std::copy(other.indices.begin(), other.indices.end(), std::back_inserter(indices));
			std::copy(other.subMeshes.begin(), other.subMeshes.end(), std::back_inserter(subMeshes));
			std::copy(other.boneAssignments.begin(), other.boneAssignments.end(), std::back_inserter(boneAssignments));

			return *this;
		}
	};


	class MeshSerializer
	{
	public:
		void ExportMesh(const MeshEntry& mesh, io::Writer& writer, MeshVersion version = mesh_version::Latest);
	};
	
	/// @brief Implementation of the ChunkReader to read chunked mesh files.
	class MeshDeserializer : public ChunkReader
	{
	public:
		/// @brief Creates a new instance of the MeshDeserializer class and initializes it.
		/// @param mesh The mesh which will be updated by this reader.
		explicit MeshDeserializer(Mesh& mesh);

	protected:
		bool ReadMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadVertexChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadVertexV2Chunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadIndexChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadSubMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadSkeletonChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadBoneChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	protected:
		void CalculateBinormalsAndTangents();
		void CreateHardwareBuffers();

		bool OnReadFinished() noexcept override;

	public:
		const MeshEntry& GetMeshEntry() const noexcept { return m_entry; }

	private:
		MeshVersion m_version;
		MeshEntry m_entry;
		Mesh& m_mesh;
	};
}
