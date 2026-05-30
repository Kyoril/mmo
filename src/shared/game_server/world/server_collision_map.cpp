// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "server_collision_map.h"

#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "base/macros.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "game_common/world_entity_loader.h"
#include "log/default_log_levels.h"
#include "math/quaternion.h"
#include "math/ray.h"

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

	void ServerCollisionMap::AddInstance(std::shared_ptr<AABBTree> tree, const Matrix4& transform)
	{
		if (!tree || tree->IsEmpty())
		{
			return;
		}

		CollisionInstance inst;
		inst.tree        = std::move(tree);
		inst.transform   = transform;
		inst.invTransform = transform.InverseAffine();

		// Compute world-space AABB by transforming the local AABB.
		inst.worldBounds = inst.tree->GetBoundingBox();
		inst.worldBounds.Transform(transform);

		m_instances.push_back(std::move(inst));
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

							AddInstance(it->second, instanceTransform * meshRefTransform);
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

				++meshWithCollision;
				AddInstance(it->second, instanceTransform);
			}
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

	bool ServerCollisionMap::LineOfSight(const Vector3& from, const Vector3& to) const
	{
		if (m_instances.empty())
		{
			return true;
		}

		const Ray worldRay(from, to);

		for (const auto& inst : m_instances)
		{
			// Fast world-AABB rejection.
			const auto [aabbHit, aabbT] = worldRay.IntersectsAABB(inst.worldBounds);
			if (!aabbHit || aabbT > worldRay.GetLength())
			{
				continue;
			}

			// Transform ray to local (mesh) space.
			const Vector3 localFrom = inst.invTransform * from;
			const Vector3 localTo   = inst.invTransform * to;

			if (localFrom == localTo)
			{
				continue;
			}

			Ray localRay(localFrom, localTo);
			if (inst.tree->IntersectRay(localRay, nullptr,
				static_cast<RaycastFlags>(raycast_flags::EarlyExit | raycast_flags::IgnoreBackface)))
			{
				return false; // blocked
			}
		}

		return true;
	}

	bool ServerCollisionMap::LineOfSightEx(const Vector3& from, const Vector3& to, Vector3& hitPoint) const
	{
		hitPoint = to;

		if (m_instances.empty())
		{
			return true;
		}

		const Ray worldRay(from, to);
		const float worldLen = worldRay.GetLength();

		float closestWorldT = 1.0f;
		bool blocked = false;

		for (const auto& inst : m_instances)
		{
			const auto [aabbHit, aabbT] = worldRay.IntersectsAABB(inst.worldBounds);
			if (!aabbHit || aabbT > worldLen * closestWorldT)
			{
				continue;
			}

			const Vector3 localFrom = inst.invTransform * from;
			const Vector3 localTo   = inst.invTransform * to;

			if (localFrom == localTo)
			{
				continue;
			}

			Ray localRay(localFrom, localTo);
			if (!inst.tree->IntersectRay(localRay, nullptr,
				static_cast<RaycastFlags>(raycast_flags::IgnoreBackface)))
			{
				continue;
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

		return !blocked;
	}
}
