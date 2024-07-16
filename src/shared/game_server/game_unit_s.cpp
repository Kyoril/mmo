// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_unit_s.h"

#include "each_tile_in_sight.h"
#include "base/utilities.h"
#include "binary_io/vector_sink.h"
#include "proto_data/project.h"

namespace mmo
{
	uint32 UnitStats::DeriveFromBaseWithFactor(const uint32 statValue, const uint32 baseValue, const uint32 factor)
	{
		// Check if just at minimum
		if (statValue <= baseValue)
		{
			return statValue;
		}

		// Init with minimum value
		uint32 result = baseValue;

		// Apply factor to difference
		result += (statValue - baseValue) * factor;

		return result;
	}

	uint32 UnitStats::GetMaxHealthFromStamina(const uint32 stamina)
	{
		return DeriveFromBaseWithFactor(stamina, 20, 10);
	}

	uint32 UnitStats::GetMaxManaFromIntellect(const uint32 intellect)
	{
		return DeriveFromBaseWithFactor(intellect, 20, 15);
	}

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
		m_attackSwingCountdown.ended.connect(this, &GameUnitS::OnAttackSwing);
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
		Set(object_fields::MaxRage, 100);
		Set(object_fields::MaxEnergy, 100);

		Set<int32>(object_fields::PowerType, power_type::Mana);

		// Base attack time of one second
		Set(object_fields::BaseAttackTime, 2000);
		Set<float>(object_fields::MinDamage, 2.0f);
		Set<float>(object_fields::MaxDamage, 4.0f);
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

	void GameUnitS::RefreshStats()
	{
		// TODO
	}

	const Vector3& GameUnitS::GetPosition() const noexcept
	{
		m_lastPosition = m_mover->GetCurrentLocation();
		return m_lastPosition;
	}

	void GameUnitS::SetLevel(uint32 newLevel)
	{
		Set(object_fields::Level, newLevel);

		RefreshStats();

		// Ensure health, mana and powers are maxed out on level up
		Set(object_fields::Health, GetMaxHealth());
		Set(object_fields::Mana, Get<uint32>(object_fields::MaxMana));
		Set(object_fields::Energy, Get<uint32>(object_fields::MaxEnergy));
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

		threatened(*instigator, damage);

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

	void GameUnitS::SpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags,
		const proto::SpellEntry& spell)
	{
		if (!m_netUnitWatcher)
		{
			return;
		}

		m_netUnitWatcher->OnSpellDamageLog(targetGuid, amount, school, flags, spell);
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

	void GameUnitS::StartAttack(const std::shared_ptr<GameUnitS>& victim)
	{
		ASSERT(victim);

		if (IsAttacking(victim))
		{
			return;
		}

		m_victim = victim;
		SetTarget(victim ? victim->GetGuid() : 0);

		const GameTime now = GetAsyncTimeMs();

		// Notify subscribers in sight
		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		game::Protocol::OutgoingPacket packet(sink);
		packet.Start(game::realm_client_packet::AttackStart);
		packet << io::write_packed_guid(GetGuid()) << io::write_packed_guid(victim->GetGuid()) << io::write<GameTime>(now);
		packet.Finish();

		// Notify all subscribers
		ForEachSubscriberInSight([&packet, &buffer](TileSubscriber& subscriber)
			{
				subscriber.SendPacket(packet, buffer);
			});

		// Trigger next attack swing
		TriggerNextAutoAttack();
	}

	void GameUnitS::StopAttack()
	{
		m_attackSwingCountdown.Cancel();
		m_victim.reset();
		SetTarget(0);

		const GameTime now = GetAsyncTimeMs();

		std::vector<char> buffer;
		io::VectorSink sink(buffer);
		game::Protocol::OutgoingPacket packet(sink);
		packet.Start(game::realm_client_packet::AttackStop);
		packet << io::write_packed_guid(GetGuid()) << io::write<GameTime>(now);
		packet.Finish();

		// Notify all subscribers
		ForEachSubscriberInSight([&packet, &buffer](TileSubscriber & subscriber)
		{
			subscriber.SendPacket(packet, buffer);
		});
	}

	void GameUnitS::SetTarget(uint64 targetGuid)
	{
		Set<uint64>(object_fields::TargetUnit, targetGuid);
	}

	void GameUnitS::SetInCombat(bool inCombat)
	{
		if (inCombat)
		{
			AddFlag<uint32>(object_fields::Flags, unit_flags::InCombat);
		}
		else
		{
			RemoveFlag<uint32>(object_fields::Flags, unit_flags::InCombat);
		}
	}

	void GameUnitS::AddAttackingUnit(const GameUnitS& attacker)
	{
		m_attackingUnits.add(&attacker);
		SetInCombat(true);
	}

	void GameUnitS::RemoveAttackingUnit(const GameUnitS& attacker)
	{
		m_attackingUnits.remove(&attacker);
		if (m_attackingUnits.empty())
		{
			SetInCombat(false);
		}
	}

	void GameUnitS::RemoveAllAttackingUnits()
	{
		m_attackingUnits.clear();
		SetInCombat(false);
	}

	uint32 GameUnitS::CalculateArmorReducedDamage(const uint32 attackerLevel, const uint32 damage) const
	{
		float armor = static_cast<float>(Get<uint32>(object_fields::Armor));
		if (armor < 0.0f)
		{
			armor = 0.0f;
		}

		float factor = armor / (armor + 400.0f + attackerLevel * 85.0f);
		factor = Clamp(factor, 0.0f, 0.75f);

		return damage - static_cast<uint32>(damage * factor);
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

		if (!IsInCombat())
		{
			RegenerateHealth();
		}
		
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

	void GameUnitS::OnAttackSwingEvent(const AttackSwingEvent attackSwingEvent) const
	{
		if (!m_netUnitWatcher)
		{
			return;
		}

		m_netUnitWatcher->OnAttackSwingEvent(attackSwingEvent);
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
		const GameTime now = GetAsyncTimeMs();
		GameTime nextAttackSwing = now;

		const GameTime mainHandCooldown = m_lastMainHand + Get<uint32>(object_fields::BaseAttackTime);
		if (mainHandCooldown > nextAttackSwing)
		{
			nextAttackSwing = mainHandCooldown;
		}

		m_attackSwingCountdown.SetEnd(nextAttackSwing);
	}

	void GameUnitS::OnAttackSwing()
	{
		// This value in milliseconds is used to retry auto attack in case of an error like out of range or wrong facing
		constexpr GameTime attackSwingErrorDelay = 200;

		// Remember that we tried to swing just now
		const GameTime now = GetAsyncTimeMs();
		m_lastMainHand = now;

		if (!IsAlive())
		{
			m_victim.reset();
			return;
		}

		auto victim = m_victim.lock();
		if (!victim)
		{
			OnAttackSwingEvent(AttackSwingEvent::CantAttack);
			return;
		}

		// Turn to target if not an attacking player
		if (GetTypeId() != ObjectTypeId::Player)
		{
			// We don't need to send this to the client as the client will display this itself
			m_movementInfo.timestamp = GetAsyncTimeMs();
			m_movementInfo.facing = GetAngle(*victim);
		}

		// Victim must be alive in order to attack
		if (!victim->IsAlive())
		{
			// We just send the error message and won't trigger another auto attack this time, as it would be pointless anyway
			OnAttackSwingEvent(AttackSwingEvent::TargetDead);
			m_victim.reset();
			return;
		}

		// Get the distance
		if (victim->GetSquaredDistanceTo(GetPosition(), false) > (GetMeleeReach() * GetMeleeReach()))
		{
			OnAttackSwingEvent(AttackSwingEvent::OutOfRange);
			m_attackSwingCountdown.SetEnd(GetAsyncTimeMs() + attackSwingErrorDelay);
			return;
		}

		// Target must be in front of us
		if (!IsFacingTowards(*victim))
		{
			OnAttackSwingEvent(AttackSwingEvent::WrongFacing);
			m_attackSwingCountdown.SetEnd(GetAsyncTimeMs() + attackSwingErrorDelay);
			return;
		}

		// Calculate damage between minimum and maximum damage
		std::uniform_real_distribution distribution(Get<float>(object_fields::MinDamage), Get<float>(object_fields::MaxDamage) + 1.0f);
		uint32 totalDamage = victim->CalculateArmorReducedDamage(Get<uint32>(object_fields::Level), static_cast<uint32>(distribution(randomGenerator)));

		// TODO: Add stuff like immunities, miss chance, dodge, parry, glancing, crushing, crit, block, absorb etc.
		const float critChance = 5.0f;			// 5% crit chance hard coded for now
		std::uniform_real_distribution critDistribution(0.0f, 100.0f);

		bool isCrit = false;
		if (critDistribution(randomGenerator) < critChance)
		{
			isCrit = true;
			totalDamage *= 2;
		}

		victim->Damage(totalDamage, spell_school::Normal, this);
		if (m_netUnitWatcher)
		{
			m_netUnitWatcher->OnNonSpellDamageLog(victim->GetGuid(), totalDamage, isCrit ? damage_flags::Crit : damage_flags::None);
		}

		// In case of success, we also want to trigger an event to potentially reset error states from previous attempts
		OnAttackSwingEvent(AttackSwingEvent::Success);
		TriggerNextAutoAttack();
	}
}
