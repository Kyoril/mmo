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

#include <algorithm>
#include <cstring>
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

		std::shared_ptr<WorldModelMergedGeometry> BuildMerged(const WorldModel& model)
		{
			auto merged = std::make_shared<WorldModelMergedGeometry>();
			merged->groups.resize(model.GetGroupCount());

			// Read back every distinct submesh once, however often it is placed
			std::map<std::pair<const Mesh*, uint16>, SubMeshGeometry> readBack;

			for (size_t groupIndex = 0; groupIndex < model.GetGroupCount(); ++groupIndex)
			{
				const WorldModelGroup* group = model.GetGroup(groupIndex);
				if (!group)
				{
					continue;
				}

				auto& out = merged->groups[groupIndex];
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

			return merged;
		}

		/// @brief Merged geometry per world model, shared by all live instances of it.
		std::shared_ptr<const WorldModelMergedGeometry> GetOrBuildMerged(const WorldModelPtr& model)
		{
			static std::mutex s_mutex;
			static std::unordered_map<const WorldModel*, std::weak_ptr<const WorldModelMergedGeometry>> s_cache;

			std::scoped_lock lock(s_mutex);
			auto& slot = s_cache[model.get()];
			if (auto existing = slot.lock())
			{
				return existing;
			}

			std::shared_ptr<const WorldModelMergedGeometry> built = BuildMerged(*model);
			slot = built;
			return built;
		}
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
