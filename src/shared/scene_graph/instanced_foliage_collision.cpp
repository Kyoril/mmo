// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "instanced_foliage_collision.h"

#include "math/collision.h"
#include "math/capsule.h"
#include "math/ray.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace mmo
{
	String InstancedFoliageCollision::s_movableType = "InstancedFoliageCollision";

	InstancedFoliageCollision::InstancedFoliageCollision(const String& name, MeshPtr mesh)
		: MovableObject(name)
		, m_mesh(std::move(mesh))
	{
	}

	const String& InstancedFoliageCollision::GetMovableType() const
	{
		return s_movableType;
	}

	void InstancedFoliageCollision::AddInstance(const Matrix4& worldTransform)
	{
		Instance instance;
		instance.worldTransform = worldTransform;
		instance.invWorldTransform = worldTransform.Inverse();

		// Precompute a world-space AABB for cheap broad-phase rejection during queries.
		AABB bounds = m_mesh ? m_mesh->GetBounds() : AABB();
		bounds.Transform(worldTransform);
		instance.worldBounds = bounds;

		m_instances.emplace_back(instance);
	}

	void InstancedFoliageCollision::Finalize()
	{
		if (m_instances.empty())
		{
			m_bounds = AABB(Vector3::Zero, Vector3::Zero);
			m_boundingRadius = 0.0f;
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
	}

	bool InstancedFoliageCollision::TestCapsuleCollision(const Capsule& capsule, std::vector<CollisionResult>& results) const
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
					if (stackCount < maxStackSize - 2 && node.children < nodes.size() - 1)
					{
						stack[stackCount++] = { node.children };
						stack[stackCount++] = { node.children + 1 };
					}
				}
			}
		}

		return foundCollision;
	}

	bool InstancedFoliageCollision::TestRayCollision(const Ray& ray, CollisionResult& result) const
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
							instanceHit = true;
						}
					}
				}
				else
				{
					if (stackCount < maxStackSize - 2 && node.children < nodes.size() - 1)
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
				foundCollision = true;
			}
		}

		return foundCollision;
	}
}
