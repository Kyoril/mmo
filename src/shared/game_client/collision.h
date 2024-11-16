#pragma once

#include <vector>

#include "math/capsule.h"

namespace mmo
{
	class Entity;
	class AABB;

	class ICollisionProvider
	{
	public:
		virtual ~ICollisionProvider() = default;

		virtual bool GetHeightAt(const Vector3& position, float range, float& out_height) = 0;

		virtual void GetCollisionTrees(const AABB& aabb, std::vector<const Entity*>& out_potentialEntities) = 0;
	};
}