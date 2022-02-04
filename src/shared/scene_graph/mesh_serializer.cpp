// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "mesh_serializer.h"

#include "material_manager.h"
#include "mesh.h"

#include "binary_io/writer.h"
#include "mesh/pre_header.h"

#include "base/chunk_writer.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "mesh_v1_0/header.h"

namespace mmo
{
	static const ChunkMagic MeshChunkMagic = { {'M', 'E', 'S', 'H'} };
	static const ChunkMagic MeshVertexChunk= { {'V', 'E', 'R', 'T'} };
	static const ChunkMagic MeshIndexChunk= { {'I', 'N', 'D', 'X'} };
	static const ChunkMagic MeshSubMeshChunk = { {'S', 'U', 'B', 'M'} };
	
	void MeshSerializer::ExportMesh(const MeshEntry& mesh, io::Writer& writer, mesh::VersionId version)
	{
		if (version == mesh::Latest)
		{
			version = mesh::Version_1_0;
		}
		
		// Write the vertex chunk data
		ChunkWriter meshChunk{ mesh::v1_0::MeshChunkMagic, writer };
		{
			writer << io::write<uint32>(version);
		}
		meshChunk.Finish();
		
		// Write the vertex chunk data
		ChunkWriter vertexChunkWriter{ mesh::v1_0::MeshVertexChunk, writer };
		{
			writer << io::write<uint32>(mesh.vertices.size());
			for (size_t i = 0; i < mesh.vertices.size(); ++i)
			{
				writer
					<< io::write<float>(mesh.vertices[i].position.x)
					<< io::write<float>(mesh.vertices[i].position.y)
					<< io::write<float>(mesh.vertices[i].position.z);
				writer
					<< io::write<uint32>(mesh.vertices[i].color);
				writer
					<< io::write<float>(mesh.vertices[i].texCoord.x)
					<< io::write<float>(mesh.vertices[i].texCoord.y)
					<< io::write<float>(mesh.vertices[i].texCoord.z);
				writer
					<< io::write<float>(mesh.vertices[i].normal.x)
					<< io::write<float>(mesh.vertices[i].normal.y)
					<< io::write<float>(mesh.vertices[i].normal.z);
			}
		}
		vertexChunkWriter.Finish();

		// Write the index chunk data
		ChunkWriter indexChunkWriter{ mesh::v1_0::MeshIndexChunk, writer };
		{
			const bool bUse16BitIndices = mesh.vertices.size() <= std::numeric_limits<uint16>().max();
			
			writer
				<< io::write<uint32>(mesh.indices.size())
				<< io::write<uint8>(bUse16BitIndices);

			for (size_t i = 0; i < mesh.indices.size(); ++i)
			{
				if (bUse16BitIndices)
				{
					writer << io::write<uint16>(mesh.indices[i]);
				}
				else
				{
					writer << io::write<uint32>(mesh.indices[i]);
				}
			}
		}
		indexChunkWriter.Finish();

		// Write submesh chunks
		for(const auto& submesh : mesh.subMeshes)
		{
			ChunkWriter submeshChunkWriter{ mesh::v1_0::MeshSubMeshChunk, writer };
			{
				// Material name
				writer
					<< io::write_dynamic_range<uint16>(submesh.material);

				// Start index & end index
				writer
					<< io::write<uint32>(submesh.indexOffset)
					<< io::write<uint32>(submesh.indexOffset + submesh.triangleCount * 3);
			}
			submeshChunkWriter.Finish();
		}
		
	}

	MeshDeserializer::MeshDeserializer(Mesh& mesh)
		: m_mesh(mesh)
	{
		m_entry = MeshEntry{};

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
		
		m_entry.vertices.clear();
		m_entry.vertices.reserve(vertexCount);

		// Read vertex data
		std::vector<POS_COL_NORMAL_TEX_VERTEX> vertices;
		vertices.resize(vertexCount);
		
		AABB boundingBox;

		auto min = Vector3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		auto max = Vector3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

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
			
            m_entry.vertices.emplace_back(Vertex{v.pos, v.normal, Vector3( v.uv[0], v.uv[1], 0.0f ), v.color});
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

		m_entry.indices.clear();
		m_entry.indices.reserve(indexCount);

		if (use16BitIndices)
		{
			std::vector<uint16> indices;
			indices.resize(indexCount);

			for (uint16& index : indices)
			{
				reader >> io::read<uint16>(index);
				m_entry.indices.emplace_back(index);
				m_entry.maxIndex = std::max<uint32>(m_entry.maxIndex, index);
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
				m_entry.indices.emplace_back(index);
				m_entry.maxIndex = std::max(m_entry.maxIndex, index);
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

        m_entry.subMeshes.emplace_back(SubMeshEntry{std::move(materialName), indexStart, (indexEnd - indexStart) / 3});
		
		return reader;
	}
}
