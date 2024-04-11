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
