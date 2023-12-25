// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "mesh_serializer.h"

#include "material_manager.h"
#include "mesh.h"

#include "binary_io/writer.h"

#include "base/chunk_writer.h"
#include "base/vector.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const ChunkMagic MeshChunkMagic = { {'M', 'E', 'S', 'H'} };
	static const ChunkMagic MeshVertexChunk= { {'V', 'E', 'R', 'T'} };
	static const ChunkMagic MeshIndexChunk= { {'I', 'N', 'D', 'X'} };
	static const ChunkMagic MeshSubMeshChunk = { {'S', 'U', 'B', 'M'} };
	static const ChunkMagic MeshSkeletonChunk = MakeChunkMagic('SKEL');
	static const ChunkMagic MeshBoneChunk = MakeChunkMagic('BONE');
	
	void MeshSerializer::ExportMesh(const MeshEntry& mesh, io::Writer& writer, MeshVersion version)
	{
		if (version == mesh_version::Latest)
		{
			version = mesh_version::Version_0_2;
		}
		
		// Write the vertex chunk data
		ChunkWriter meshChunk{ MeshChunkMagic, writer };
		{
			writer << io::write<uint32>(version);
		}
		meshChunk.Finish();
		
		// Write the vertex chunk data
		ChunkWriter vertexChunkWriter{ MeshVertexChunk, writer };
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
				writer
					<< io::write<float>(mesh.vertices[i].binormal.x)
					<< io::write<float>(mesh.vertices[i].binormal.y)
					<< io::write<float>(mesh.vertices[i].binormal.z);
				writer
					<< io::write<float>(mesh.vertices[i].tangent.x)
					<< io::write<float>(mesh.vertices[i].tangent.y)
					<< io::write<float>(mesh.vertices[i].tangent.z);
			}
		}
		vertexChunkWriter.Finish();

		// Write the index chunk data
		ChunkWriter indexChunkWriter{ MeshIndexChunk, writer };
		{
			const bool bUse16BitIndices = mesh.vertices.size() <= std::numeric_limits<uint16>().max();
			
			writer
				<< io::write<uint32>(mesh.indices.size())
				<< io::write<uint8>(bUse16BitIndices);

			for (uint32 index : mesh.indices)
			{
				if (bUse16BitIndices)
				{
					writer << io::write<uint16>(index);
				}
				else
				{
					writer << io::write<uint32>(index);
				}
			}
		}
		indexChunkWriter.Finish();

		if (!mesh.skeletonName.empty())
		{
			ChunkWriter skeletonChunkWriter{ MeshSkeletonChunk, writer };
			{
				writer
					<< io::write_dynamic_range<uint16>(mesh.skeletonName);
			}
			skeletonChunkWriter.Finish();
		}

		for (const auto& boneAssignment : mesh.boneAssignments)
		{
			ChunkWriter boneAssignmentChunk{ MeshBoneChunk, writer };
			{
				writer
					<< io::write<uint32>(boneAssignment.vertexIndex)
					<< io::write<uint16>(boneAssignment.boneIndex)
					<< io::write<float>(boneAssignment.weight);
			}
			boneAssignmentChunk.Finish();
		}

		// Write submesh chunks
		for(const auto& submesh : mesh.subMeshes)
		{
			ChunkWriter submeshChunkWriter{ MeshSubMeshChunk, writer };
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

		m_version = static_cast<MeshVersion>(version);
		if (version < mesh_version::Version_0_2)
		{
			WLOG("Mesh is using an old file format, please consider upgrading it!");
		}

		if (reader)
		{
			if (version >= mesh_version::Version_0_1)
			{
				AddChunkHandler(*MeshVertexChunk, true, *this, &MeshDeserializer::ReadVertexChunk);
				AddChunkHandler(*MeshIndexChunk, true, *this, &MeshDeserializer::ReadIndexChunk);
				AddChunkHandler(*MeshSubMeshChunk, true, *this, &MeshDeserializer::ReadSubMeshChunk);
				AddChunkHandler(*MeshSkeletonChunk, false, *this, &MeshDeserializer::ReadSkeletonChunk);
				AddChunkHandler(*MeshBoneChunk, false, *this, &MeshDeserializer::ReadBoneChunk);

				if (version >= mesh_version::Version_0_2)
				{
					AddChunkHandler(*MeshVertexChunk, true, *this, &MeshDeserializer::ReadVertexV2Chunk);
				}
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
				
		AABB boundingBox;

		auto min = Vector3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		auto max = Vector3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

		// Iterate through vertices
		for (size_t i = 0; i < vertexCount; ++i)
		{
			Vertex v{};
			reader
				>> io::read<float>(v.position[0])
				>> io::read<float>(v.position[1])
				>> io::read<float>(v.position[2]);

			min = TakeMinimum(v.position, min);
			max = TakeMaximum(v.position, max);

			// Color
			reader
				>> io::read<uint32>(v.color);

			// Uvw
			reader
				>> io::read<float>(v.texCoord[0])
				>> io::read<float>(v.texCoord[1])
				>> io::skip<float>();

			// Normal
			reader
				>> io::read<float>(v.normal[0])
				>> io::read<float>(v.normal[1])
				>> io::read<float>(v.normal[2]);
						
			m_entry.vertices.emplace_back(std::move(v));
		}

		const AABB box { min, max };
		boundingBox.Combine(box);

		m_mesh.SetBounds(boundingBox);
		
		return reader;
	}

	bool MeshDeserializer::ReadVertexV2Chunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint32 vertexCount;
		reader >> io::read<uint32>(vertexCount);
		
		m_entry.vertices.clear();
		m_entry.vertices.reserve(vertexCount);
		
		AABB boundingBox;

		auto min = Vector3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		auto max = Vector3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
		
		float tmp = 0.0f;

		// Iterate through vertices
		for (size_t i = 0; i < vertexCount; ++i)
		{
			Vertex v {};
			reader
				>> io::read<float>(v.position[0])
				>> io::read<float>(v.position[1])
				>> io::read<float>(v.position[2]);

			min = TakeMinimum(v.position, min);
			max = TakeMaximum(v.position, max);

			// Color
			reader
				>> io::read<uint32>(v.color);

			// Uvw
			reader
				>> io::read<float>(v.texCoord[0])
				>> io::read<float>(v.texCoord[1])
				>> io::read<float>(tmp);

			// Normal
			reader
				>> io::read<float>(v.normal[0])
				>> io::read<float>(v.normal[1])
				>> io::read<float>(v.normal[2]);
			
			// Binormal
			reader
				>> io::read<float>(v.binormal[0])
				>> io::read<float>(v.binormal[1])
				>> io::read<float>(v.binormal[2]);
			
			// Tangent
			reader
				>> io::read<float>(v.tangent[0])
				>> io::read<float>(v.tangent[1])
				>> io::read<float>(v.tangent[2]);
			
			m_entry.vertices.emplace_back(std::move(v));
		}
		
		const AABB box { min, max };
		boundingBox.Combine(box);

		m_mesh.SetBounds(boundingBox);
		
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

		MaterialPtr material = MaterialManager::Get().Load(materialName);
		if (!material)
		{
			material = MaterialManager::Get().Load("Models/Default.hmat");
		}
		subMesh.SetMaterial(material);
		subMesh.useSharedVertices = true;
		subMesh.indexData = std::make_unique<IndexData>();
		subMesh.indexData->indexStart = indexStart;
		subMesh.indexData->indexCount = indexEnd - indexStart;
		subMesh.indexData->indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(m_entry.indices.size(), IndexBufferSize::Index_32, BufferUsage::StaticWriteOnly, m_entry.indices.data());
		// TODO: IndexBuffer?

		SubMeshEntry entry{};
		entry.material = materialName;
		entry.indexOffset = indexStart;
		entry.triangleCount = (indexEnd - indexStart) / 3;
        m_entry.subMeshes.push_back(entry);
		
		return reader;
	}

	bool MeshDeserializer::ReadSkeletonChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		String skeletonName;
		reader >> io::read_container<uint16>(skeletonName);
		if (reader)
		{
			m_mesh.SetSkeletonName(skeletonName);
		}

		return reader;
	}

	bool MeshDeserializer::ReadBoneChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		VertexBoneAssignment assign;
		if (reader
			>> io::read<uint32>(assign.vertexIndex)
			>> io::read<uint16>(assign.boneIndex)
			>> io::read<float>(assign.weight))
		{
			m_mesh.AddBoneAssignment(assign);
		}

		return reader;
	}

	void MeshDeserializer::CalculateBinormalsAndTangents()
	{
		// Iterate over each triangle...
		for (int i = 0; i < m_entry.indices.size(); i += 3)
		{
			// Get the vertices for that triangle
			auto& v1 = m_entry.vertices[m_entry.indices[i+0]];
			auto& v2 = m_entry.vertices[m_entry.indices[i+1]];
			auto& v3 = m_entry.vertices[m_entry.indices[i+2]];
			
			// Calculate the two vectors for this face.
			const Vector3 edge1 = v2.position - v1.position;
			const Vector3 edge2 = v3.position - v1.position;
			const Vector3 deltaUV1 = v2.texCoord - v1.texCoord;
			const Vector3 deltaUV2 = v3.texCoord - v1.texCoord;

			// Calculate the denominator of the tangent/binormal equation.
			const float denominator = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
			
			// Calculate the cross products and multiply by the coefficient to get the tangent and binormal.
			const auto tangent = Vector3(
				(deltaUV2.y * edge1.x - deltaUV1.y * edge2.x) * denominator,
				(deltaUV2.y * edge1.y - deltaUV1.y * edge2.y) * denominator,
				(deltaUV2.y * edge1.z - deltaUV1.y * edge2.z) * denominator
			).NormalizedCopy();

			const auto binormal = Vector3(
				(-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x) * denominator,
				(-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y) * denominator,
				(-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z) * denominator
			).NormalizedCopy();

			v1.binormal = binormal;
			v2.binormal = binormal;
			v3.binormal = binormal;

			v1.tangent = tangent;
			v2.tangent = tangent;
			v3.tangent = tangent;
		}
	}

	void MeshDeserializer::CreateHardwareBuffers()
	{
		if (m_version < mesh_version::Version_0_2 || (!m_entry.vertices.empty() && m_entry.vertices[0].binormal.IsNearlyEqual(Vector3::Zero)))
		{
			CalculateBinormalsAndTangents();
		}

		std::vector<POS_COL_NORMAL_BINORMAL_TANGENT_TEX_VERTEX> vertices;
		vertices.resize(m_entry.vertices.size());

		for (size_t i = 0; i < vertices.size(); ++i)
		{
			const auto& v = m_entry.vertices[i];
			vertices[i].pos = v.position;
			vertices[i].color = v.color;
			vertices[i].normal = v.normal;
			vertices[i].binormal = v.binormal;
			vertices[i].tangent = v.tangent;
			vertices[i].uv[0] = v.texCoord.x;
			vertices[i].uv[1] = v.texCoord.y;
		}

		m_mesh.sharedVertexData = std::make_unique<VertexData>();
		m_mesh.sharedVertexData->vertexCount = vertices.size();

		VertexDeclaration* decl = m_mesh.sharedVertexData->vertexDeclaration;
		decl->AddElement(0, 0, VertexElementType::Float3, VertexElementSemantic::Position);
		decl->AddElement(0, decl->GetVertexSize(0), VertexElementType::Color, VertexElementSemantic::Diffuse);
		decl->AddElement(0, decl->GetVertexSize(0), VertexElementType::Float3, VertexElementSemantic::Normal);
		decl->AddElement(0, decl->GetVertexSize(0), VertexElementType::Float3, VertexElementSemantic::Binormal);
		decl->AddElement(0, decl->GetVertexSize(0), VertexElementType::Float3, VertexElementSemantic::Tangent);
		decl->AddElement(0, decl->GetVertexSize(0), VertexElementType::Float2, VertexElementSemantic::TextureCoordinate);

		const uint32 vertexSize = decl->GetVertexSize(0);
		const VertexBufferPtr buffer = GraphicsDevice::Get().CreateVertexBuffer(m_mesh.sharedVertexData->vertexCount, vertexSize, BufferUsage::StaticWriteOnly, vertices.data());
		m_mesh.sharedVertexData->vertexBufferBinding->SetBinding(0, buffer);

		if (m_mesh.HasSkeleton())
		{
			m_mesh.UpdateCompiledBoneAssignments();
		}
	}

	bool MeshDeserializer::OnReadFinished() noexcept
	{
		CreateHardwareBuffers();
		
		return true;
	}
}
