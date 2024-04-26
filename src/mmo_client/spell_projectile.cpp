
#include "spell_projectile.h"

#include "log/default_log_levels.h"
#include "scene_graph/scene.h"

namespace mmo
{
	SpellProjectile::SpellProjectile(Scene& scene, const proto_client::SpellEntry& spell, const Vector3& startPosition, std::weak_ptr<GameUnitC> targetUnit)
		: m_scene(scene)
		, m_targetUnit(targetUnit)
		, m_spell(spell)
	{
		m_node = m_scene.GetRootSceneNode().CreateChildSceneNode(startPosition);

		// Create sphere mesh
		static uint64 s_projectileNum = 0;
		m_sphereEntity = m_scene.CreateEntity(std::to_string(s_projectileNum++) + "_PROJECTILE", "Models/Sphere.hmsh");
		ASSERT(m_sphereEntity);

		m_node->AttachObject(*m_sphereEntity);
	}

	SpellProjectile::~SpellProjectile()
	{
		m_scene.DestroyEntity(*m_sphereEntity);
		m_scene.DestroySceneNode(*m_node);
	}

	void SpellProjectile::Update(const float deltaTime)
	{
		if (m_hit)
		{
			return;
		}

		auto strongTarget = m_targetUnit.lock();
		if (!strongTarget)
		{
			m_hit = true;
			return;
		}

		ASSERT(strongTarget->GetSceneNode());

		const Vector3& position = m_node->GetDerivedPosition();
		const Vector3& targetPosition = strongTarget->GetSceneNode()->GetDerivedPosition();

		Vector3 distance = targetPosition - position;
		if (distance.GetLength() <= 0.1f)
		{
			m_hit = true;
			return;
		}

		distance.Normalize();
		m_node->Translate(distance * m_spell.speed() * deltaTime);
	}
}
