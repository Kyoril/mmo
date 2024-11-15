#pragma once

#include "math/capsule.h"

namespace mmo
{
	class ICollisionProvider
	{
	public:
		virtual ~ICollisionProvider() = default;

		virtual bool GetHeightAt(const Vector3& position, float range, float& out_height) = 0;
	};
}