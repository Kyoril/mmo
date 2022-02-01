// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/chunk_reader.h"
#include "base/typedefs.h"

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


	class MeshSerializer
	{
	public:
		void ExportMesh(const Mesh& mesh, io::Writer& writer, mesh::VersionId version = mesh::Latest);
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
		
	private:
		Mesh& m_mesh;
	};
}