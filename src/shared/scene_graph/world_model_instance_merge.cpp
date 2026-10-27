// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_instance.h"
#include "world_model_merge.h"
#include "world_model_flatten.h"
#include "world_model_manager.h"
#include "mesh_merge.h"

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

	/// @brief The outdoor geometry of a top-level world model and every child world model it nests,
	///        merged per cluster cell (see WorldModelClusterSize).
	struct WorldModelClusterGeometry
	{
		struct Cluster
		{
			/// @brief Cell centre in top-level world model space; the meshes are relative to it.
			Vector3 origin;

			/// @brief Merged meshes; more than one only when a material exceeds the vertex limit.
			std::vector<MeshPtr> structure;

			/// @brief Merged small placements (IsWorldModelDetail), drawn close up only, casting no shadow.
			std::vector<MeshPtr> detail;
		};

		std::vector<Cluster> clusters;
	};

	namespace
	{
		/// @brief Merges a world model's groups, all at once or a few groups per call.
		class MergedGeometryBuilder final
		{
		public:
			explicit MergedGeometryBuilder(const WorldModelPtr& model)
				: m_model(*model)
				, m_merged(std::make_shared<WorldModelMergedGeometry>())
			{
				m_merged->groups.resize(model->GetGroupCount());
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
				m_readBack.Clear();
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
				MeshMergeBuckets structure;
				MeshMergeBuckets detail;

				// Outdoor groups are merged per area into the top-level instance's clusters instead; their
				// placements still need collision proxies, and unmergeable ones still their own entities.
				const bool clustered = WorldModelInstance::IsClusteredGroup(*group);

				const auto& meshRefs = group->GetMeshRefs();
				for (size_t refIndex = 0; refIndex < meshRefs.size(); ++refIndex)
				{
					const WorldModelMeshRef& meshRef = meshRefs[refIndex];

					// Particle systems (.hpar) are created by WorldModelInstance::CreateParticleSystems
					if (!meshRef.visible || IsParticleSystemPath(meshRef.meshPath))
					{
						continue;
					}

					const MeshPtr mesh = MeshManager::Get().Load(meshRef.meshPath);
					if (!mesh || !WorldModelInstance::CanMergeMesh(*mesh))
					{
						out.fallbackRefs.push_back(refIndex);
						continue;
					}

					if (clustered)
					{
						out.mergedRefs.push_back(refIndex);
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
						const MeshMergeGeometry* geometry = readBack.Get(*mesh, subIndex);
						if (!geometry)
						{
							complete = false;
							break;
						}

						const MaterialPtr material = overrideMaterial ? overrideMaterial : subMesh.GetMaterial();
						const String materialName = material ? String(material->GetName()) : String();
						MeshMergeBucket& bucket = chunk[materialName];
						bucket.material = material;
						AppendTransformedGeometry(geometry->vertices.data(), geometry->vertices.size(),
							geometry->indices.data(), geometry->indices.size(), transform, bucket.vertices, bucket.indices);
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
			MeshReadBackCache m_readBack;
		};

		/// @brief Merges the clustered groups of a world model tree per cluster cell, a few groups per call.
		class ClusterGeometryBuilder final
		{
		public:
			explicit ClusterGeometryBuilder(const WorldModelPtr& root)
				: m_rootKey(reinterpret_cast<uintptr_t>(root.get()))
				, m_result(std::make_shared<WorldModelClusterGeometry>())
			{
				Collect(*root, FlattenTransform{}, {});
			}

			/// @brief Merges up to maxGroups more groups. Returns true once every group is merged.
			bool Step(const size_t maxGroups)
			{
				PROFILE_SCOPE("WMO: BuildClusters");
				for (size_t built = 0; built < maxGroups && m_nextWork < m_work.size(); ++built)
				{
					MergeGroup(m_work[m_nextWork++]);
				}

				if (m_nextWork < m_work.size())
				{
					return false;
				}

				if (!m_finished)
				{
					Finish();
					m_finished = true;
				}

				return true;
			}

			[[nodiscard]] const std::shared_ptr<WorldModelClusterGeometry>& GetResult() const { return m_result; }

		private:
			struct Work
			{
				/// The root (alive while the cache steps it) or a child held in m_children
				const WorldModel* model { nullptr };
				FlattenTransform toRoot;
				size_t groupIndex { 0 };
			};

			struct Cell
			{
				std::unique_ptr<MeshMergeAccumulator> structure;
				std::unique_ptr<MeshMergeAccumulator> detail;
			};

			/// Lists the clustered groups of a model and, by the rules CreateChildInstances instantiates
			/// children with, of its nested children, so exactly what the instances show is covered.
			void Collect(const WorldModel& model, const FlattenTransform& toRoot, const std::vector<const WorldModel*>& ancestors)
			{
				for (size_t groupIndex = 0; groupIndex < model.GetGroupCount(); ++groupIndex)
				{
					const WorldModelGroup* group = model.GetGroup(groupIndex);
					if (group && WorldModelInstance::IsClusteredGroup(*group))
					{
						m_work.push_back(Work{ &model, toRoot, groupIndex });
					}
				}

				if (ancestors.size() >= WorldModelInstance::MaxChildDepth)
				{
					return;
				}

				std::vector<const WorldModel*> chain = ancestors;
				chain.push_back(&model);

				for (const WorldModelChildRef& childRef : model.GetChildRefs())
				{
					if (!childRef.visible || childRef.wmoPath.empty())
					{
						continue;
					}

					WorldModelPtr child = WorldModelManager::Get().Load(childRef.wmoPath);
					if (!child || std::find(chain.begin(), chain.end(), child.get()) != chain.end())
					{
						continue;
					}

					const WorldModel& childModel = *child;
					m_children.push_back(std::move(child));
					Collect(childModel, toRoot.Compose(childRef.position, childRef.rotation, childRef.scale), chain);
				}
			}

			void MergeGroup(const Work& work)
			{
				const WorldModelGroup* group = work.model->GetGroup(work.groupIndex);
				if (!group)
				{
					return;
				}

				for (const WorldModelMeshRef& meshRef : group->GetMeshRefs())
				{
					// The same placements the per-group merge counts as merged (it gives them collision);
					// the rest keep their own entities in the instance owning the group
					if (!meshRef.visible || IsParticleSystemPath(meshRef.meshPath))
					{
						continue;
					}

					const MeshPtr mesh = MeshManager::Get().Load(meshRef.meshPath);
					if (!mesh || !WorldModelInstance::CanMergeMesh(*mesh))
					{
						continue;
					}

					const FlattenTransform placement = work.toRoot.Compose(meshRef.position, meshRef.rotation, meshRef.scale);
					const auto cellIndex = GetWorldModelClusterCell(placement.position);
					Cell& cell = m_cells[cellIndex];

					const float maxScale = std::max({ std::abs(placement.scale.x), std::abs(placement.scale.y), std::abs(placement.scale.z) });
					auto& accumulator = IsWorldModelDetail(mesh->GetBoundRadius() * maxScale) ? cell.detail : cell.structure;
					if (!accumulator)
					{
						accumulator = std::make_unique<MeshMergeAccumulator>(m_readBack);
					}

					// A mesh reference's override replaces the material of every submesh
					const std::vector<String> overrides = meshRef.materialOverride.empty()
						? std::vector<String>()
						: std::vector<String>(mesh->GetSubMeshCount(), meshRef.materialOverride);

					const Matrix4 transform = Matrix4::GetTrans(-GetWorldModelClusterOrigin(cellIndex)) * placement.ToMatrix();
					if (!accumulator->Add(*mesh, transform, overrides))
					{
						WLOG("Could not merge mesh " << meshRef.meshPath << " completely into its world model cluster");
					}
				}
			}

			void Finish()
			{
				const String prefix = "WorldModelCluster_" + std::to_string(m_rootKey) + "_";
				for (auto& [index, cell] : m_cells)
				{
					WorldModelClusterGeometry::Cluster cluster;
					cluster.origin = GetWorldModelClusterOrigin(index);

					const String name = prefix + std::to_string(index.first) + "_" + std::to_string(index.second);
					const auto createParts = [&name](const std::unique_ptr<MeshMergeAccumulator>& accumulator, const char* suffix, std::vector<MeshPtr>& out)
					{
						if (!accumulator)
						{
							return;
						}

						auto& parts = accumulator->GetParts();
						for (size_t part = 0; part < parts.size(); ++part)
						{
							if (MeshPtr mesh = CreateMergedMesh(name + suffix + std::to_string(part), parts[part]))
							{
								out.push_back(std::move(mesh));
							}
						}
					};

					createParts(cell.structure, "_structure", cluster.structure);
					createParts(cell.detail, "_detail", cluster.detail);
					if (!cluster.structure.empty() || !cluster.detail.empty())
					{
						m_result->clusters.push_back(std::move(cluster));
					}
				}

				// Only needed while merging
				m_cells.clear();
				m_readBack.Clear();
				m_children.clear();
			}

			uintptr_t m_rootKey;
			std::shared_ptr<WorldModelClusterGeometry> m_result;
			std::vector<Work> m_work;
			size_t m_nextWork = 0;
			bool m_finished = false;
			std::vector<WorldModelPtr> m_children;

			/// Declared before the cells, whose accumulators read through it
			MeshReadBackCache m_readBack;
			std::map<std::pair<int32, int32>, Cell> m_cells;
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

		size_t GetGeometryBytes(const WorldModelMergedGeometry& geometry)
		{
			size_t bytes = 0;
			for (const auto& group : geometry.groups)
			{
				bytes += GetMeshBytes(group.structure) + GetMeshBytes(group.detail);
			}
			return bytes;
		}

		size_t GetGeometryBytes(const WorldModelClusterGeometry& geometry)
		{
			size_t bytes = 0;
			for (const auto& cluster : geometry.clusters)
			{
				for (const MeshPtr& mesh : cluster.structure)
				{
					bytes += GetMeshBytes(mesh);
				}
				for (const MeshPtr& mesh : cluster.detail)
				{
					bytes += GetMeshBytes(mesh);
				}
			}
			return bytes;
		}

		/// @brief Geometry built from a world model (per group merges, or the clusters of a whole tree),
		///        shared by all live instances of it, and kept for a while after the last one is gone
		///        (see WorldModelInstance::SetMergedGeometryRetention).
		template <typename Builder, typename Geometry>
		class MergedGeometryCache final
		{
		public:
			/// @brief Returns the model's merged geometry, merging whatever is not merged yet.
			std::shared_ptr<const Geometry> GetOrBuild(const WorldModelPtr& model)
			{
				return Advance(model, std::numeric_limits<size_t>::max());
			}

			/// @brief Merges up to maxGroups more groups of the model. Returns its merged geometry once
			///        complete, null while groups remain.
			std::shared_ptr<const Geometry> Advance(const WorldModelPtr& model, const size_t maxGroups)
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

				std::shared_ptr<const Geometry> built = builder->builder.GetResult();
				m_builders.erase(model.get());

				Entry entry;
				entry.model = model;
				entry.weak = built;
				entry.lastUse = ++m_useCounter;
				entry.bytes = GetGeometryBytes(*built);
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
					, builder(source)
				{
				}

				std::weak_ptr<WorldModel> model;
				Builder builder;
			};

			struct Entry
			{
				std::weak_ptr<WorldModel> model;
				std::weak_ptr<const Geometry> weak;

				/// Set while retention is on: keeps the geometry alive after its last instance.
				std::shared_ptr<const Geometry> strong;

				size_t bytes = 0;
				uint64 lastUse = 0;

				[[nodiscard]] std::shared_ptr<const Geometry> Get() const
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

		using GroupGeometryCache = MergedGeometryCache<MergedGeometryBuilder, WorldModelMergedGeometry>;
		using ClusterGeometryCache = MergedGeometryCache<ClusterGeometryBuilder, WorldModelClusterGeometry>;

		GroupGeometryCache& GetMergedGeometryCache()
		{
			static GroupGeometryCache s_cache;
			return s_cache;
		}

		ClusterGeometryCache& GetClusterGeometryCache()
		{
			static ClusterGeometryCache s_cache;
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

	std::shared_ptr<const void> WorldModelInstance::PrewarmClusterGeometry(const WorldModelPtr& model, const size_t maxGroups)
	{
		if (!model)
		{
			return nullptr;
		}

		return GetClusterGeometryCache().Advance(model, std::max<size_t>(1, maxGroups));
	}

	void WorldModelInstance::SetMergedGeometryRetention(const size_t bytes)
	{
		// Each cache gets the budget: the clusters of a town hold about what its groups did before
		GetMergedGeometryCache().SetRetention(bytes);
		GetClusterGeometryCache().SetRetention(bytes);
	}

	bool WorldModelInstance::IsClusteredGroup(const WorldModelGroup& group)
	{
		return !group.IsInterior() && group.GetContainmentVolumes().empty();
	}

	bool WorldModelInstance::CanMergeMesh(const Mesh& mesh)
	{
		return mmo::CanMergeMesh(mesh);
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

		// The outdoor groups of this model and of every nested child are drawn by the top-level instance
		if (m_ancestors.empty())
		{
			BuildClusterGeometry(scene);
		}
	}

	void WorldModelInstance::BuildClusterGeometry(Scene& scene)
	{
		PROFILE_SCOPE("WMO: BuildClusterGeometry");

		m_clusterGeometry = GetClusterGeometryCache().GetOrBuild(m_worldModel);
		if (!m_clusterGeometry)
		{
			return;
		}

		SceneNode& parent = m_parentNode ? *static_cast<SceneNode*>(m_parentNode) : scene.GetRootSceneNode();

		// Distance culling measures from the node, which sits at the cell centre: a detail placement in
		// a corner of the cell must not vanish before it is WorldModelDetailDistance away
		constexpr float cellHalfDiagonal = WorldModelClusterSize * 0.70711f;

		for (size_t clusterIndex = 0; clusterIndex < m_clusterGeometry->clusters.size(); ++clusterIndex)
		{
			const auto& cluster = m_clusterGeometry->clusters[clusterIndex];

			const auto addMesh = [&](const MeshPtr& mesh, const String& suffix, const bool isDetail)
			{
				MeshVisualization viz;
				viz.mesh = mesh;

				const String name = m_name + "_cluster" + std::to_string(clusterIndex) + suffix;
				viz.node = &scene.CreateSceneNode(name);
				viz.node->SetPosition(cluster.origin);
				parent.AddChild(*viz.node);

				viz.entity = scene.CreateEntity(name + "_entity", mesh);
				if (viz.entity)
				{
					// Like the merged group meshes: no collision tree, the query flags only serve picking
					// in the world editor; collision comes from each instance's proxies.
					viz.entity->SetQueryFlags(GetQueryFlags());
					if (isDetail)
					{
						viz.entity->SetRenderingDistance(WorldModelDetailDistance + cellHalfDiagonal);
						viz.entity->SetCastShadows(false);
					}
					viz.node->AttachObject(*viz.entity);
				}

				m_clusterRenderables.push_back(std::move(viz));
			};

			for (size_t part = 0; part < cluster.structure.size(); ++part)
			{
				addMesh(cluster.structure[part], "_structure" + std::to_string(part), false);
			}
			for (size_t part = 0; part < cluster.detail.size(); ++part)
			{
				addMesh(cluster.detail[part], "_detail" + std::to_string(part), true);
			}
		}
	}

	void WorldModelInstance::UpdateClusterVisibility(const PortalInteriorView* interiorView)
	{
		for (const MeshVisualization& viz : m_clusterRenderables)
		{
			if (!viz.entity)
			{
				continue;
			}

			// From inside a room, the outdoors is only what its doors show
			bool visible = interiorView == nullptr;
			if (!visible)
			{
				const AABB& bounds = viz.entity->GetWorldBoundingBox(true);
				visible = std::any_of(interiorView->exteriorFrustums.begin(), interiorView->exteriorFrustums.end(),
					[&bounds](const PortalFrustum& frustum) { return frustum.IsVisible(bounds); });
			}

			viz.entity->SetVisible(visible);
		}
	}
}
