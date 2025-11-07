#pragma once

#include "game_client/game_unit_c.h"
#include "base/signal.h"

#include <memory>

namespace mmo
{
	class Entity;
	class Scene;
	class SceneNode;

	namespace proto_client
	{
		class SpellEntry;
	}

	class SpellProjectile
	{
	public:
		SpellProjectile(Scene& scene, const proto_client::SpellEntry& spell, const Vector3& startPosition, std::weak_ptr<GameUnitC> targetUnit);
		~SpellProjectile();

	public:
		void Update(float deltaTime);

		bool HasHit() const { return m_hit; }

		/// \brief Access the spell entry driving this projectile.
		const proto_client::SpellEntry& GetSpell() const { return m_spell; }

		/// \brief Return a strong pointer to the target unit if still alive.
		std::shared_ptr<GameUnitC> GetTargetUnit() const { return m_targetUnit.lock(); }

	protected:
		Scene& m_scene;
		SceneNode* m_node;
		Entity* m_sphereEntity;
		std::weak_ptr<GameUnitC> m_targetUnit;
		const proto_client::SpellEntry& m_spell;
		bool m_hit { false };
	};
}
