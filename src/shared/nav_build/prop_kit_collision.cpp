// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Kept out of map.cpp so nav_build_tests can compile it directly: it needs only math and the
// header-only prop kit types, not the scene graph or asset libraries map.cpp links against.

#include "map.h"

#include "math/aabb_tree.h"
#include "math/matrix4.h"
#include "scene_graph/prop_kit.h"

namespace mmo
{
	void AppendPropKitTriangles(const PropKit& kit, const Matrix4& placementTransform,
		const std::function<const AABBTree*(const String& meshPath)>& collisionOf,
		std::vector<Vector3>& vertices, std::vector<int32>& indices)
	{
		for (const WorldModelMeshRef& prop : kit.props)
		{
			if (!prop.visible || prop.meshPath.empty())
			{
				continue;
			}

			const AABBTree* collisionTree = collisionOf(prop.meshPath);
			if (!collisionTree || collisionTree->IsEmpty())
			{
				continue;
			}

			Matrix4 propTransform;
			propTransform.MakeTransform(prop.position, prop.scale, prop.rotation);
			const Matrix4 transform = placementTransform * propTransform;

			const int32 baseIndex = static_cast<int32>(vertices.size());
			for (const Vector3& vertex : collisionTree->GetVertices())
			{
				vertices.push_back(transform * vertex);
			}

			for (const AABBTree::Index index : collisionTree->GetIndices())
			{
				indices.push_back(baseIndex + static_cast<int32>(index));
			}
		}
	}
}
