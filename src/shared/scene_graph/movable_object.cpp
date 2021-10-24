// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "movable_object.h"
#include "scene_node.h"


namespace mmo
{
	MovableObject::MovableObject(Scene& scene)
		: m_scene(scene)
	{
	}

	const Matrix4& MovableObject::GetParentNodeFullTransform()
	{
		if (m_parentNode)
		{
			return m_parentNode->GetFullTransform();
		}

		return Matrix4::Identity;
	}

	const AABB& MovableObject::GetWorldBounds(bool derive)
	{
		if (derive)
		{
			m_worldBounds = GetBounds();
			m_worldBounds.Transform(GetParentNodeFullTransform());
		}

		return m_worldBounds;
	}
}
