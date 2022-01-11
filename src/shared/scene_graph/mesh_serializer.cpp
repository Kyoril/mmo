// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "mesh_serializer.h"
#include "mesh.h"

#include "base/macros.h"
#include "binary_io/writer.h"
#include "mesh/pre_header.h"

#include "mesh/chunk_writer.h"
#include "mesh_v1_0/header.h"
#include "mesh_v1_0/header_save.h"

namespace mmo
{
	void MeshSerializer::ExportMesh(const Mesh& mesh, io::Writer& writer, mesh::VersionId version)
	{
		ASSERT(!mesh.GetBounds().IsNull());
		ASSERT(mesh.GetBoundRadius() != 0.0f);

		if (version == mesh::Latest)
		{
			version = mesh::Version_1_0;
		}
		
		// Write file header
		mesh::v1_0::Header header;
		header.version = mesh::Version_1_0;
		header.vertexChunkOffset = 0;
		header.indexChunkOffset = 0;
		mesh::v1_0::HeaderSaver saver { writer.Sink(), header };

		{
			ChunkWriter meshChunkWriter { mesh::v1_0::MeshChunkMagic, writer };

			// Whether this mesh has a skeleton link
			writer << io::write<uint8>(mesh.HasSkeleton());

			// Whether this mesh has shared geometry
			writer << io::write<uint32>(mesh.m_vertexBuffer->GetVertexCount());


			meshChunkWriter.Finish();
		}

		// Finalize file header
		saver.Finish();
	}
}
