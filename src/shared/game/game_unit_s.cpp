// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_unit_s.h"

#include "base/utilities.h"
#include "proto_data/project.h"

namespace mmo
{
	GameUnitS::GameUnitS(const proto::Project& project, TimerQueue& timers)
		: GameObjectS(project)
		, m_timers(timers)
		, m_despawnCountdown(timers)
		, m_attackSwingCountdown(timers)
		, m_regenCountdown(timers)
	{
		// Setup unit mover
		m_mover = make_unique<UnitMover>(*this);

		// Create spell caster
		m_spellCast = std::make_unique<SpellCast>(m_timers, *this);

		m_regenCountdown.ended.connect(this, &GameUnitS::OnRegeneration);
		m_despawnCountdown.ended.connect(this, &GameUnitS::OnDespawnTimer);
	}

	void GameUnitS::Initialize()
	{
		GameObjectS::Initialize();

		// Initialize some values
		Set(object_fields::Type, ObjectTypeId::Unit);
		Set(object_fields::Scale, 1.0f);

		// Set unit values
		Set(object_fields::Health, 60u);
		Set(object_fields::MaxHealth, 60u);

		Set(object_fields::Mana, 100);
		Set(object_fields::Rage, 0);	
		Set(object_fields::Energy, 100);

		Set(object_fields::MaxMana, 100);
		Set(object_fields::MaxRage, 1000);
		Set(object_fields::MaxEnergy, 100);
	}

	void GameUnitS::TriggerDespawnTimer(GameTime despawnDelay)
	{
		m_despawnCountdown.SetEnd(GetAsyncTimeMs() + despawnDelay);
	}

	void GameUnitS::WriteObjectUpdateBlock(io::Writer& writer, bool creation) const
	{
		GameObjectS::WriteObjectUpdateBlock(writer, creation);
	}

	void GameUnitS::WriteValueUpdateBlock(io::Writer& writer, bool creation) const
	{
		GameObjectS::WriteValueUpdateBlock(writer, creation);
	}

	void GameUnitS::SetLevel(uint32 newLevel)
	{
		Set(object_fields::Level, newLevel);
	}

	auto GameUnitS::SpellHasCooldown(const uint32 spellId, uint32 spellCategory) const -> bool
	{
		const auto now = GetAsyncTimeMs();

		if (const auto it = m_spellCooldowns.find(spellId); it != m_spellCooldowns.end() && it->second > now)
		{
			return true;
		}

		if (const auto it2 = m_spellCategoryCooldowns.find(spellCategory); it2 != m_spellCategoryCooldowns.end() && it2->second > now)
		{
			return true;
		}

		return false;
	}

	bool GameUnitS::HasSpell(uint32 spellId) const
	{
		return std::find_if(m_spells.begin(), m_spells.end(), [spellId](const auto& spell) { return spell->id() == spellId; }) != m_spells.end();
	}

	void GameUnitS::SetInitialSpells(const std::vector<uint32>& spellIds)
	{
		ASSERT(m_spells.empty());
		m_spells.clear();

		for (const auto& spellId : spellIds)
		{
			const auto* spell = m_project.spells.getById(spellId);
			if (!spell)
			{
				WLOG("Unknown spell " << spellId << " in list of initial spells for unit " << log_hex_digit(GetGuid()));
				continue;
			}

			m_spells.insert(spell);
		}
	}

	void GameUnitS::AddSpell(const uint32 spellId)
	{
		const auto* spell = m_project.spells.getById(spellId);
		if (!spell)
		{
			WLOG("Unable to add unknown spell " << spellId << " to unit " << log_hex_digit(GetGuid()));
			return;
		}

		if (m_spells.contains(spell))
		{
			WLOG("Spell " << spellId << " is already known by unit " << log_hex_digit(GetGuid()));
			return;
		}

		m_spells.insert(spell);
		OnSpellLearned(*spell);
	}

	void GameUnitS::RemoveSpell(const uint32 spellId)
	{
		const auto* spell = m_project.spells.getById(spellId);
		if (!spell)
		{
			WLOG("Unable to remove unknown spell " << spellId << " from unit " << log_hex_digit(GetGuid()));
			return;
		}

		if (m_spells.erase(spell) < 1)
		{
			WLOG("Unable to remove spell " << spellId << " from unit " << log_hex_digit(GetGuid()) << ": spell was not known");
		}
		else
		{
			OnSpellUnlearned(*spell);
		}
	}

	void GameUnitS::SetCooldown(uint32 spellId, GameTime cooldownTimeMs)
	{
		if (cooldownTimeMs == 0)
		{
			m_spellCooldowns.erase(spellId);
		}
		else
		{
			m_spellCooldowns[spellId] = GetAsyncTimeMs() + cooldownTimeMs;
		}
	}

	void GameUnitS::SetSpellCategoryCooldown(uint32 spellCategory, GameTime cooldownTimeMs)
	{
		if (cooldownTimeMs == 0)
		{
			m_spellCategoryCooldowns.erase(spellCategory);
		}
		else
		{
			m_spellCategoryCooldowns[spellCategory] = GetAsyncTimeMs() + cooldownTimeMs;
		}
	}

	SpellCastResult GameUnitS::CastSpell(const SpellTargetMap& target, const proto::SpellEntry& spell, const uint32 castTimeMs)
	{
		if (!HasSpell(spell.id()))
		{
			WLOG("Player does not know spell " << spell.id());
			return spell_cast_result::FailedNotKnown;
		}

		const auto result = m_spellCast->StartCast(spell, target, castTimeMs);
		if (result.first == spell_cast_result::CastOkay)
		{
			startedCasting(spell);
		}

		// Reset auto attack timer if requested
		if (result.first == spell_cast_result::CastOkay &&
			m_attackSwingCountdown.IsRunning() &&
			result.second)
		{
			// Register for casts ended-event
			if (castTimeMs > 0)
			{
				// Pause auto attack during spell cast
				m_attackSwingCountdown.Cancel();
				result.second->ended.connect(this, &GameUnitS::OnSpellCastEnded);
			}
			else
			{
				// Cast already finished since it was an instant cast
				OnSpellCastEnded(true);
			}
		}

		return result.first;
	}

	void GameUnitS::Damage(const uint32 damage, uint32 school, GameUnitS* instigator)
	{
		uint32 health = Get<uint32>(object_fields::Health);
		if (health < 1)
		{
			return;
		}

		threatened(*instigator, 1.0f);

		if (health < damage)
		{
			health = 0;
		}
		else
		{
			health -= damage;
		}

		Set<uint32>(object_fields::Health, health);
		takenDamage(instigator, damage);

		// Kill event
		if (health < 1)
		{
			OnKilled(instigator);
		}
	}

	void GameUnitS::Kill(GameUnitS* killer)
	{
		Set<uint32>(object_fields::Health, 0);
		OnKilled(killer);
	}

	void GameUnitS::StartRegeneration()
	{
		if (m_regenCountdown.IsRunning())
		{
			return;
		}

		m_regenCountdown.SetEnd(GetAsyncTimeMs() + (constants::OneSecond * 2));
	}

	void GameUnitS::StopRegeneration()
	{
		m_regenCountdown.Cancel();
	}

	void GameUnitS::OnKilled(GameUnitS* killer)
	{
		m_spellCast->StopCast();

		Set<uint64>(object_fields::TargetUnit, 0);

		killed(killer);
	}

	void GameUnitS::OnSpellCastEnded(bool succeeded)
	{
		// TODO
		/*if (m_victim)
		{
			m_lastMainHand = m_lastOffHand = GetAsyncTimeMs();

			if (!m_attackSwingCountdown.IsRunning())
			{
				TriggerNextAutoAttack();
			}
		}*/
	}

	void GameUnitS::OnRegeneration()
	{
		if (!IsAlive())
		{
			return;
		}

		RegenerateHealth();

		if (!IsInCombat())
		{
			RegeneratePower(power_type::Rage);
		}

		RegeneratePower(power_type::Energy);
		RegeneratePower(power_type::Mana);

		StartRegeneration();
	}

	void GameUnitS::RegenerateHealth()
	{
		if (!IsAlive())
		{
			return;
		}

		// TODO: Do proper health regen formula

		const uint32 maxHealth = GetMaxHealth();
		uint32 health = GetHealth();

		health += 9;
		if (health > maxHealth) health = maxHealth;

		Set<uint32>(object_fields::Health, health);
	}

	void GameUnitS::RegeneratePower(PowerType powerType)
	{
		if (!IsAlive())
		{
			return;
		}

		ASSERT(static_cast<uint8>(powerType) < static_cast<uint8>(power_type::Count_));

		// TODO: Do proper power regen formula

		// Get power and max power
		int32 power = Get<int32>(object_fields::Mana + static_cast<uint8>(powerType));
		const int32 maxPower = Get<uint32>(object_fields::MaxMana + static_cast<uint8>(powerType));

		switch(powerType)
		{
		case power_type::Rage:
			power -= 6;
			if (power < 0) power = 0;
			break;
		case power_type::Energy:
			power += 20;
			if (power > maxPower) power = maxPower;
			break;
		case power_type::Mana:
			power += 9;
			if (power > maxPower) power = maxPower;
			break;
		}

		Set<int32>(object_fields::Mana + static_cast<uint8>(powerType), power);
	}

	void GameUnitS::OnDespawnTimer()
	{
		if (m_worldInstance)
		{
			m_worldInstance->RemoveGameObject(*this);
		}
	}

	void GameUnitS::TriggerNextAutoAttack()
	{
		// TODO
	}
}
