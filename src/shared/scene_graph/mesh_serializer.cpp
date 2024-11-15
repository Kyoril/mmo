// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "mesh_serializer.h"

#include <ranges>

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
	static const ChunkMagic MeshCollisionChunk = MakeChunkMagic('COLL');
	
	void WriteVertexData(const VertexData& vertexData, io::Writer& writer)
	{
		writer << io::write<uint32>(vertexData.vertexCount);

		// Get shared vertex data
		const auto buffer = vertexData.vertexBufferBinding->GetBuffer(0);

		auto bufferData = static_cast<uint8*>(buffer->Map(LockOptions::ReadOnly));
		ASSERT(bufferData);

		for (size_t i = 0; i < vertexData.vertexCount; ++i)
		{
			const VertexElement* posElement = vertexData.vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Position);
			float* position = nullptr;
			posElement->BaseVertexPointerToElement(bufferData, &position);
			writer
				<< io::write<float>(*position++)
				<< io::write<float>(*position++)
				<< io::write<float>(*position++);

			const VertexElement* colElement = vertexData.vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Diffuse);
			uint32* color = nullptr;
			colElement->BaseVertexPointerToElement(bufferData, &color);
			writer
				<< io::write<uint32>(*color++);

			const VertexElement* texElement = vertexData.vertexDeclaration->FindElementBySemantic(VertexElementSemantic::TextureCoordinate);
			float* tex = nullptr;
			texElement->BaseVertexPointerToElement(bufferData, &tex);
			writer
				<< io::write<float>(*tex++)
				<< io::write<float>(*tex++)
				<< io::write<float>(0.0f);

			const VertexElement* normalElement = vertexData.vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Normal);
			float* normal = nullptr;
			normalElement->BaseVertexPointerToElement(bufferData, &normal);
			writer
				<< io::write<float>(*normal++)
				<< io::write<float>(*normal++)
				<< io::write<float>(*normal++);

			const VertexElement* binormalElement = vertexData.vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Binormal);
			float* binormal = nullptr;
			binormalElement->BaseVertexPointerToElement(bufferData, &binormal);
			writer
				<< io::write<float>(*binormal++)
				<< io::write<float>(*binormal++)
				<< io::write<float>(*binormal++);

			const VertexElement* tangentElement = vertexData.vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Tangent);
			float* tangent = nullptr;
			tangentElement->BaseVertexPointerToElement(bufferData, &tangent);
			writer
				<< io::write<float>(*tangent++)
				<< io::write<float>(*tangent++)
				<< io::write<float>(*tangent++);

			bufferData += vertexData.vertexDeclaration->GetVertexSize(0);
		}

		buffer->Unmap();
	}

	void WriteIndexData(const IndexData& indexData, io::Writer& writer)
	{
		writer
			<< io::write<uint32>(indexData.indexCount)
			<< io::write<uint8>(indexData.indexBuffer->GetIndexSize());

		if (indexData.indexBuffer->GetIndexSize() == IndexBufferSize::Index_16)
		{
			const uint16* indices = reinterpret_cast<uint16*>(indexData.indexBuffer->Map(LockOptions::ReadOnly));
			ASSERT(indices);

			for (size_t i = 0; i < indexData.indexCount; ++i)
			{
				writer << io::write<uint16>(*indices++);
			}
		}
		else
		{
			const uint32* indices = reinterpret_cast<uint32*>(indexData.indexBuffer->Map(LockOptions::ReadOnly));
			ASSERT(indices);

			for (size_t i = 0; i < indexData.indexCount; ++i)
			{
				writer << io::write<uint32>(*indices++);
			}
		}

		indexData.indexBuffer->Unmap();
	}

	void MeshSerializer::Serialize(const MeshPtr& mesh, io::Writer& writer, MeshVersion version)
	{
		if (version == mesh_version::Latest)
		{
			version = mesh_version::Version_0_3;
		}

		// Write the vertex chunk data
		ChunkWriter meshChunk{ MeshChunkMagic, writer };
		{
			writer << io::write<uint32>(version);
		}
		meshChunk.Finish();

		if (mesh->sharedVertexData)
		{
			// Write the vertex chunk data
			ChunkWriter vertexChunkWriter{ MeshVertexChunk, writer };
			{
				WriteVertexData(*mesh->sharedVertexData, writer);
			}
			vertexChunkWriter.Finish();
		}

		if (mesh->HasSkeleton())
		{
			ChunkWriter skeletonChunkWriter{ MeshSkeletonChunk, writer };
			{
				writer
					<< io::write_dynamic_range<uint16>(mesh->GetSkeletonName());
			}
			skeletonChunkWriter.Finish();
		}

		// Collision tree available?
		if (!mesh->GetCollisionTree().IsEmpty())
		{
			ChunkWriter collisionChunk{ MeshCollisionChunk, writer };
			writer << mesh->GetCollisionTree();
			collisionChunk.Finish();
		}

		// Write submesh chunks
		uint16 submeshIndex = 0;
		for (const auto& submesh : mesh->GetSubMeshes())
		{
			ChunkWriter submeshChunkWriter{ MeshSubMeshChunk, writer };
			{
				// Try to get name of the submesh
				String name;
				mesh->GetSubMeshName(submeshIndex++, name);

				// Write material name
				writer << io::write_dynamic_range<uint8>(name);
				writer << io::write_dynamic_range<uint16>(submesh->GetMaterial() ? submesh->GetMaterial()->GetName() : "Models/Default.hmat");

				// Write vertex data
				writer << io::write<uint8>(submesh->useSharedVertices);
				if (!submesh->useSharedVertices)
				{
					WriteVertexData(*submesh->vertexData, writer);
				}

				// Write index data
				writer << io::write<uint8>(submesh->indexData != nullptr);
				if (submesh->indexData)
				{
					WriteIndexData(*submesh->indexData, writer);
				}

				// Write vertex bone assignments
				const uint32 vertexBoneAssignmentCount = submesh->GetBoneAssignments().size();
				writer << io::write<uint32>(vertexBoneAssignmentCount);

				for (const auto& [vertexIndex, boneIndex, weight] : submesh->GetBoneAssignments() | std::views::values)
				{
					writer
						<< io::write<uint32>(vertexIndex)
						<< io::write<uint16>(boneIndex)
						<< io::write<float>(weight);
				}
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
		if (version < mesh_version::Version_0_3)
		{
			WLOG("Mesh '" << m_mesh.GetName() << "' is using an old file format, please consider upgrading it!");
		}

		if (reader)
		{
			if (version >= mesh_version::Version_0_1)
			{
				AddChunkHandler(*MeshVertexChunk, false, *this, &MeshDeserializer::ReadVertexChunk);
				AddChunkHandler(*MeshIndexChunk, false, *this, &MeshDeserializer::ReadIndexChunk);
				AddChunkHandler(*MeshSubMeshChunk, false, *this, &MeshDeserializer::ReadSubMeshChunk);
				AddChunkHandler(*MeshSkeletonChunk, false, *this, &MeshDeserializer::ReadSkeletonChunk);
				AddChunkHandler(*MeshBoneChunk, false, *this, &MeshDeserializer::ReadBoneChunk);

				if (version >= mesh_version::Version_0_2)
				{
					AddChunkHandler(*MeshVertexChunk, false, *this, &MeshDeserializer::ReadVertexV2Chunk);

					if (version >= mesh_version::Version_0_3)
					{
						AddChunkHandler(*MeshSubMeshChunk, false, *this, &MeshDeserializer::ReadSubMeshChunkV3);
						AddChunkHandler(*MeshCollisionChunk, false, *this, &MeshDeserializer::ReadCollisionChunk);

						// Chunk no longer supported in V03 because there is no longer global index data
						RemoveChunkHandler(*MeshIndexChunk);
					}
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

	bool MeshDeserializer::ReadSubMeshChunkV3(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		AABB bounds = m_mesh.GetBounds();

		String name;
		if (!(reader >> io::read_container<uint8>(name)))
		{
			ELOG("Failed to read submesh name");
			return false;
		}

		String materialName;
		reader >> io::read_container<uint16>(materialName);

		MaterialPtr material = MaterialManager::Get().Load(materialName);
		if (!material)
		{
			material = MaterialManager::Get().Load("Models/Default.hmat");
			ASSERT(material);
		}

		// Create the sub mesh
		auto& subMesh = m_mesh.CreateSubMesh();
		if (!name.empty())
		{
			m_mesh.NameSubMesh(m_mesh.GetSubMeshCount() - 1, name);
		}

		subMesh.SetMaterial(material);

		bool useSharedVertices = false;
		if (!(reader >> io::read<uint8>(useSharedVertices)))
		{
			ELOG("Failed to read submesh useSharedVertices");
			return false;
		}

		subMesh.useSharedVertices = useSharedVertices;
		ASSERT(!useSharedVertices || m_mesh.sharedVertexData);

		if (!useSharedVertices)
		{
			// Read vertex data
			subMesh.vertexData = std::make_unique<VertexData>();

			// TODO: Read vertex data

			uint32 vertexCount = 0;
			if (!(reader >> io::read<uint32>(vertexCount)))
			{
				ELOG("Failed to read submesh vertexCount");
				return false;
			}

			subMesh.vertexData->vertexCount = vertexCount;
			subMesh.vertexData->vertexStart = 0;

			// Setup vertex buffer binding
			uint32 offset = 0;
			offset += subMesh.vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
			offset += subMesh.vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
			offset += subMesh.vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
			offset += subMesh.vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
			offset += subMesh.vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
			offset += subMesh.vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

			struct VertexStruct
			{
				Vector3 position;
				uint32 color;
				Vector3 normal;
				Vector3 binormal;
				Vector3 tangent;
				float u, v;
			};

			std::vector<VertexStruct> vertices(vertexCount);

			VertexStruct* vert = vertices.data();
			for (uint32 i = 0; i < vertexCount; ++i)
			{
				if (!(reader
					>> io::read<float>(vert->position.x)
					>> io::read<float>(vert->position.y)
					>> io::read<float>(vert->position.z)
					
					>> io::read<uint32>(vert->color)

					>> io::read<float>(vert->u)
					>> io::read<float>(vert->v)
					>> io::skip<float>()
					
					>> io::read<float>(vert->normal.x)
					>> io::read<float>(vert->normal.y)
					>> io::read<float>(vert->normal.z)
					
					>> io::read<float>(vert->binormal.x)
					>> io::read<float>(vert->binormal.y)
					>> io::read<float>(vert->binormal.z)

					>> io::read<float>(vert->tangent.x)
					>> io::read<float>(vert->tangent.y)
					>> io::read<float>(vert->tangent.z)))
				{
					ELOG("Failed to read submesh vertex data");
					return false;
				}

				bounds.Combine(vert->position);

				vert++;
			}

			VertexBufferPtr bufferPtr = GraphicsDevice::Get().CreateVertexBuffer(vertexCount, subMesh.vertexData->vertexDeclaration->GetVertexSize(0), BufferUsage::StaticWriteOnly, vertices.data());
			subMesh.vertexData->vertexBufferBinding->SetBinding(0, bufferPtr);
		}

		// Read index data
		bool hasIndexData = false;
		if (!(reader >> io::read<uint8>(hasIndexData)))
		{
			ELOG("Failed to read submesh hasIndexData");
			return false;
		}

		if (hasIndexData)
		{
			subMesh.indexData = std::make_unique<IndexData>();

			uint32 indexCount = 0;
			IndexBufferSize indexSize;
			if (!(reader >> io::read<uint32>(indexCount) >> io::read<uint8>(indexSize)))
			{
				ELOG("Failed to read submesh indexCount or indexSize");
				return false;
			}

			subMesh.indexData->indexCount = indexCount;
			subMesh.indexData->indexStart = 0;

			if (indexSize == IndexBufferSize::Index_16)
			{
				std::vector<uint16> indices(indexCount, 0);
				if (!(reader >> io::read_range(indices)))
				{
					ELOG("Failed to read submesh indices");
					return false;
				}

				subMesh.indexData->indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(indexCount, IndexBufferSize::Index_16, BufferUsage::StaticWriteOnly, indices.data());
			}
			else
			{
				std::vector<uint32> indices(indexCount, 0);
				if (!(reader >> io::read_range(indices)))
				{
					ELOG("Failed to read submesh indices");
					return false;
				}

				subMesh.indexData->indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(indexCount, IndexBufferSize::Index_32, BufferUsage::StaticWriteOnly, indices.data());
			}
		}

		// Read bone assignments
		uint32 vertexBoneAssignmentCount = 0;
		if (!(reader >> io::read<uint32>(vertexBoneAssignmentCount)))
		{
			ELOG("Failed to read submesh vertexBoneAssignmentCount");
			return false;
		}

		for (uint32 i = 0; i < vertexBoneAssignmentCount; ++i)
		{
			VertexBoneAssignment assign;
			if (!(reader
				>> io::read<uint32>(assign.vertexIndex)
				>> io::read<uint16>(assign.boneIndex)
				>> io::read<float>(assign.weight)))
			{
				ELOG("Failed to read submesh vertexBoneAssignment");
				return false;
			}

			subMesh.AddBoneAssignment(assign);
		}

		// Update bounding box
		m_mesh.SetBounds(bounds);

		return true;
	}

	bool MeshDeserializer::ReadSkeletonChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		String skeletonName;
		reader >> io::read_container<uint16>(skeletonName);
		if (reader)
		{
			m_entry.skeletonName = skeletonName;
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

			m_entry.boneAssignments.emplace_back(assign);
		}

		return reader;
	}

	bool MeshDeserializer::ReadCollisionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		reader >> m_mesh.m_collisionTree;
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

		if (m_version <= mesh_version::Version_0_2)
		{
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
		}

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
