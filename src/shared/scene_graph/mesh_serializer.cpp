// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "mesh_serializer.h"

#include "material_manager.h"
#include "mesh.h"

#include "base/macros.h"
#include "binary_io/writer.h"
#include "mesh/pre_header.h"

#include "base/chunk_writer.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "mesh_v1_0/header.h"
#include "mesh_v1_0/header_save.h"

namespace mmo
{
	static const ChunkMagic MeshChunkMagic = { {'M', 'E', 'S', 'H'} };
	static const ChunkMagic MeshVertexChunk= { {'V', 'E', 'R', 'T'} };
	static const ChunkMagic MeshIndexChunk= { {'I', 'N', 'D', 'X'} };
	static const ChunkMagic MeshSubMeshChunk = { {'S', 'U', 'B', 'M'} };
	
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

	MeshDeserializer::MeshDeserializer(Mesh& mesh)
		: m_mesh(mesh)
	{
		AddChunkHandler(*MeshChunkMagic, true, *this, &MeshDeserializer::ReadMeshChunk);
	}

	bool MeshDeserializer::ReadMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint32 version;
		reader >> io::read<uint32>(version);

		if (reader)
		{
			if (version == mesh_version::Version_0_1)
			{
				AddChunkHandler(*MeshVertexChunk, true, *this, &MeshDeserializer::ReadVertexChunk);
				AddChunkHandler(*MeshIndexChunk, true, *this, &MeshDeserializer::ReadIndexChunk);
				AddChunkHandler(*MeshSubMeshChunk, true, *this, &MeshDeserializer::ReadSubMeshChunk);
			}
			else
			{
				ELOG("Unknown mesh version!");
				return false;
			}
		}

		return reader;
	}

	bool MeshDeserializer::ReadVertexChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint32 vertexCount;
		reader >> io::read<uint32>(vertexCount);
		
		// Read vertex data
		std::vector<POS_COL_NORMAL_TEX_VERTEX> vertices;
		vertices.resize(vertexCount);
		
		AABB boundingBox;
		
		Vector3 min = Vector3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		Vector3 max = Vector3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

		// Iterate through vertices
		for (POS_COL_NORMAL_TEX_VERTEX& v : vertices)
		{
			reader
				>> io::read<float>(v.pos[0])
				>> io::read<float>(v.pos[1])
				>> io::read<float>(v.pos[2]);

			min = TakeMinimum(v.pos, min);
			max = TakeMaximum(v.pos, max);

			// Color
			reader
				>> io::read<uint32>(v.color);

			// Uvw
			reader
				>> io::read<float>(v.uv[0])
				>> io::read<float>(v.uv[1])
				>> io::skip<float>();

			// Normal
			reader
				>> io::read<float>(v.normal[0])
				>> io::read<float>(v.normal[1])
				>> io::read<float>(v.normal[2]);
		}

		const AABB box { min, max };
		boundingBox.Combine(box);

		m_mesh.SetBounds(boundingBox);

		// Create vertex buffer
		m_mesh.m_vertexBuffer = GraphicsDevice::Get().CreateVertexBuffer(vertexCount, sizeof(POS_COL_NORMAL_TEX_VERTEX), false, &vertices[0]);
		
		return reader;
	}

	bool MeshDeserializer::ReadIndexChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint32 indexCount = 0;
		bool use16BitIndices = false;
		if (!(reader
			>> io::read<uint32>(indexCount)
			>> io::read<uint8>(use16BitIndices)))
		{
			return false;
		}

		if (use16BitIndices)
		{
			std::vector<uint16> indices;
			indices.resize(indexCount);

			for (uint16& index : indices)
			{
				reader >> io::read<uint16>(index);
			}

			m_mesh.m_indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(indexCount, IndexBufferSize::Index_16, &indices[0]);
		}
		else
		{
			std::vector<uint32> indices;
			indices.resize(indexCount);

			for (uint32& index : indices)
			{
				reader >> io::read<uint32>(index);
			}

			m_mesh.m_indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(indexCount, IndexBufferSize::Index_32, &indices[0]);
		}
				
		return reader;
	}

	bool MeshDeserializer::ReadSubMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		String materialName;
		reader >> io::read_container<uint16>(materialName);

		uint32 indexStart = 0, indexEnd = 0;
		reader >> io::read<uint32>(indexStart) >> io::read<uint32>(indexEnd);

		auto& subMesh = m_mesh.CreateSubMesh();

		subMesh.SetMaterial(MaterialManager::Get().Load(materialName));
		subMesh.m_useSharedVertices = true;
		subMesh.m_indexStart = indexStart;
		subMesh.m_indexEnd = indexEnd;
		
		return reader;
	}
}
