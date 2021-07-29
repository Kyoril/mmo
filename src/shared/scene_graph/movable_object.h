// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "math/aabb.h"

namespace mmo
{
	class SceneNode;
	class Scene;

	/// Base class of an object in a scene which is movable, so it has a node which it is attached to.
	class MovableObject
	{
	public:
		explicit MovableObject(Scene& scene);
		virtual ~MovableObject() = default;

	public:
		Scene& GetScene() const { return m_scene; }
	
		const Matrix4& GetParentNodeFullTransform();
		const AABB& GetWorldBounds(bool derive);

		virtual const AABB& GetBounds() const = 0;

	public:
		Scene& m_scene;
		SceneNode* m_parentNode{};
		bool m_visible{};
		AABB m_worldBounds;
	};
}