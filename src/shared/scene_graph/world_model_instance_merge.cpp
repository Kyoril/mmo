// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_instance.h"
#include "world_model_merge.h"

#include "entity.h"
#include "material_manager.h"
#include "mesh.h"
#include "mesh_manager.h"
#include "scene.h"
#include "scene_node.h"
#include "sub_mesh.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "base/profiler.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <unordered_map>

namespace mmo
{
	/// @brief The merged meshes of one world model: per group a structure and a detail mesh (either may
	///        be null) and the mesh references that could not be merged.
	struct WorldModelMergedGeometry
	{
		struct Group
		{
			MeshPtr structure;
			MeshPtr detail;

			/// @brief Visible mesh references that render through the per-entity path instead.
			std::vector<size_t> fallbackRefs;

			/// @brief Visible mesh references baked into the meshes above; they need collision proxies.
			std::vector<size_t> mergedRefs;
		};

		std::vector<Group> groups;
	};

	namespace
	{
		/// @brief CPU copy of one submesh, read back from its GPU buffers.
		struct SubMeshGeometry
		{
			std::vector<WorldModelMergeVertex> vertices;
			std::vector<uint32> indices;
		};

		bool HasMergeableLayout(const VertexData& vertexData)
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

		bool ReadSubMesh(const SubMesh& subMesh, SubMeshGeometry& out)
		{
			PROFILE_SCOPE("WMO: ReadSubMesh (GPU readback)");
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

		/// @brief Merged geometry being collected for one material of one chunk.
		struct MaterialBucket
		{
			MaterialPtr material;
			std::vector<WorldModelMergeVertex> vertices;
			std::vector<uint32> indices;
		};

		MeshPtr CreateMergedMesh(const String& name, std::map<String, MaterialBucket>& buckets)
		{
			PROFILE_SCOPE("WMO: CreateMergedMesh");
			if (buckets.empty())
			{
				return nullptr;
			}

			auto mesh = std::make_shared<Mesh>(name);
			AABB bounds;
			bounds.SetNull();

			auto& device = GraphicsDevice::Get();
			for (auto& [materialName, bucket] : buckets)
			{
				if (bucket.indices.empty())
				{
					continue;
				}

				SubMesh& subMesh = mesh->CreateSubMesh();
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
					bounds.Combine(vertex.pos);
				}
			}

			if (mesh->GetSubMeshCount() == 0)
			{
				return nullptr;
			}

			mesh->SetBounds(bounds);
			return mesh;
		}

		/// @brief Merges a world model's groups, all at once or a few groups per call.
		class MergedGeometryBuilder final
		{
		public:
			explicit MergedGeometryBuilder(const WorldModel& model)
				: m_model(model)
				, m_merged(std::make_shared<WorldModelMergedGeometry>())
			{
				m_merged->groups.resize(model.GetGroupCount());
			}

			/// @brief Merges up to maxGroups more groups. Returns true once every group is merged.
			bool Step(const size_t maxGroups)
			{
				PROFILE_SCOPE("WMO: BuildMerged");
				for (size_t built = 0; built < maxGroups && m_nextGroup < m_model.GetGroupCount(); ++built)
				{
					BuildGroup(m_nextGroup++);
				}

				if (m_nextGroup < m_model.GetGroupCount())
				{
					return false;
				}

				// The read-back copies are only needed while merging
				m_readBack.clear();
				return true;
			}

			[[nodiscard]] const std::shared_ptr<WorldModelMergedGeometry>& GetResult() const { return m_merged; }

		private:
			void BuildGroup(const size_t groupIndex)
			{
				const WorldModel& model = m_model;
				auto& readBack = m_readBack;

				const WorldModelGroup* group = model.GetGroup(groupIndex);
				if (!group)
				{
					return;
				}

				auto& out = m_merged->groups[groupIndex];
				std::map<String, MaterialBucket> structure;
				std::map<String, MaterialBucket> detail;

				const auto& meshRefs = group->GetMeshRefs();
				for (size_t refIndex = 0; refIndex < meshRefs.size(); ++refIndex)
				{
					const WorldModelMeshRef& meshRef = meshRefs[refIndex];
					if (!meshRef.visible)
					{
						continue;
					}

					const MeshPtr mesh = MeshManager::Get().Load(meshRef.meshPath);
					if (!mesh || !WorldModelInstance::CanMergeMesh(*mesh))
					{
						out.fallbackRefs.push_back(refIndex);
						continue;
					}

					const MaterialPtr overrideMaterial = meshRef.materialOverride.empty() ? nullptr : MaterialManager::Get().Load(meshRef.materialOverride);

					Matrix4 transform;
					transform.MakeTransform(meshRef.position, meshRef.scale, meshRef.rotation);

					const float maxScale = std::max({ std::abs(meshRef.scale.x), std::abs(meshRef.scale.y), std::abs(meshRef.scale.z) });
					auto& chunk = IsWorldModelDetail(mesh->GetBoundRadius() * maxScale) ? detail : structure;

					bool complete = true;
					for (uint16 subIndex = 0; subIndex < mesh->GetSubMeshCount() && complete; ++subIndex)
					{
						SubMesh& subMesh = mesh->GetSubMesh(subIndex);
						const auto key = std::make_pair(mesh.get(), subIndex);
						auto it = readBack.find(key);
						if (it == readBack.end())
						{
							SubMeshGeometry geometry;
							if (!ReadSubMesh(subMesh, geometry))
							{
								complete = false;
								break;
							}
							it = readBack.emplace(key, std::move(geometry)).first;
						}

						const MaterialPtr material = overrideMaterial ? overrideMaterial : subMesh.GetMaterial();
						const String materialName = material ? String(material->GetName()) : String();
						MaterialBucket& bucket = chunk[materialName];
						bucket.material = material;
						AppendTransformedGeometry(it->second.vertices.data(), it->second.vertices.size(),
							it->second.indices.data(), it->second.indices.size(), transform, bucket.vertices, bucket.indices);
					}

					if (!complete)
					{
						// Whatever was read stays merged; the placement keeps its collision either way
						WLOG("Could not read back mesh " << meshRef.meshPath << " completely for merging");
					}

					out.mergedRefs.push_back(refIndex);
				}

				const String prefix = "WorldModelMerge_" + std::to_string(reinterpret_cast<uintptr_t>(&model)) + "_g" + std::to_string(groupIndex);
				out.structure = CreateMergedMesh(prefix + "_structure", structure);
				out.detail = CreateMergedMesh(prefix + "_detail", detail);
			}

			const WorldModel& m_model;
			std::shared_ptr<WorldModelMergedGeometry> m_merged;
			size_t m_nextGroup = 0;

			/// Every distinct submesh is read back once, however often it is placed
			std::map<std::pair<const Mesh*, uint16>, SubMeshGeometry> m_readBack;
		};

		size_t GetMeshBytes(const MeshPtr& mesh)
		{
			if (!mesh)
			{
				return 0;
			}

			size_t bytes = 0;
			for (uint16 i = 0; i < mesh->GetSubMeshCount(); ++i)
			{
				const SubMesh& subMesh = mesh->GetSubMesh(i);
				if (subMesh.vertexData)
				{
					bytes += subMesh.vertexData->vertexCount * sizeof(WorldModelMergeVertex);
				}
				if (subMesh.indexData && subMesh.indexData->indexBuffer)
				{
					const size_t indexSize = subMesh.indexData->indexBuffer->GetIndexSize() == IndexBufferSize::Index_16 ? 2 : 4;
					bytes += subMesh.indexData->indexCount * indexSize;
				}
			}

			return bytes;
		}

		/// @brief Merged geometry per world model, shared by all live instances of it, and kept for a
		///        while after the last one is gone (see WorldModelInstance::SetMergedGeometryRetention).
		class MergedGeometryCache final
		{
		public:
			/// @brief Returns the model's merged geometry, merging whatever is not merged yet.
			std::shared_ptr<const WorldModelMergedGeometry> GetOrBuild(const WorldModelPtr& model)
			{
				return Advance(model, std::numeric_limits<size_t>::max());
			}

			/// @brief Merges up to maxGroups more groups of the model. Returns its merged geometry once
			///        complete, null while groups remain.
			std::shared_ptr<const WorldModelMergedGeometry> Advance(const WorldModelPtr& model, const size_t maxGroups)
			{
				std::scoped_lock lock(m_mutex);

				auto it = m_entries.find(model.get());
				if (it != m_entries.end())
				{
					// The key is a raw pointer, so a world model that died may have been replaced by
					// another one at the same address - its geometry must not be handed out.
					if (it->second.model.lock() == model)
					{
						if (auto existing = it->second.Get())
						{
							it->second.lastUse = ++m_useCounter;
							return existing;
						}
					}

					m_entries.erase(it);
				}

				auto& builder = m_builders[model.get()];
				if (!builder || builder->model.lock() != model)
				{
					builder = std::make_unique<PendingBuild>(model);
				}

				if (!builder->builder.Step(maxGroups))
				{
					return nullptr;
				}

				std::shared_ptr<const WorldModelMergedGeometry> built = builder->builder.GetResult();
				m_builders.erase(model.get());

				Entry entry;
				entry.model = model;
				entry.weak = built;
				entry.lastUse = ++m_useCounter;
				for (const auto& group : built->groups)
				{
					entry.bytes += GetMeshBytes(group.structure) + GetMeshBytes(group.detail);
				}
				if (m_retentionBytes > 0)
				{
					entry.strong = built;
				}
				m_entries.emplace(model.get(), std::move(entry));

				Trim();
				return built;
			}

			void SetRetention(const size_t bytes)
			{
				std::scoped_lock lock(m_mutex);
				m_retentionBytes = bytes;
				if (bytes == 0)
				{
					for (auto& [key, entry] : m_entries)
					{
						entry.strong.reset();
					}
				}

				Trim();
			}

		private:
			/// A merge in progress. Holds the model weakly, like the entries.
			struct PendingBuild
			{
				explicit PendingBuild(const WorldModelPtr& source)
					: model(source)
					, builder(*source)
				{
				}

				std::weak_ptr<WorldModel> model;
				MergedGeometryBuilder builder;
			};

			struct Entry
			{
				std::weak_ptr<WorldModel> model;
				std::weak_ptr<const WorldModelMergedGeometry> weak;

				/// Set while retention is on: keeps the geometry alive after its last instance.
				std::shared_ptr<const WorldModelMergedGeometry> strong;

				size_t bytes = 0;
				uint64 lastUse = 0;

				[[nodiscard]] std::shared_ptr<const WorldModelMergedGeometry> Get() const
				{
					return strong ? strong : weak.lock();
				}

				/// Whether only the cache keeps the geometry alive.
				[[nodiscard]] bool IsUnused() const
				{
					return strong && strong.use_count() == 1;
				}
			};

			/// Drops expired entries, then releases the least recently used unused geometry until the
			/// unused geometry fits the retention budget.
			void Trim()
			{
				std::erase_if(m_builders, [](const auto& pending) { return pending.second->model.expired(); });

				size_t unusedBytes = 0;
				for (auto it = m_entries.begin(); it != m_entries.end();)
				{
					if (!it->second.Get())
					{
						it = m_entries.erase(it);
						continue;
					}

					if (it->second.IsUnused())
					{
						unusedBytes += it->second.bytes;
					}
					++it;
				}

				while (unusedBytes > m_retentionBytes)
				{
					auto oldest = m_entries.end();
					for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
					{
						if (it->second.IsUnused() && (oldest == m_entries.end() || it->second.lastUse < oldest->second.lastUse))
						{
							oldest = it;
						}
					}

					if (oldest == m_entries.end())
					{
						break;
					}

					unusedBytes -= oldest->second.bytes;
					m_entries.erase(oldest);
				}
			}

			std::mutex m_mutex;
			std::unordered_map<const WorldModel*, Entry> m_entries;
			std::unordered_map<const WorldModel*, std::unique_ptr<PendingBuild>> m_builders;
			size_t m_retentionBytes = 0;
			uint64 m_useCounter = 0;
		};

		MergedGeometryCache& GetMergedGeometryCache()
		{
			static MergedGeometryCache s_cache;
			return s_cache;
		}

		std::shared_ptr<const WorldModelMergedGeometry> GetOrBuildMerged(const WorldModelPtr& model)
		{
			return GetMergedGeometryCache().GetOrBuild(model);
		}
	}

	std::shared_ptr<const void> WorldModelInstance::PrewarmMergedGeometry(const WorldModelPtr& model, const size_t maxGroups)
	{
		if (!model)
		{
			return nullptr;
		}

		return GetMergedGeometryCache().Advance(model, std::max<size_t>(1, maxGroups));
	}

	void WorldModelInstance::SetMergedGeometryRetention(const size_t bytes)
	{
		GetMergedGeometryCache().SetRetention(bytes);
	}

	bool WorldModelInstance::CanMergeMesh(const Mesh& mesh)
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

			if (!HasMergeableLayout(*subMesh.vertexData))
			{
				return false;
			}
		}

		return true;
	}

	void WorldModelInstance::BuildMergedGeometry(Scene& scene)
	{
		PROFILE_SCOPE("WMO: BuildMergedGeometry");
		if (!m_worldModel)
		{
			return;
		}

		for (size_t i = 0; i < m_worldModel->GetGroupCount(); ++i)
		{
			GroupRenderable renderable;
			renderable.groupIndex = i;
			m_groupRenderables.push_back(std::move(renderable));
		}

		m_mergedGeometry = GetOrBuildMerged(m_worldModel);

		SceneNode& parent = m_parentNode ? *static_cast<SceneNode*>(m_parentNode) : scene.GetRootSceneNode();
		std::vector<WorldModelBucket> collisionBuckets;

		for (size_t groupIndex = 0; groupIndex < m_mergedGeometry->groups.size(); ++groupIndex)
		{
			const auto& group = m_mergedGeometry->groups[groupIndex];

			const auto addChunk = [&](const MeshPtr& mesh, const char* suffix, const bool isDetail)
			{
				if (!mesh)
				{
					return;
				}

				MeshVisualization viz;
				viz.mesh = mesh;

				// The merged vertices are already in world model space, so the node carries no transform
				// of its own and simply follows the placement node.
				const String name = m_name + "_group" + std::to_string(groupIndex) + suffix;
				viz.node = &scene.CreateSceneNode(name);
				parent.AddChild(*viz.node);

				viz.entity = scene.CreateEntity(name + "_entity", mesh);
				if (viz.entity)
				{
					// The merged mesh has no collision tree, so the query flags only serve picking in the
					// world editor; movement and camera collision come from the proxies below.
					viz.entity->SetQueryFlags(GetQueryFlags());
					if (isDetail)
					{
						viz.entity->SetRenderingDistance(WorldModelDetailDistance);
						viz.entity->SetCastShadows(false);
					}
					viz.node->AttachObject(*viz.entity);
				}

				m_groupRenderables[groupIndex].meshVisualizations.push_back(std::move(viz));
			};

			addChunk(group.structure, "_merged", false);
			addChunk(group.detail, "_detail", true);

			for (const size_t refIndex : group.fallbackRefs)
			{
				CreateMeshRefEntity(groupIndex, refIndex, scene);
			}

			const auto& meshRefs = m_worldModel->GetGroup(groupIndex)->GetMeshRefs();
			for (const size_t refIndex : group.mergedRefs)
			{
				const WorldModelMeshRef& meshRef = meshRefs[refIndex];

				WorldModelBucket bucket;
				bucket.key.groupIndex = groupIndex;
				bucket.key.meshPath = meshRef.meshPath;
				bucket.key.materialOverride = meshRef.materialOverride;

				WorldModelPlacementInput placement;
				placement.groupIndex = groupIndex;
				placement.sourceIndex = refIndex;
				placement.meshPath = meshRef.meshPath;
				placement.materialOverride = meshRef.materialOverride;
				placement.position = meshRef.position;
				placement.rotation = meshRef.rotation;
				placement.scale = meshRef.scale;
				bucket.placements.push_back(std::move(placement));
				collisionBuckets.push_back(std::move(bucket));
			}
		}

		EnsureBatchRootNode(scene);
		BuildCollisionProxies(scene, collisionBuckets, m_collisionProxies);
		m_batchesDirty = true;
	}
}
