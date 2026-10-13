// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "server_collision_map.h"

#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "base/macros.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "game_common/world_entity_loader.h"
#include "game_common/world_foliage.h"
#include "log/default_log_levels.h"
#include "math/quaternion.h"
#include "math/ray.h"

#include <algorithm>
#include <unordered_map>

namespace mmo
{
	// ---------------------------------------------------------------------------
	// Chunk magic constants — derived via MakeChunkMagic for guaranteed match.
	// ---------------------------------------------------------------------------

	static const uint32 s_collMagic     = *MakeChunkMagic('COLL');
	static const uint32 s_wmoGroupMagic = *MakeChunkMagic('MOGP');
	static const uint32 s_wmoMeshMagic  = *MakeChunkMagic('MMRF');

	// ---------------------------------------------------------------------------
	// AddInstance — compute world AABB and inverse transform, then store.
	// ---------------------------------------------------------------------------

	bool ServerCollisionMap::MakeInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform,
		const std::string& debugName, CollisionInstance& outInstance)
	{
		// A transform with a zero component in its authored scale is singular, and InverseAffine
		// divides by the determinant without a singularity check — so the inverse comes out full
		// of inf/NaN. Every ray transformed into this instance's local space would then be NaN,
		// and because NaN compares false against everything (including itself) it slips past both
		// the degenerate-segment guards and Ray's own assert. The instance would block or pass
		// rays at random, with no crash and no log line. Dropping it is the honest failure.
		//
		// The criterion is finiteness of the inverse, not the magnitude of the scale: a
		// legitimately shrunken prop has a tiny determinant but a perfectly usable inverse, and
		// must still collide. That leaves extreme non-uniform scales (one axis near zero while
		// the others are astronomically large) as the one degenerate shape this accepts — no
		// authoring path can produce them.
		if (!transform.IsAffine() || !transform.IsFinite())
		{
			WarnRejectedTransform(debugName, "the transform is not affine or not finite");
			return false;
		}

		const Matrix4 invTransform = transform.InverseAffine();
		if (!invTransform.IsFinite())
		{
			WarnRejectedTransform(debugName, "the transform is not invertible (is a scale component zero?)");
			return false;
		}

		outInstance.tree         = std::move(tree);
		outInstance.transform    = transform;
		outInstance.invTransform = invTransform;

		// Compute world-space AABB by transforming the local AABB.
		outInstance.worldBounds = outInstance.tree->GetBoundingBox();
		outInstance.worldBounds.Transform(transform);

		return true;
	}

	void ServerCollisionMap::WarnRejectedTransform(const std::string& debugName, const char* reason)
	{
		if (!m_warnedTransforms.insert(debugName).second)
		{
			return;
		}

		WLOG("ServerCollisionMap: ignoring collision instances of '" << debugName << "' because "
			<< reason << " — they would corrupt line of sight instead of blocking it");
	}

	bool ServerCollisionMap::AddInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform, const std::string& debugName)
	{
		if (!tree || tree->IsEmpty())
		{
			return false;
		}

		CollisionInstance inst;
		if (!MakeInstance(std::move(tree), transform, debugName, inst))
		{
			return false;
		}

		m_instances.push_back(std::move(inst));
		return true;
	}

	ServerCollisionMap::ServerCollisionMap(std::shared_ptr<const ServerCollisionMap> sharedStatic)
		: m_sharedStatic(std::move(sharedStatic))
	{
	}

	bool ServerCollisionMap::GetDynamicInstanceLocalBounds(const uint64 handle, AABB& out) const
	{
		const auto it = m_dynamicInstances.find(handle);
		if (it == m_dynamicInstances.end() || !it->second.instance.tree)
		{
			return false;
		}

		out = it->second.instance.tree->GetBoundingBox();
		return true;
	}

	bool ServerCollisionMap::AddFoliageInstance(const std::shared_ptr<AABBTree>& tree, const Matrix4& transform, const std::string& debugName)
	{
		return AddInstance(MakeFoliageLineOfSightTree(tree), transform, debugName);
	}

	std::shared_ptr<AABBTree> ServerCollisionMap::MakeFoliageLineOfSightTree(const std::shared_ptr<AABBTree>& tree)
	{
		if (!tree || tree->IsEmpty())
		{
			return nullptr;
		}

		const std::vector<AABBTree::Vertex>& vertices = tree->GetVertices();
		const std::vector<AABBTree::Index>& indices = tree->GetIndices();

		// Keep every triangle that reaches below the trunk height. A triangle spanning the cut
		// is kept whole, so the trunk is never shortened.
		std::vector<AABBTree::Index> trunkIndices;
		trunkIndices.reserve(indices.size());
		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const float lowestY = std::min({ vertices[indices[i]].y, vertices[indices[i + 1]].y, vertices[indices[i + 2]].y });
			if (lowestY < FoliageLineOfSightTrunkHeight)
			{
				trunkIndices.push_back(indices[i]);
				trunkIndices.push_back(indices[i + 1]);
				trunkIndices.push_back(indices[i + 2]);
			}
		}

		if (trunkIndices.size() == indices.size())
		{
			return tree;
		}

		if (trunkIndices.empty())
		{
			return nullptr;
		}

		// Unreferenced vertices above the cut are harmless: the tree only visits indexed faces.
		auto trunkTree = std::make_shared<AABBTree>();
		trunkTree->Build(vertices, trunkIndices);
		return trunkTree;
	}

	// ---------------------------------------------------------------------------
	// Dynamic instances — toggleable collision for spawned world objects (doors).
	// Single-thread contract: the world server runs its io_service single threaded
	// (maxNetworkThreads = 0), so mutation and LoS queries never race.
	// ---------------------------------------------------------------------------

	uint64 ServerCollisionMap::AddDynamicInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform, const bool enabled,
		const std::string& debugName)
	{
		if (!tree || tree->IsEmpty())
		{
			return 0;
		}

		DynamicInstance dyn;
		if (!MakeInstance(std::move(tree), transform, debugName, dyn.instance))
		{
			return 0;
		}

		dyn.enabled = enabled;

		const uint64 handle = m_nextDynamicHandle++;
		m_dynamicInstances.emplace(handle, std::move(dyn));

		return handle;
	}

	uint64 ServerCollisionMap::AddDynamicInstanceFromMesh(const std::string& meshPath, const Matrix4& transform, const bool enabled)
	{
		auto it = m_meshTreeCache.find(meshPath);
		if (it == m_meshTreeCache.end())
		{
			it = m_meshTreeCache.emplace(meshPath, LoadMeshTree(meshPath)).first;
		}

		if (!it->second || it->second->IsEmpty())
		{
			return 0;
		}

		return AddDynamicInstance(it->second, transform, enabled, meshPath);
	}

	void ServerCollisionMap::RemoveDynamicInstance(const uint64 handle)
	{
		// Only drops this instance — the AABBTree is shared per mesh path and may still be
		// referenced by other instances and the cache.
		m_dynamicInstances.erase(handle);
	}

	void ServerCollisionMap::SetDynamicInstanceEnabled(const uint64 handle, const bool enabled)
	{
		const auto it = m_dynamicInstances.find(handle);
		if (it != m_dynamicInstances.end())
		{
			it->second.enabled = enabled;
		}
	}

	// ---------------------------------------------------------------------------
	// LoadMeshTree — reads the COLL top-level chunk from a .mesh file.
	// The mesh file layout is a FLAT sequence of top-level chunks:
	//   MESH (version uint32 only — NOT a container for the rest)
	//   VERT, SKEL, COLL, SUBM×N, TAGS  (all at the same level as MESH)
	// ---------------------------------------------------------------------------

	std::shared_ptr<AABBTree> ServerCollisionMap::LoadMeshTree(const std::string& meshPath)
	{
		auto file = AssetRegistry::OpenFile(meshPath);
		if (!file)
		{
			return nullptr;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		while (reader)
		{
			uint32 chunkId, chunkSize;
			if (!(reader >> io::read<uint32>(chunkId) >> io::read<uint32>(chunkSize)))
			{
				break;
			}

			if (chunkId == s_collMagic)
			{
				auto tree = std::make_shared<AABBTree>();
				reader >> *tree;

				if (reader && !tree->IsEmpty())
				{
					return tree;
				}
				return nullptr;
			}

			reader >> io::skip(chunkSize);
		}

		return nullptr;
	}

	// ---------------------------------------------------------------------------
	// LoadWorldModelInstances — opens a .hwmo file, walks MOGP group chunks,
	// reads MMRF mesh-ref sub-chunks, and registers one CollisionInstance per
	// visible mesh ref that has a collision tree.
	// ---------------------------------------------------------------------------

	void ServerCollisionMap::LoadWorldModelInstances(const std::string& hwmoPath,
	                                                  const Matrix4& instanceTransform)
	{
		auto file = AssetRegistry::OpenFile(hwmoPath);
		if (!file)
		{
			WLOG("ServerCollisionMap: could not open world model: " << hwmoPath);
			return;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };

		// Cache mesh trees within this WMO to avoid re-loading the same mesh.
		std::unordered_map<std::string, std::shared_ptr<AABBTree>> localMeshCache;

		while (reader)
		{
			uint32 chunkId, chunkSize;
			if (!(reader >> io::read<uint32>(chunkId) >> io::read<uint32>(chunkSize)))
			{
				break;
			}

			const size_t chunkDataStart = reader.getSource()->position();

			if (chunkId == s_wmoGroupMagic)
			{
				// MOGP fixed header is 68 bytes, followed by sub-chunks.
				constexpr size_t k_groupHeaderSize = 68;
				if (chunkSize < k_groupHeaderSize)
				{
					reader >> io::skip(chunkSize);
					continue;
				}

				reader >> io::skip(k_groupHeaderSize);

				const size_t groupEnd = chunkDataStart + chunkSize;

				while (reader && reader.getSource()->position() < groupEnd)
				{
					uint32 subId, subSize;
					if (!(reader >> io::read<uint32>(subId) >> io::read<uint32>(subSize)))
					{
						break;
					}

					if (subId == s_wmoMeshMagic)
					{
						uint32 count = 0;
						reader >> io::read<uint32>(count);

						for (uint32 i = 0; i < count && reader; ++i)
						{
							// meshPath: uint32 pathLen, then (pathLen+1) bytes including null
							uint32 pathLen = 0;
							reader >> io::read<uint32>(pathLen);
							std::string meshPath(pathLen + 1, '\0');
							reader.getSource()->read(&meshPath[0], pathLen + 1);
							if (!meshPath.empty() && meshPath.back() == '\0')
							{
								meshPath.resize(pathLen);
							}

							// name: skip
							uint32 nameLen = 0;
							reader >> io::read<uint32>(nameLen);
							reader >> io::skip(nameLen + 1);

							// material override: skip
							uint32 matLen = 0;
							reader >> io::read<uint32>(matLen);
							reader >> io::skip(matLen + 1);

							// transform + visible flag
							float px, py, pz, rw, rx, ry, rz, sx, sy, sz;
							uint8 visible = 1;
							reader
								>> io::read<float>(px) >> io::read<float>(py) >> io::read<float>(pz)
								>> io::read<float>(rw) >> io::read<float>(rx)
								>> io::read<float>(ry) >> io::read<float>(rz)
								>> io::read<float>(sx) >> io::read<float>(sy) >> io::read<float>(sz)
								>> io::read<uint8>(visible);

							if (!visible || meshPath.empty())
							{
								continue;
							}

							// Load (or reuse) the mesh collision tree.
							auto it = localMeshCache.find(meshPath);
							if (it == localMeshCache.end())
							{
								it = localMeshCache.emplace(meshPath, LoadMeshTree(meshPath)).first;
							}

							if (!it->second || it->second->IsEmpty())
							{
								continue;
							}

							// Combined transform: world-instance × mesh-ref-local
							Matrix4 meshRefTransform;
							meshRefTransform.MakeTransform(
								Vector3(px, py, pz),
								Vector3(sx, sy, sz),
								Quaternion(rw, rx, ry, rz));

							AddInstance(it->second, instanceTransform * meshRefTransform, meshPath);
						}
					}
					else
					{
						reader >> io::skip(subSize);
					}
				}
			}
			else
			{
				reader >> io::skip(chunkSize);
			}
		}
	}

	// ---------------------------------------------------------------------------
	// Constructor — load all entity placements for the map.
	// ---------------------------------------------------------------------------

	ServerCollisionMap::ServerCollisionMap(const std::string& mapName)
	{
		const std::string entityDir = "Worlds/" + mapName + "/" + mapName + "/Entities/";
		const std::vector<std::string> entityFiles = AssetRegistry::ListFiles(entityDir, "wobj");

		if (entityFiles.empty())
		{
			WLOG("ServerCollisionMap: no entity files found under '" << entityDir
				<< "' — collision geometry will not be available for map '" << mapName << "'");
			return;
		}

		DLOG("ServerCollisionMap: loading " << entityFiles.size()
			<< " entity files for map '" << mapName << "'");

		// Shared mesh tree cache — one AABBTree per unique mesh path.
		std::unordered_map<std::string, std::shared_ptr<AABBTree>> meshCache;

		uint32 meshCount = 0, wmoCount = 0;
		uint32 meshWithCollision = 0, wmoWithCollision = 0;
		uint32 readFailCount = 0;

		for (const auto& entityFilename : entityFiles)
		{
			auto entityFile = AssetRegistry::OpenFile(entityFilename);
			if (!entityFile)
			{
				++readFailCount;
				continue;
			}

			io::StreamSource entitySource{ *entityFile };
			io::Reader entityReader{ entitySource };

			WorldEntityLoader loader;
			if (!loader.Read(entityReader))
			{
				WLOG("ServerCollisionMap: failed to read entity file: " << entityFilename);
				++readFailCount;
				continue;
			}

			const auto& placement = loader.GetEntity();

			Matrix4 instanceTransform;
			instanceTransform.MakeTransform(placement.position, placement.scale, placement.rotation);

			if (placement.entityType == WorldEntityType::WorldModel)
			{
				++wmoCount;
				const size_t before = m_instances.size();
				LoadWorldModelInstances(placement.meshName, instanceTransform);
				if (m_instances.size() > before)
				{
					++wmoWithCollision;
				}
			}
			else
			{
				++meshCount;

				auto it = meshCache.find(placement.meshName);
				if (it == meshCache.end())
				{
					it = meshCache.emplace(placement.meshName,
					     LoadMeshTree(placement.meshName)).first;
				}

				if (!it->second || it->second->IsEmpty())
				{
					continue;
				}

				if (AddInstance(it->second, instanceTransform, placement.meshName))
				{
					++meshWithCollision;
				}
			}
		}

		// -----------------------------------------------------------------------
		// Authored instanced foliage (trees) — same collision treatment as mesh
		// entities, but stored per-page as .hfol files holding many instances each.
		// -----------------------------------------------------------------------
		const std::string foliageDir = "Worlds/" + mapName + "/" + mapName + "/Foliage/";
		const std::vector<std::string> foliageFiles = AssetRegistry::ListFiles(foliageDir, "hfol");

		uint32 foliageInstanceCount = 0, foliageWithCollision = 0;

		// Line of sight trees of foliage meshes (trunk only), one per unique mesh path.
		std::unordered_map<std::string, std::shared_ptr<AABBTree>> foliageLosCache;

		for (const auto& foliageFilename : foliageFiles)
		{
			auto foliageFile = AssetRegistry::OpenFile(foliageFilename);
			if (!foliageFile)
			{
				++readFailCount;
				continue;
			}

			io::StreamSource foliageSource{ *foliageFile };
			io::Reader foliageReader{ foliageSource };

			WorldFoliageLoader foliageLoader;
			if (!foliageLoader.Read(foliageReader))
			{
				WLOG("ServerCollisionMap: failed to read foliage file: " << foliageFilename);
				++readFailCount;
				continue;
			}

			for (const auto& instance : foliageLoader.GetInstances())
			{
				++foliageInstanceCount;

				// Honor the per-instance collision flag - decorative foliage stays walkable server-side.
				if (!instance.collides)
				{
					continue;
				}

				// Foliage only blocks line of sight with its trunk, so it gets its own cache of
				// filtered trees: the same mesh placed as an entity still blocks with all of it.
				auto it = foliageLosCache.find(instance.meshName);
				if (it == foliageLosCache.end())
				{
					it = foliageLosCache.emplace(instance.meshName, MakeFoliageLineOfSightTree(LoadMeshTree(instance.meshName))).first;
				}

				if (!it->second || it->second->IsEmpty())
				{
					continue;
				}

				Matrix4 instanceTransform;
				instanceTransform.MakeTransform(instance.position, instance.scale, instance.rotation);

				if (AddInstance(it->second, instanceTransform, instance.meshName))
				{
					++foliageWithCollision;
				}
			}
		}

		if (foliageInstanceCount > 0)
		{
			DLOG("ServerCollisionMap: " << foliageInstanceCount << " foliage instances ("
				<< foliageWithCollision << " with collision)");
		}

		DLOG("ServerCollisionMap: " << meshCount << " mesh entities (" << meshWithCollision
			<< " with collision), " << wmoCount << " WMO entities (" << wmoWithCollision
			<< " with collision), " << readFailCount << " read failures");

		if (m_instances.empty())
		{
			WLOG("ServerCollisionMap: no collision instances for map '" << mapName << "'");
		}
		else
		{
			DLOG("ServerCollisionMap: " << m_instances.size()
				<< " collision instances ready for map '" << mapName << "'");
		}
	}

	// ---------------------------------------------------------------------------
	// Ray testing
	// ---------------------------------------------------------------------------

	namespace
	{
		/// Squared length below which a line of sight segment is considered degenerate.
		/// Matches the threshold used by nav::Map::LineOfSightEx (1 cm).
		constexpr float s_minLosSegmentLengthSq = 0.0001f;

		/// A segment this short has no meaningful direction to normalize, and no geometry can
		/// fit between its two ends — so the two points always see each other. Two units standing
		/// on the exact same spot (e.g. an add summoned at its summoner's position) hit this.
		bool isDegenerateSegment(const Vector3& from, const Vector3& to)
		{
			return (to - from).GetSquaredLength() < s_minLosSegmentLengthSq;
		}

		/// Static world geometry is closed, so its back faces are skipped. Dynamic instances
		/// (doors, barriers) are tested from both sides: their art is often a single-sided
		/// plane, and a closed door must block sight from the far side as well.
		constexpr RaycastFlags s_staticRayFlags = raycast_flags::IgnoreBackface;
		constexpr RaycastFlags s_dynamicRayFlags = raycast_flags::None;

		/// Tests a single collision instance for any intersection with the given world ray.
		bool instanceBlocksRay(const CollisionInstance& inst, const Ray& worldRay, const Vector3& from, const Vector3& to,
			const RaycastFlags flags)
		{
			// Fast world-AABB rejection.
			const auto [aabbHit, aabbT] = worldRay.IntersectsAABB(inst.worldBounds);
			if (!aabbHit || aabbT > worldRay.GetLength())
			{
				return false;
			}

			// Transform ray to local (mesh) space.
			const Vector3 localFrom = inst.invTransform * from;
			const Vector3 localTo   = inst.invTransform * to;

			if (localFrom == localTo)
			{
				return false;
			}

			Ray localRay(localFrom, localTo);
			return inst.tree->IntersectRay(localRay, nullptr,
				static_cast<RaycastFlags>(raycast_flags::EarlyExit | flags));
		}

		/// Tests a single collision instance and tracks the closest world-space hit.
		void intersectInstanceEx(const CollisionInstance& inst, const Ray& worldRay, const Vector3& from,
			const Vector3& to, const float worldLen, const RaycastFlags flags, float& closestWorldT, Vector3& hitPoint, bool& blocked)
		{
			const auto [aabbHit, aabbT] = worldRay.IntersectsAABB(inst.worldBounds);
			if (!aabbHit || aabbT > worldLen * closestWorldT)
			{
				return;
			}

			const Vector3 localFrom = inst.invTransform * from;
			const Vector3 localTo   = inst.invTransform * to;

			if (localFrom == localTo)
			{
				return;
			}

			Ray localRay(localFrom, localTo);
			if (!inst.tree->IntersectRay(localRay, nullptr, flags))
			{
				return;
			}

			// Reconstruct world-space hit point via the forward transform.
			const Vector3 localHit = localFrom + (localTo - localFrom) * localRay.hitDistance;
			const Vector3 worldHit = inst.transform * localHit;
			const float worldT = (worldLen > 0.f)
				? (worldHit - from).GetLength() / worldLen
				: 0.f;

			if (worldT < closestWorldT)
			{
				closestWorldT = worldT;
				hitPoint = worldHit;
				blocked = true;
			}
		}
	}

	bool ServerCollisionMap::LineOfSight(const Vector3& from, const Vector3& to) const
	{
		if (m_instances.empty() && m_dynamicInstances.empty() && !m_sharedStatic)
		{
			return true;
		}

		if (isDegenerateSegment(from, to))
		{
			return true;
		}

		const Ray worldRay(from, to);

		if (m_sharedStatic && !m_sharedStatic->LineOfSight(from, to))
		{
			return false; // blocked by shared static geometry
		}

		for (const auto& inst : m_instances)
		{
			if (instanceBlocksRay(inst, worldRay, from, to, s_staticRayFlags))
			{
				return false; // blocked
			}
		}

		for (const auto& [handle, dyn] : m_dynamicInstances)
		{
			if (dyn.enabled && instanceBlocksRay(dyn.instance, worldRay, from, to, s_dynamicRayFlags))
			{
				return false; // blocked
			}
		}

		return true;
	}

	bool ServerCollisionMap::LineOfSightEx(const Vector3& from, const Vector3& to, Vector3& hitPoint) const
	{
		hitPoint = to;

		if (m_instances.empty() && m_dynamicInstances.empty() && !m_sharedStatic)
		{
			return true;
		}

		if (isDegenerateSegment(from, to))
		{
			return true;
		}

		const Ray worldRay(from, to);
		const float worldLen = worldRay.GetLength();

		float closestWorldT = 1.0f;
		bool blocked = false;

		if (m_sharedStatic)
		{
			Vector3 sharedHit;
			if (!m_sharedStatic->LineOfSightEx(from, to, sharedHit))
			{
				const float sharedT = worldLen > 0.f ? (sharedHit - from).GetLength() / worldLen : 0.f;
				if (sharedT < closestWorldT)
				{
					closestWorldT = sharedT;
					hitPoint = sharedHit;
					blocked = true;
				}
			}
		}

		for (const auto& inst : m_instances)
		{
			intersectInstanceEx(inst, worldRay, from, to, worldLen, s_staticRayFlags, closestWorldT, hitPoint, blocked);
		}

		for (const auto& [handle, dyn] : m_dynamicInstances)
		{
			if (dyn.enabled)
			{
				intersectInstanceEx(dyn.instance, worldRay, from, to, worldLen, s_dynamicRayFlags, closestWorldT, hitPoint, blocked);
			}
		}

		return !blocked;
	}
}
