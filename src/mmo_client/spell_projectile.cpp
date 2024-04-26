
#include "spell_projectile.h"

#include "scene_graph/scene.h"

namespace mmo
{
	SpellProjectile::SpellProjectile(Scene& scene)
		: m_scene(scene)
	{
		m_node = m_scene.GetRootSceneNode().CreateChildSceneNode();
	}

	SpellProjectile::~SpellProjectile()
	{
		m_scene.DestroySceneNode(*m_node);
	}


}
