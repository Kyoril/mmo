// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "prop_kit_checks.h"

#include "portal.h"

#include "math/matrix4.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		/// Transforms all eight corners of the box by the prop's TRS and returns the enclosing AABB.
		AABB transformBounds(const AABB& local, const WorldModelMeshRef& prop)
		{
			Matrix4 matrix;
			matrix.MakeTransform(prop.position, prop.scale, prop.rotation);

			AABB result;
			bool first = true;
			for (int i = 0; i < 8; ++i)
			{
				const Vector3 corner(
					(i & 1) ? local.max.x : local.min.x,
					(i & 2) ? local.max.y : local.min.y,
					(i & 4) ? local.max.z : local.min.z);
				const Vector3 world = matrix.TransformAffine(corner);

				if (first)
				{
					result.min = result.max = world;
					first = false;
				}
				else
				{
					result.min = Vector3(std::min(result.min.x, world.x), std::min(result.min.y, world.y), std::min(result.min.z, world.z));
					result.max = Vector3(std::max(result.max.x, world.x), std::max(result.max.y, world.y), std::max(result.max.z, world.z));
				}
			}

			return result;
		}

		/// Gap between two boxes, 0 if they overlap.
		float boxGap(const AABB& a, const AABB& b)
		{
			const float dx = std::max({ 0.0f, a.min.x - b.max.x, b.min.x - a.max.x });
			const float dy = std::max({ 0.0f, a.min.y - b.max.y, b.min.y - a.max.y });
			const float dz = std::max({ 0.0f, a.min.z - b.max.z, b.min.z - a.max.z });
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}
	}

	std::vector<PortalClearanceIssue> FindPropsNearPortals(const WorldModel& model, const PropKit& kit,
		const std::function<std::optional<AABB>(const String& meshPath)>& collisionBoundsOf, const float margin)
	{
		std::vector<PortalClearanceIssue> issues;

		// Portal bounds in model space
		std::vector<std::optional<AABB>> portalBounds;
		for (const auto& portal : model.GetPortals())
		{
			std::optional<AABB> bounds;
			if (portal)
			{
				const auto& vertices = portal->GetWorldVertices();
				if (!vertices.empty())
				{
					AABB box;
					box.min = box.max = vertices.front();
					for (const Vector3& vertex : vertices)
					{
						box.min = Vector3(std::min(box.min.x, vertex.x), std::min(box.min.y, vertex.y), std::min(box.min.z, vertex.z));
						box.max = Vector3(std::max(box.max.x, vertex.x), std::max(box.max.y, vertex.y), std::max(box.max.z, vertex.z));
					}
					bounds = box;
				}
			}
			portalBounds.push_back(bounds);
		}

		for (const WorldModelMeshRef& prop : kit.props)
		{
			const std::optional<AABB> local = collisionBoundsOf(prop.meshPath);
			if (!local)
			{
				continue;
			}

			const AABB propBounds = transformBounds(*local, prop);
			for (size_t i = 0; i < portalBounds.size(); ++i)
			{
				if (!portalBounds[i])
				{
					continue;
				}

				const float gap = boxGap(propBounds, *portalBounds[i]);
				if (gap <= margin)
				{
					issues.push_back(PortalClearanceIssue{ prop.name.empty() ? prop.meshPath : prop.name, static_cast<int32>(i), gap });
				}
			}
		}

		return issues;
	}
}
