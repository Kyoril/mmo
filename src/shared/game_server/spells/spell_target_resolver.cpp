// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/spell_target_resolver.h"

#include "game_server/spells/spell_cast_context.h"
#include "game_server/world/world_instance.h"
#include "game_server/objects/game_object_s.h"
#include "game/spell.h"
#include "log/default_log_levels.h"

namespace mmo
{
	SpellTargetResolver::SpellTargetResolver(const SpellCastContext& context)
		: m_context(context)
	{
	}

	GameUnitS* SpellTargetResolver::ResolveUnitTarget() const
	{
		if (!m_context.GetTarget().HasUnitTarget())
		{
			return nullptr;
		}

		return m_context.FindUnitByGuid(m_context.GetTarget().GetUnitTarget());
	}

	std::shared_ptr<GameUnitS> SpellTargetResolver::ResolveEffectUnitTarget(const proto::SpellEffect& effect) const
	{
		switch (effect.targeta())
		{
		case spell_effect_targets::Caster:
		case spell_effect_targets::NearbyParty:
			// TODO: Nearby party
			return std::static_pointer_cast<GameUnitS>(m_context.GetExecutor().shared_from_this());
		case spell_effect_targets::TargetAlly:
		case spell_effect_targets::TargetEnemy:
		case spell_effect_targets::TargetAny:
			{
				GameObjectS* targetObject = m_context.FindObjectByGuid(m_context.GetTarget().GetUnitTarget());
				if (!targetObject)
				{
					return nullptr;
				}

				auto unit = std::dynamic_pointer_cast<GameUnitS>(targetObject->shared_from_this());
				if (unit)
				{
					switch (effect.targeta())
					{
					case spell_effect_targets::TargetAlly:
						if (!m_context.GetExecutor().UnitIsFriendly(*unit))
						{
							return nullptr;
						}
						break;
					case spell_effect_targets::TargetEnemy:
						if (m_context.GetExecutor().UnitIsFriendly(*unit))
						{
							return nullptr;
						}
						break;
					}
				}

				return unit;
			}
		default:
			return nullptr;
		}
	}

	void SpellTargetResolver::PrepareTargetsBuffer(std::vector<GameObjectS*>& targets) const
	{
		targets.clear();
		if (targets.capacity() < 8)
		{
			targets.reserve(8);
		}
	}

	bool SpellTargetResolver::ResolveSingleCasterTarget(std::vector<GameObjectS*>& targets) const
	{
		targets.push_back(&m_context.GetExecutor());
		return true;
	}

	bool SpellTargetResolver::ResolveSingleObjectTarget(std::vector<GameObjectS*>& targets) const
	{
		if (!m_context.GetTarget().HasGOTarget())
		{
			return false;
		}

		GameObjectS* target = m_context.FindObjectByGuid(m_context.GetTarget().GetGOTarget());
		if (!target)
		{
			return false;
		}

		targets.push_back(target);
		return true;
	}

	bool SpellTargetResolver::ResolveSingleUnitTarget(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const
	{
		GameUnitS* unit = m_context.FindUnitByGuid(m_context.GetTarget().GetUnitTarget());
		if (!unit)
		{
			return false;
		}

		switch (effect.targeta())
		{
		case spell_effect_targets::TargetAlly:
			if (!m_context.GetExecutor().UnitIsFriendly(*unit))
			{
				return false;
			}
			break;
		case spell_effect_targets::TargetEnemy:
			if (m_context.GetExecutor().UnitIsFriendly(*unit))
			{
				return false;
			}
			break;
		}

		targets.push_back(unit);
		return true;
	}

	bool SpellTargetResolver::ValidateEffectRadius(const proto::SpellEffect& effect) const
	{
		if (effect.radius() > 0.0f)
		{
			return true;
		}

		ELOG("Spell " << m_context.GetSpell().id() << " (" << m_context.GetSpell().name() << ") effect has no radius >= 0 set");
		return false;
	}

	bool SpellTargetResolver::CanAddUnitTarget(const std::vector<GameObjectS*>& targets, const GameUnitS& unit) const
	{
		if (m_context.GetSpell().maxtargets() > 0 && targets.size() >= m_context.GetSpell().maxtargets())
		{
			return false;
		}

		if (!(m_context.GetSpell().attributes(0) & spell_attributes::CanTargetDead) && !unit.IsAlive())
		{
			return false;
		}

		// Area-of-effect LOS check: each candidate must be visible from the caster.
		// Skipped for the caster themselves, passive spells, and spells with IgnoreLineOfSight.
		const bool isSelf     = (&unit == &m_context.GetExecutor());
		const bool isPassive  = (m_context.GetSpell().attributes(0) & spell_attributes::Passive) != 0;
		const bool ignoresLos = m_context.GetSpell().attributes_size() >= 2 &&
		                        (m_context.GetSpell().attributes(1) & spell_attributes_b::IgnoreLineOfSight) != 0;

		if (!isSelf && !isPassive && !ignoresLos)
		{
			const WorldInstance* world = m_context.GetWorldInstance();
			if (world)
			{
				MapData* mapData = world->GetMapData();
				if (mapData && !mapData->IsInLineOfSight(m_context.GetExecutor().GetPosition(), unit.GetPosition()))
				{
					return false;
				}
			}
		}

		return true;
	}

	bool SpellTargetResolver::ResolvePartyOrNearbyTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const
	{
		if (!ValidateEffectRadius(effect))
		{
			return false;
		}

		const bool isPartyTargetType =
			effect.targeta() == spell_effect_targets::CasterAreaParty ||
			effect.targeta() == spell_effect_targets::NearbyParty;

		if (isPartyTargetType)
		{
			if (!m_context.GetExecutor().IsPlayer() || m_context.GetExecutor().AsPlayer().GetGroupId() == 0)
			{
				targets.push_back(&m_context.GetExecutor());
				return true;
			}
		}

		auto* world = m_context.GetWorldInstance();
		if (!world)
		{
			return false;
		}

		const auto& position = m_context.GetExecutor().GetPosition();
		world->GetUnitFinder().FindUnits(Circle(position.x, position.z, effect.radius()), [this, &effect, &targets, isPartyTargetType](GameUnitS& unit)
		{
			if (!CanAddUnitTarget(targets, unit))
			{
				return true;
			}

			if (isPartyTargetType)
			{
				if (unit.IsPlayer() && unit.AsPlayer().GetGroupId() == m_context.GetExecutor().AsPlayer().GetGroupId())
				{
					targets.push_back(&unit);
				}
				return true;
			}

			if (effect.targeta() == spell_effect_targets::NearbyAlly)
			{
				if (!m_context.GetExecutor().UnitIsFriendly(unit))
				{
					return true;
				}

				targets.push_back(&unit);
				return true;
			}

			if (effect.targeta() == spell_effect_targets::NearbyEnemy)
			{
				if (m_context.GetExecutor().UnitIsFriendly(unit))
				{
					return true;
				}

				targets.push_back(&unit);
			}

			return true;
		});

		return true;
	}

	bool SpellTargetResolver::CollectEnemyUnitsInRadius(const proto::SpellEffect& effect, float centerX, float centerZ, std::vector<GameObjectS*>& targets) const
	{
		if (!ValidateEffectRadius(effect))
		{
			return false;
		}

		auto* world = m_context.GetWorldInstance();
		if (!world)
		{
			return false;
		}

		world->GetUnitFinder().FindUnits(Circle(centerX, centerZ, effect.radius()), [this, &targets](GameUnitS& unit)
		{
			if (!CanAddUnitTarget(targets, unit))
			{
				return true;
			}

			if (m_context.GetExecutor().UnitIsFriendly(unit))
			{
				return true;
			}

			targets.push_back(&unit);
			return true;
		});

		return true;
	}

	bool SpellTargetResolver::ResolveAreaEnemyTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const
	{
		if (effect.targeta() == spell_effect_targets::TargetAreaEnemy)
		{
			GameObjectS* targetObject = m_context.FindObjectByGuid(m_context.GetTarget().GetUnitTarget());
			if (!targetObject)
			{
				return false;
			}

			const auto& position = targetObject->GetPosition();
			return CollectEnemyUnitsInRadius(effect, position.x, position.z, targets);
		}

		if (effect.targeta() == spell_effect_targets::SourceAreaEnemy)
		{
			const auto& position = m_context.GetExecutor().GetPosition();
			return CollectEnemyUnitsInRadius(effect, position.x, position.z, targets);
		}

		return false;
	}

	bool SpellTargetResolver::ResolveEffectTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const
	{
		PrepareTargetsBuffer(targets);

		switch (effect.targeta())
		{
		case spell_effect_targets::Caster:
			return ResolveSingleCasterTarget(targets);
		case spell_effect_targets::ObjectTarget:
			return ResolveSingleObjectTarget(targets);
		case spell_effect_targets::TargetAlly:
		case spell_effect_targets::TargetAny:
		case spell_effect_targets::TargetEnemy:
			return ResolveSingleUnitTarget(effect, targets);
		case spell_effect_targets::CasterAreaParty:
		case spell_effect_targets::NearbyParty:
		case spell_effect_targets::NearbyAlly:
		case spell_effect_targets::NearbyEnemy:
			return ResolvePartyOrNearbyTargets(effect, targets);
		case spell_effect_targets::TargetAreaEnemy:
		case spell_effect_targets::SourceAreaEnemy:
			return ResolveAreaEnemyTargets(effect, targets);
		default:
			return false;
		}
	}
}
