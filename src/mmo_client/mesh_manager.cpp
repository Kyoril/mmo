// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "mesh_manager.h"

#include "assets/asset_registry.h"
#include "mesh/pre_header.h"
#include "mesh/pre_header_load.h"
#include "mesh_v1_0/header.h"
#include "mesh_v1_0/header_load.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "base/macros.h"
#include "log/default_log_levels.h"
#include "graphics/graphics_device.h"
#include "mesh/chunk_writer.h"


namespace mmo
{
	MeshManager & MeshManager::Get()
	{
		static MeshManager instance;
		return instance;
	}

	MeshPtr MeshManager::Load(const std::string & filename)
	{
		// Try to find the mesh first before trying to load it again
		const auto it = m_meshes.find(filename);
		if (it != m_meshes.end())
		{
			return it->second;
		}

		// Try to load the file from the registry
		const auto filePtr = AssetRegistry::OpenFile(filename);
		ASSERT(filePtr && "Unable to load mesh file");

		// Create readers
		io::StreamSource source{ *filePtr };
		io::Reader reader{ source };

		// Load mesh pre header to determine file format version etc.
		mesh::PreHeader preHeader;
		if (!mesh::LoadPreHeader(preHeader, reader))
		{
			ELOG("Failed to load mesh pre header of file " << filename);
			return nullptr;
		}

		// Create the resulting mesh
		auto mesh = std::make_shared<Mesh>();

		// Depending on the format version, load the real mesh data now
		switch (preHeader.version)
		{
		case mesh::Version_1_0:
			{
				mesh::v1_0::Header header;
				if (!mesh::v1_0::LoadHeader(header, reader))
				{
					ELOG("Failed to load mesh header of file " << filename);
					return nullptr;
				}

				// Create a submesh
				SubMesh& submesh = mesh->CreateSubMesh("Default");
				submesh.m_useSharedVertices = false;

				// Load vertex chunk
				source.seek(header.vertexChunkOffset);
				ASSERT(reader);
				{
					// Read chunk magic
					ChunkMagic vertexMagic;
					source.read(&vertexMagic[0], vertexMagic.size());
					ASSERT(reader);
					ASSERT(vertexMagic == mesh::v1_0::VertexChunkMagic);

					// Read chunk size
					uint32 chunkSize = 0;
					reader >> io::read<uint32>(chunkSize);
					ASSERT(reader);

					uint32 vertexCount = 0;
					reader >> io::read<uint32>(vertexCount);

					// Read vertex data
					std::vector<POS_COL_VERTEX> vertices;
					vertices.resize(vertexCount);

					// Iterate through vertices
					for (POS_COL_VERTEX& v : vertices)
					{
						reader
							>> io::read<float>(v.pos[0])
							>> io::read<float>(v.pos[1])
							>> io::read<float>(v.pos[2]);
						ASSERT(reader);

						// Color
						reader
							>> io::read<uint32>(v.color);
						ASSERT(reader);

						// Uvw
						reader
							>> io::skip<float>()
							>> io::skip<float>()
							>> io::skip<float>();
						ASSERT(reader);

						// Normal
						reader
							>> io::skip<float>()
							>> io::skip<float>()
							>> io::skip<float>();
						ASSERT(reader);
					}

					// Create the vertex buffer
					submesh.m_vertexBuffer = GraphicsDevice::Get().CreateVertexBuffer(vertices.size(), sizeof(POS_COL_VERTEX), false,
						&vertices[0]);
				}

				// Load index chunk
				source.seek(header.indexChunkOffset);
				ASSERT(reader);
				{
					// Read chunk magic
					ChunkMagic indexMagic;
					source.read(&indexMagic[0], indexMagic.size());
					ASSERT(reader);
					ASSERT(indexMagic == mesh::v1_0::IndexChunkMagic);

					// Read chunk size
					uint32 chunkSize = 0;
					reader >> io::read<uint32>(chunkSize);
					ASSERT(reader);

					uint32 indexCount = 0;
					bool use16BitIndices = false;
					reader
						>> io::read<uint32>(indexCount)
						>> io::read<uint8>(use16BitIndices);
					ASSERT(reader);

					if (use16BitIndices)
					{
						std::vector<uint16> indices;
						indices.resize(indexCount);

						for (uint16& index : indices)
						{
							reader >> io::read<uint16>(index);
							ASSERT(reader);
						}

						submesh.m_indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(indexCount, IndexBufferSize::Index_16, &indices[0]);
					}
					else
					{
						std::vector<uint32> indices;
						indices.resize(indexCount);

						for (uint32& index : indices)
						{
							reader >> io::read<uint32>(index);
							ASSERT(reader);
						}

						submesh.m_indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(indexCount, IndexBufferSize::Index_32, &indices[0]);
					}
				}
				
			}
			break;
		}

		// Store the mesh in the cache
		m_meshes[filename] = mesh;

		// And return the mesh pointer
		return mesh;
	}

	MeshPtr MeshManager::CreateManual(const std::string& name)
	{
		const auto it = m_meshes.find(name);
		ASSERT(it == m_meshes.end());

		auto mesh = std::make_shared<Mesh>();
		m_meshes[name] = mesh;

		return mesh;
	}
}
