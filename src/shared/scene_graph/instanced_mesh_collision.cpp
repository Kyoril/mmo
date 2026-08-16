// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "instanced_mesh_collision.h"

#include "log/default_log_levels.h"
#include "math/collision.h"
#include "math/capsule.h"
#include "math/ray.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace mmo
{
	String InstancedMeshCollision::s_movableType = "InstancedMeshCollision";

	InstancedMeshCollision::InstancedMeshCollision(const String& name, MeshPtr mesh)
		: MovableObject(name)
		, m_mesh(std::move(mesh))
	{
	}

	const String& InstancedMeshCollision::GetMovableType() const
	{
		return s_movableType;
	}

	bool InstancedMeshCollision::AddInstance(const Matrix4& worldTransform)
	{
		// A zero component in the authored scale (.hfol foliage, .hwmo mesh refs) makes this transform
		// singular, and neither
		// inverse routine has a singularity check — the result comes out full of inf/NaN. Storing
		// that is worse than dropping the instance: every ray transformed into the instance's local
		// space becomes NaN, and because NaN compares false against everything (including itself)
		// the instance quietly stops colliding instead of announcing the bad data. Dropping it is
		// the honest failure, and it matches what the world server does with the same content.
		//
		// The criterion is finiteness of the inverse, not the magnitude of the scale: a legitimately
		// shrunken prop has a tiny determinant but a perfectly usable inverse, and must still
		// collide. That leaves extreme non-uniform scales (one axis near zero while the others are
		// astronomically large) as the one degenerate shape this accepts — no authoring path can
		// produce them.
		if (!worldTransform.IsAffine() || !worldTransform.IsFinite())
		{
			WarnRejectedTransform("it is not affine or not finite");
			return false;
		}

		// InverseAffine rather than Inverse: for an affine transform the two agree, but Inverse
		// asserts on a zero determinant before it returns, so on the non-Windows client the assert
		// would fire before the check below ever ran. Worse, that assert compares the determinant
		// against zero with a FLT_EPSILON tolerance, which a valid 1/1000th-scale prop trips.
		const Matrix4 invWorldTransform = worldTransform.InverseAffine();
		if (!invWorldTransform.IsFinite())
		{
			WarnRejectedTransform("it is not invertible (is a scale component zero?)");
			return false;
		}

		Instance instance;
		instance.worldTransform = worldTransform;
		instance.invWorldTransform = invWorldTransform;

		// Precompute a world-space AABB for cheap broad-phase rejection during queries.
		AABB bounds = m_mesh ? m_mesh->GetBounds() : AABB();
		bounds.Transform(worldTransform);
		instance.worldBounds = bounds;

		m_instances.emplace_back(instance);
		return true;
	}

	void InstancedMeshCollision::WarnRejectedTransform(const char* reason)
	{
		// One proxy can hold hundreds of instances of a single mesh, and bad authored data tends to
		// affect all of them at once, so warn once per proxy. The object name carries the mesh name
		// plus where the batch came from (cell coordinates or room), which is what an artist needs
		// to find the offending placement.
		if (m_warnedRejectedTransform)
		{
			return;
		}

		m_warnedRejectedTransform = true;

		WLOG("InstancedMeshCollision: ignoring instances of '" << GetName() << "' because "
			<< reason << " - they would corrupt collision queries instead of blocking them");
	}

	void InstancedMeshCollision::ClearInstances()
	{
		m_instances.clear();
		m_bounds = AABB(Vector3::Zero, Vector3::Zero);
		m_boundingRadius = 0.0f;

		// See Finalize() - the cached world bounding box is derived from these bounds.
		NotifyMoved();
	}

	void InstancedMeshCollision::Finalize()
	{
		if (m_instances.empty())
		{
			m_bounds = AABB(Vector3::Zero, Vector3::Zero);
			m_boundingRadius = 0.0f;
			NotifyMoved();
			return;
		}

		Vector3 minBounds(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		Vector3 maxBounds(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

		for (const auto& instance : m_instances)
		{
			minBounds.x = std::min(minBounds.x, instance.worldBounds.min.x);
			minBounds.y = std::min(minBounds.y, instance.worldBounds.min.y);
			minBounds.z = std::min(minBounds.z, instance.worldBounds.min.z);
			maxBounds.x = std::max(maxBounds.x, instance.worldBounds.max.x);
			maxBounds.y = std::max(maxBounds.y, instance.worldBounds.max.y);
			maxBounds.z = std::max(maxBounds.z, instance.worldBounds.max.z);
		}

		m_bounds = AABB(minBounds, maxBounds);
		m_boundingRadius = (maxBounds - minBounds).GetLength() * 0.5f;

		// MovableObject caches the world bounding box that AABB/ray scene queries filter on, and only
		// recomputes it when the object reports having moved. A world model proxy is attached to its
		// node while still empty and only filled on the first frame, by which time the scene graph
		// update has already derived - and cached - a degenerate box at the origin. Without this the
		// proxy keeps that box forever, so every collision query rejects it in the broad phase and the
		// batched dungeon geometry is walked straight through. Mirrors WorldModelBatch::UploadInstances.
		NotifyMoved();
	}

	bool InstancedMeshCollision::TestCapsuleCollision(const Capsule& capsule, std::vector<CollisionResult>& results) const
	{
		if (!m_mesh)
		{
			return false;
		}

		const auto& collisionTree = m_mesh->GetCollisionTree();
		if (collisionTree.IsEmpty())
		{
			return false;
		}

		const AABB capsuleBounds = capsule.GetBounds();

		const auto& nodes = collisionTree.GetNodes();
		const auto& vertices = collisionTree.GetVertices();
		const auto& indices = collisionTree.GetIndices();

		bool foundCollision = false;

		for (const auto& instance : m_instances)
		{
			// Broad-phase: most instances in a cell are far from the capsule - reject them cheaply.
			if (!capsuleBounds.Intersects(instance.worldBounds))
			{
				continue;
			}

			const Matrix4& worldTransform = instance.worldTransform;

			// Narrow-phase: traverse this instance's collision tree, transforming geometry to world
			// space (handles non-uniform scale correctly) and testing the capsule per triangle.
			struct StackEntry { uint32 nodeIndex; };
			constexpr int maxStackSize = 64;
			StackEntry stack[maxStackSize];
			int stackCount = 1;
			stack[0] = { 0 };

			while (stackCount > 0 && stackCount < maxStackSize)
			{
				const StackEntry current = stack[--stackCount];
				if (current.nodeIndex >= nodes.size())
				{
					continue;
				}

				const auto& node = nodes[current.nodeIndex];

				// Transform the node's local AABB into world space (all 8 corners for non-uniform scale).
				const Vector3& localMin = node.bounds.min;
				const Vector3& localMax = node.bounds.max;
				const Vector3 corners[8] = {
					worldTransform * Vector3(localMin.x, localMin.y, localMin.z),
					worldTransform * Vector3(localMax.x, localMin.y, localMin.z),
					worldTransform * Vector3(localMin.x, localMax.y, localMin.z),
					worldTransform * Vector3(localMax.x, localMax.y, localMin.z),
					worldTransform * Vector3(localMin.x, localMin.y, localMax.z),
					worldTransform * Vector3(localMax.x, localMin.y, localMax.z),
					worldTransform * Vector3(localMin.x, localMax.y, localMax.z),
					worldTransform * Vector3(localMax.x, localMax.y, localMax.z)
				};

				AABB worldBounds;
				worldBounds.min = corners[0];
				worldBounds.max = corners[0];
				for (int i = 1; i < 8; ++i)
				{
					worldBounds.min.x = std::min(worldBounds.min.x, corners[i].x);
					worldBounds.min.y = std::min(worldBounds.min.y, corners[i].y);
					worldBounds.min.z = std::min(worldBounds.min.z, corners[i].z);
					worldBounds.max.x = std::max(worldBounds.max.x, corners[i].x);
					worldBounds.max.y = std::max(worldBounds.max.y, corners[i].y);
					worldBounds.max.z = std::max(worldBounds.max.z, corners[i].z);
				}

				if (!capsuleBounds.Intersects(worldBounds))
				{
					continue;
				}

				if (node.numFaces > 0)
				{
					for (uint32 i = 0; i < node.numFaces; ++i)
					{
						const uint32 faceIndex = node.startFace + i;
						if (faceIndex * 3 + 2 >= indices.size())
						{
							continue;
						}

						const uint32 i0 = indices[faceIndex * 3 + 0];
						const uint32 i1 = indices[faceIndex * 3 + 1];
						const uint32 i2 = indices[faceIndex * 3 + 2];
						if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
						{
							continue;
						}

						const Vector3 worldV0 = worldTransform * Vector3(vertices[i0].x, vertices[i0].y, vertices[i0].z);
						const Vector3 worldV1 = worldTransform * Vector3(vertices[i1].x, vertices[i1].y, vertices[i1].z);
						const Vector3 worldV2 = worldTransform * Vector3(vertices[i2].x, vertices[i2].y, vertices[i2].z);

						Vector3 contactPoint, contactNormal;
						float penetrationDepth;
						float distance;
						if (CapsuleTriangleIntersection(capsule, worldV0, worldV1, worldV2, contactPoint, contactNormal, penetrationDepth, distance))
						{
							results.emplace_back(true, contactPoint, contactNormal, worldV0, worldV1, worldV2, penetrationDepth, distance);
							foundCollision = true;
						}
					}
				}
				else
				{
					if (stackCount < maxStackSize - 2 && node.children > current.nodeIndex && node.children + 1 < nodes.size())
					{
						stack[stackCount++] = { node.children };
						stack[stackCount++] = { node.children + 1 };
					}
				}
			}
		}

		return foundCollision;
	}

	bool InstancedMeshCollision::TestRayCollision(const Ray& ray, CollisionResult& result) const
	{
		if (!m_mesh)
		{
			return false;
		}

		const auto& collisionTree = m_mesh->GetCollisionTree();
		if (collisionTree.IsEmpty())
		{
			return false;
		}

		const auto& nodes = collisionTree.GetNodes();
		const auto& vertices = collisionTree.GetVertices();
		const auto& indices = collisionTree.GetIndices();

		bool foundCollision = false;
		float bestWorldDistanceSq = std::numeric_limits<float>::max();

		const Vector3 rayDir = ray.GetDirection();

		for (const auto& instance : m_instances)
		{
			// Broad-phase: skip instances the ray does not even enter.
			const auto [enters, enterDist] = ray.IntersectsAABB(instance.worldBounds);
			if (!enters)
			{
				continue;
			}

			const Matrix4& worldTransform = instance.worldTransform;
			const Matrix4& invWorldTransform = instance.invWorldTransform;

			// Transform the ray into the instance's local space.
			const Vector3 localOrigin = invWorldTransform * ray.origin;
			Vector3 localDirection = invWorldTransform * (ray.origin + rayDir) - localOrigin;
			localDirection.Normalize();
			const Ray localRay(localOrigin, localOrigin + localDirection);

			struct StackEntry { uint32 nodeIndex; };
			constexpr int maxStackSize = 64;
			StackEntry stack[maxStackSize];
			int stackCount = 1;
			stack[0] = { 0 };

			float closestLocalDistance = std::numeric_limits<float>::max();
			Vector3 localHitPoint;
			Vector3 localHitNormal;
			// Tracked so the hit can be resolved back to a submesh, and from there to a surface
			// type, exactly like Entity::TestRayCollision does.
			int32 closestFaceIndex = -1;
			bool instanceHit = false;

			while (stackCount > 0 && stackCount < maxStackSize)
			{
				const StackEntry current = stack[--stackCount];
				if (current.nodeIndex >= nodes.size())
				{
					continue;
				}

				const auto& node = nodes[current.nodeIndex];

				auto [intersects, distance] = localRay.IntersectsAABB(node.bounds);
				if (!intersects || distance > closestLocalDistance)
				{
					continue;
				}

				if (node.numFaces > 0)
				{
					for (uint32 i = 0; i < node.numFaces; ++i)
					{
						const uint32 faceIndex = node.startFace + i;
						if (faceIndex * 3 + 2 >= indices.size())
						{
							continue;
						}

						const uint32 i0 = indices[faceIndex * 3 + 0];
						const uint32 i1 = indices[faceIndex * 3 + 1];
						const uint32 i2 = indices[faceIndex * 3 + 2];
						if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
						{
							continue;
						}

						const auto v0 = Vector3(vertices[i0].x, vertices[i0].y, vertices[i0].z);
						const auto v1 = Vector3(vertices[i1].x, vertices[i1].y, vertices[i1].z);
						const auto v2 = Vector3(vertices[i2].x, vertices[i2].y, vertices[i2].z);

						auto [hitTriangle, hitDistance] = localRay.IntersectsTriangle(v0, v1, v2);
						if (hitTriangle && hitDistance < closestLocalDistance)
						{
							closestLocalDistance = hitDistance;
							localHitPoint = localRay.origin + localRay.GetDirection() * hitDistance;

							Vector3 normal = (v1 - v0).Cross(v2 - v0);
							normal.Normalize();
							localHitNormal = normal;
							closestFaceIndex = static_cast<int32>(faceIndex);
							instanceHit = true;
						}
					}
				}
				else
				{
					if (stackCount < maxStackSize - 2 && node.children > current.nodeIndex && node.children + 1 < nodes.size())
					{
						stack[stackCount++] = { node.children };
						stack[stackCount++] = { node.children + 1 };
					}
				}
			}

			if (!instanceHit)
			{
				continue;
			}

			// Convert the local hit to world space and keep the nearest hit across all instances.
			const Vector3 worldHitPoint = worldTransform * localHitPoint;
			const float worldDistanceSq = (worldHitPoint - ray.origin).GetSquaredLength();
			if (worldDistanceSq < bestWorldDistanceSq)
			{
				bestWorldDistanceSq = worldDistanceSq;

				Vector3 worldNormal = worldTransform * localHitNormal - worldTransform * Vector3::Zero;
				worldNormal.Normalize();

				result.hasCollision = true;
				result.contactPoint = worldHitPoint;
				result.contactNormal = worldNormal;
				result.penetrationDepth = std::sqrt(worldDistanceSq);
				result.distance = result.penetrationDepth;
				result.faceIndex = closestFaceIndex;
				foundCollision = true;
			}
		}

		return foundCollision;
	}

	uint32 InstancedMeshCollision::GetSurfaceTypeAt(const CollisionResult& hit) const
	{
		if (!m_mesh)
		{
			return 0;
		}

		// A material override replaces the material of every submesh (that is what
		// Entity::SetMaterial does), so the hit face does not need resolving at all.
		if (m_materialOverride)
		{
			return m_materialOverride->GetSurfaceTypeId();
		}

		// Map the hit face to its source submesh. Legacy collision trees have no mapping;
		// fall back to the first submesh so single-material meshes still resolve correctly.
		uint16 subMeshIndex = 0;
		const auto& faceSubMeshes = m_mesh->GetCollisionTree().GetFaceSubMeshes();
		if (hit.faceIndex >= 0 && static_cast<size_t>(hit.faceIndex) < faceSubMeshes.size())
		{
			subMeshIndex = faceSubMeshes[hit.faceIndex];
		}

		if (subMeshIndex >= m_mesh->GetSubMeshCount())
		{
			subMeshIndex = 0;
		}

		if (m_mesh->GetSubMeshCount() == 0)
		{
			return 0;
		}

		const MaterialPtr& material = m_mesh->GetSubMesh(subMeshIndex).GetMaterial();
		return material ? material->GetSurfaceTypeId() : 0;
	}
}
