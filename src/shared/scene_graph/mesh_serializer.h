// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <span>

#include "base/chunk_reader.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include "mesh/magic.h"

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

			Version_0_1 = 0x0100
		};	
	}

	typedef mesh_version::Type MeshVersion;
	
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
		uint32 color { 0xffffffff };
	};
	
	struct SubMeshEntry
	{
		String material { "Default" };
		uint32 indexOffset { 0 };
		uint32 triangleCount { 0 };
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
		uint32 maxIndex { 0 };

		std::vector<SubMeshEntry> subMeshes;
	};


	class MeshSerializer
	{
	public:
		void ExportMesh(const MeshEntry& mesh, io::Writer& writer, mesh::VersionId version = mesh::Latest);
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
		bool ReadIndexChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadSubMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		
	public:
		const MeshEntry& GetMeshEntry() const noexcept { return m_entry; }

	private:
		MeshEntry m_entry;
		Mesh& m_mesh;
	};
}