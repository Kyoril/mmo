#pragma once

namespace mmo
{
	class Scene;
	class SceneNode;

	class SpellProjectile
	{
	public:
		SpellProjectile(Scene& scene);
		~SpellProjectile();

	public:


	protected:
		Scene& m_scene;
		SceneNode* m_node;
	};
}
