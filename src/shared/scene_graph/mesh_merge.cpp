// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_merge.h"

#include "material_manager.h"
#include "sub_mesh.h"
#include "graphics/graphics_device.h"
#include "base/profiler.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace mmo
{
	namespace
	{
		/// Replaces a non-finite or degenerate direction with a unit fallback.
		void SanitizeDirection(Vector3& v, const Vector3& fallback)
		{
			if (!IsFiniteVector(v))
			{
				v = fallback;
				return;
			}

			const float length = v.GetLength();
			if (!std::isfinite(length) || length <= 1e-6f)
			{
				v = fallback;
			}
			else if (std::abs(length - 1.0f) > 1e-3f)
			{
				v /= length;
			}
		}
	}

	bool HasMergeableVertexLayout(const VertexData& vertexData)
	{
		if (!vertexData.vertexDeclaration || !vertexData.vertexBufferBinding ||
			vertexData.vertexBufferBinding->GetBufferCount() != 1 ||
			vertexData.vertexDeclaration->GetVertexSize(0) != sizeof(WorldModelMergeVertex))
		{
			return false;
		}

		// The offsets the static mesh loader uses (MeshDeserializer) - anything else is another layout
		const auto* decl = vertexData.vertexDeclaration;
		const auto offsetOf = [decl](const VertexElementSemantic semantic) -> int32
		{
			const VertexElement* element = decl->FindElementBySemantic(semantic);
			return element ? static_cast<int32>(element->GetOffset()) : -1;
		};

		return offsetOf(VertexElementSemantic::Position) == offsetof(WorldModelMergeVertex, pos) &&
			offsetOf(VertexElementSemantic::Diffuse) == offsetof(WorldModelMergeVertex, color) &&
			offsetOf(VertexElementSemantic::Normal) == offsetof(WorldModelMergeVertex, normal) &&
			offsetOf(VertexElementSemantic::Binormal) == offsetof(WorldModelMergeVertex, binormal) &&
			offsetOf(VertexElementSemantic::Tangent) == offsetof(WorldModelMergeVertex, tangent) &&
			offsetOf(VertexElementSemantic::TextureCoordinate) == offsetof(WorldModelMergeVertex, uv) &&
			!decl->FindElementBySemantic(VertexElementSemantic::BlendIndices);
	}

	bool CanMergeMesh(const Mesh& mesh)
	{
		if (mesh.HasSkeleton() || mesh.GetSubMeshCount() == 0)
		{
			return false;
		}

		for (uint16 i = 0; i < mesh.GetSubMeshCount(); ++i)
		{
			const SubMesh& subMesh = mesh.GetSubMesh(i);
			if (subMesh.useSharedVertices || !subMesh.vertexData || !subMesh.indexData || subMesh.indexData->indexCount == 0)
			{
				return false;
			}

			if (!HasMergeableVertexLayout(*subMesh.vertexData))
			{
				return false;
			}
		}

		return true;
	}

	bool ReadSubMeshGeometry(const SubMesh& subMesh, MeshMergeGeometry& out)
	{
		PROFILE_SCOPE("Mesh merge: ReadSubMesh (GPU readback)");
		if (!subMesh.vertexData || !subMesh.indexData || !subMesh.vertexData->vertexBufferBinding)
		{
			return false;
		}

		const VertexData& vertexData = *subMesh.vertexData;
		const IndexData& indexData = *subMesh.indexData;
		const VertexBufferPtr& vertexBuffer = vertexData.vertexBufferBinding->GetBuffer(0);
		if (!vertexBuffer || !indexData.indexBuffer)
		{
			return false;
		}

		const auto* vertexBytes = static_cast<const uint8*>(vertexBuffer->Map(LockOptions::ReadOnly));
		if (!vertexBytes)
		{
			return false;
		}

		out.vertices.resize(vertexData.vertexCount);
		std::memcpy(out.vertices.data(), vertexBytes + vertexData.vertexStart * sizeof(WorldModelMergeVertex),
			vertexData.vertexCount * sizeof(WorldModelMergeVertex));
		vertexBuffer->Unmap();

		const void* indexBytes = indexData.indexBuffer->Map(LockOptions::ReadOnly);
		if (!indexBytes)
		{
			return false;
		}

		out.indices.resize(indexData.indexCount);
		if (indexData.indexBuffer->GetIndexSize() == IndexBufferSize::Index_16)
		{
			const auto* indices = static_cast<const uint16*>(indexBytes) + indexData.indexStart;
			std::copy_n(indices, indexData.indexCount, out.indices.begin());
		}
		else
		{
			const auto* indices = static_cast<const uint32*>(indexBytes) + indexData.indexStart;
			std::copy_n(indices, indexData.indexCount, out.indices.begin());
		}
		indexData.indexBuffer->Unmap();

		return true;
	}

	MeshPtr CreateMergedMesh(const String& name, MeshMergeBuckets& buckets)
	{
		PROFILE_SCOPE("Mesh merge: CreateMergedMesh");
		if (buckets.empty())
		{
			return nullptr;
		}

		auto mesh = std::make_shared<Mesh>(name);
		// A null AABB is a zero box at the origin, and combining points with it keeps the origin inside:
		// the bounds start at the first vertex instead, or a merged group far from its world model's
		// origin would reach back to it and never be culled.
		AABB bounds;
		bool hasBounds = false;

		auto& device = GraphicsDevice::Get();
		for (auto& [materialName, bucket] : buckets)
		{
			if (bucket.indices.empty())
			{
				continue;
			}

			SubMesh& subMesh = mesh->CreateSubMesh("m" + std::to_string(mesh->GetSubMeshCount()));
			subMesh.useSharedVertices = false;
			subMesh.SetMaterial(bucket.material);

			subMesh.vertexData = std::make_unique<VertexData>(&device);
			subMesh.vertexData->vertexCount = static_cast<uint32>(bucket.vertices.size());
			subMesh.vertexData->vertexStart = 0;

			uint32 offset = 0;
			auto* decl = subMesh.vertexData->vertexDeclaration;
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
			decl->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate);

			subMesh.vertexData->vertexBufferBinding->SetBinding(0, device.CreateVertexBuffer(
				static_cast<uint32>(bucket.vertices.size()), sizeof(WorldModelMergeVertex), BufferUsage::StaticWriteOnly, bucket.vertices.data()));

			subMesh.indexData = std::make_unique<IndexData>();
			subMesh.indexData->indexStart = 0;
			subMesh.indexData->indexCount = static_cast<uint32>(bucket.indices.size());
			if (bucket.vertices.size() <= 0xFFFF)
			{
				std::vector<uint16> narrow(bucket.indices.begin(), bucket.indices.end());
				subMesh.indexData->indexBuffer = device.CreateIndexBuffer(
					static_cast<uint32>(narrow.size()), IndexBufferSize::Index_16, BufferUsage::StaticWriteOnly, narrow.data());
			}
			else
			{
				subMesh.indexData->indexBuffer = device.CreateIndexBuffer(
					static_cast<uint32>(bucket.indices.size()), IndexBufferSize::Index_32, BufferUsage::StaticWriteOnly, bucket.indices.data());
			}

			for (const auto& vertex : bucket.vertices)
			{
				if (hasBounds)
				{
					bounds.Combine(vertex.pos);
				}
				else
				{
					bounds = AABB(vertex.pos, vertex.pos);
					hasBounds = true;
				}
			}
		}

		if (mesh->GetSubMeshCount() == 0)
		{
			return nullptr;
		}

		mesh->SetBounds(bounds);
		return mesh;
	}

	std::vector<bool> FindRepeatedPlacements(const std::vector<String>& keys, const size_t minCount)
	{
		std::map<String, size_t> counts;
		for (const String& key : keys)
		{
			++counts[key];
		}

		std::vector<bool> repeated(keys.size(), false);
		for (size_t i = 0; i < keys.size(); ++i)
		{
			repeated[i] = counts[keys[i]] >= minCount;
		}

		return repeated;
	}

	const MeshMergeGeometry* MeshReadBackCache::Get(const Mesh& mesh, const uint16 subMeshIndex, const bool requireFinite)
	{
		const auto key = std::make_pair(&mesh, subMeshIndex);
		auto it = m_entries.find(key);
		if (it == m_entries.end())
		{
			Entry entry;
			entry.geometry = std::make_unique<MeshMergeGeometry>();
			if (subMeshIndex >= mesh.GetSubMeshCount() || !ReadSubMeshGeometry(mesh.GetSubMesh(subMeshIndex), *entry.geometry))
			{
				entry.geometry.reset();
			}
			else
			{
				// Validated once here rather than per placement
				const MeshMergeGeometry& geometry = *entry.geometry;
				entry.finite =
					std::all_of(geometry.vertices.begin(), geometry.vertices.end(), [](const WorldModelMergeVertex& v) { return IsFiniteVector(v.pos); }) &&
					std::none_of(geometry.indices.begin(), geometry.indices.end(), [&geometry](const uint32 index) { return index >= geometry.vertices.size(); });
			}
			it = m_entries.emplace(key, std::move(entry)).first;
		}

		if (requireFinite && !it->second.finite)
		{
			return nullptr;
		}

		return it->second.geometry.get();
	}

	MeshMergeAccumulator::MeshMergeAccumulator(MeshReadBackCache& cache, const size_t maxVerticesPerBucket)
		: m_cache(cache)
		, m_maxVertices(std::max<size_t>(1, maxVerticesPerBucket))
	{
	}

	bool MeshMergeAccumulator::Add(const Mesh& mesh, const Matrix4& transform, const std::vector<String>& materialOverrides)
	{
		if (!transform.IsFinite())
		{
			return false;
		}

		bool complete = true;
		for (uint16 subIndex = 0; subIndex < mesh.GetSubMeshCount(); ++subIndex)
		{
			// Source data can hold anything: unreadable geometry, positions that are not finite and
			// indices out of range cannot be repaired, so such a submesh is left out.
			const MeshMergeGeometry* geometry = m_cache.Get(mesh, subIndex, true);
			if (!geometry)
			{
				complete = false;
				continue;
			}

			const SubMesh& subMesh = mesh.GetSubMesh(subIndex);
			MaterialPtr material;
			if (subIndex < materialOverrides.size() && !materialOverrides[subIndex].empty())
			{
				material = MaterialManager::Get().Load(materialOverrides[subIndex]);
				if (!material)
				{
					WLOG("Mesh merge: material override " << materialOverrides[subIndex] << " of " << mesh.GetName()
						<< " submesh " << subIndex << " did not load, using the submesh's own material");
				}
			}
			if (!material)
			{
				material = subMesh.GetMaterial();
			}

			const String materialName = material ? String(material->GetName()) : String();
			MeshMergeBucket& bucket = BucketFor(materialName, geometry->vertices.size());
			bucket.material = material;

			const size_t first = bucket.vertices.size();
			const size_t firstIndex = bucket.indices.size();
			AppendTransformedGeometry(geometry->vertices.data(), geometry->vertices.size(),
				geometry->indices.data(), geometry->indices.size(), transform, bucket.vertices, bucket.indices);

			// A finite transform of finite positions can still overflow; such a placement is undone.
			if (!std::all_of(bucket.vertices.begin() + static_cast<std::ptrdiff_t>(first), bucket.vertices.end(),
				[](const WorldModelMergeVertex& v) { return IsFiniteVector(v.pos); }))
			{
				bucket.vertices.resize(first);
				bucket.indices.resize(firstIndex);
				complete = false;
				continue;
			}

			for (size_t i = first; i < bucket.vertices.size(); ++i)
			{
				WorldModelMergeVertex& v = bucket.vertices[i];
				SanitizeDirection(v.normal, Vector3::UnitY);
				SanitizeDirection(v.tangent, Vector3::UnitX);
				SanitizeDirection(v.binormal, Vector3::UnitZ);
				for (float& uv : v.uv)
				{
					if (!std::isfinite(uv))
					{
						uv = 0.0f;
					}
				}
			}
		}

		return complete;
	}

	MeshMergeBucket& MeshMergeAccumulator::BucketFor(const String& materialName, const size_t vertexCount)
	{
		for (auto& part : m_parts)
		{
			auto it = part.find(materialName);
			if (it == part.end())
			{
				return part[materialName];
			}

			if (it->second.vertices.empty() || it->second.vertices.size() + vertexCount <= m_maxVertices)
			{
				return it->second;
			}
		}

		return m_parts.emplace_back()[materialName];
	}
}
