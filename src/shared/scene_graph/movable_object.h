// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once


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
	
	public:
		Scene& m_scene;
		SceneNode* m_parentNode{};
		bool m_visible{};
	};
}