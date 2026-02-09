// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/spells/spell_target_resolver.h"

#include "game_server/spells/spell_cast_context.h"
#include "game_server/world/world_instance.h"
#include "game_server/objects/game_object_s.h"
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

	bool SpellTargetResolver::ResolveEffectTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const
	{
		targets.clear();
		if (targets.capacity() < 8)
		{
			targets.reserve(8);
		}

		if (effect.targeta() == spell_effect_targets::Caster)
		{
			targets.push_back(&m_context.GetExecutor());
			return true;
		}

		if (effect.targeta() == spell_effect_targets::ObjectTarget)
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

		if (effect.targeta() == spell_effect_targets::CasterAreaParty ||
			effect.targeta() == spell_effect_targets::NearbyParty ||
			effect.targeta() == spell_effect_targets::NearbyAlly ||
			effect.targeta() == spell_effect_targets::NearbyEnemy)
		{
			const auto& position = m_context.GetExecutor().GetPosition();
			if (effect.radius() <= 0.0f)
			{
				ELOG("Spell " << m_context.GetSpell().id() << " (" << m_context.GetSpell().name() << ") effect has no radius >= 0 set");
				return false;
			}

			if (effect.targeta() == spell_effect_targets::CasterAreaParty ||
				effect.targeta() == spell_effect_targets::NearbyParty)
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

			world->GetUnitFinder().FindUnits(Circle(position.x, position.z, effect.radius()), [this, &effect, &targets](GameUnitS& unit)
			{
				if (m_context.GetSpell().maxtargets() > 0 && targets.size() >= m_context.GetSpell().maxtargets())
				{
					return true;
				}

				if (!(m_context.GetSpell().attributes(0) & spell_attributes::CanTargetDead) && !unit.IsAlive())
				{
					return true;
				}

				if (effect.targeta() == spell_effect_targets::CasterAreaParty ||
					effect.targeta() == spell_effect_targets::NearbyParty)
				{
					if (!unit.IsPlayer())
					{
						return true;
					}

					if (unit.AsPlayer().GetGroupId() == m_context.GetExecutor().AsPlayer().GetGroupId())
					{
						targets.push_back(&unit);
					}
				}
				else if (effect.targeta() == spell_effect_targets::NearbyAlly)
				{
					if (!m_context.GetExecutor().UnitIsFriendly(unit))
					{
						return true;
					}

					targets.push_back(&unit);
				}
				else if (effect.targeta() == spell_effect_targets::NearbyEnemy)
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

		if (effect.targeta() == spell_effect_targets::TargetAreaEnemy)
		{
			GameObjectS* targetObject = m_context.FindObjectByGuid(m_context.GetTarget().GetUnitTarget());
			if (!targetObject)
			{
				return false;
			}

			const auto& position = targetObject->GetPosition();
			if (effect.radius() <= 0.0f)
			{
				ELOG("Spell " << m_context.GetSpell().id() << " (" << m_context.GetSpell().name() << ") effect has no radius >= 0 set");
				return false;
			}

			auto* world = m_context.GetWorldInstance();
			if (!world)
			{
				return false;
			}

			world->GetUnitFinder().FindUnits(Circle(position.x, position.z, effect.radius()), [this, &targets](GameUnitS& unit)
			{
				if (m_context.GetSpell().maxtargets() > 0 && targets.size() >= m_context.GetSpell().maxtargets())
				{
					return true;
				}

				if (!(m_context.GetSpell().attributes(0) & spell_attributes::CanTargetDead) && !unit.IsAlive())
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

		if (effect.targeta() == spell_effect_targets::SourceAreaEnemy)
		{
			const auto& position = m_context.GetExecutor().GetPosition();
			if (effect.radius() <= 0.0f)
			{
				ELOG("Spell " << m_context.GetSpell().id() << " (" << m_context.GetSpell().name() << ") effect has no radius >= 0 set");
				return false;
			}

			auto* world = m_context.GetWorldInstance();
			if (!world)
			{
				return false;
			}

			world->GetUnitFinder().FindUnits(Circle(position.x, position.z, effect.radius()), [this, &targets](GameUnitS& unit)
			{
				if (m_context.GetSpell().maxtargets() > 0 && targets.size() >= m_context.GetSpell().maxtargets())
				{
					return true;
				}

				if (!(m_context.GetSpell().attributes(0) & spell_attributes::CanTargetDead) && !unit.IsAlive())
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

		if (effect.targeta() == spell_effect_targets::TargetAlly ||
			effect.targeta() == spell_effect_targets::TargetAny ||
			effect.targeta() == spell_effect_targets::TargetEnemy)
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

		return false;
	}
}
