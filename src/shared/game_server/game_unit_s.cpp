// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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

	PendingMovementChange::PendingMovementChange()
	{
		counter = 0;
		changeType = MovementChangeType::Invalid;
		speed = 0.0f;
		timestamp = 0;
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

	GameUnitS::~GameUnitS()
	{
		// First misapply all auras before deleting them
		for (auto& aura : m_auras)
		{
			aura->SetApplied(false);
		}

		m_auras.clear();
	}

	void GameUnitS::Initialize()
	{
		GameObjectS::Initialize();

		// Initialize unit mods
		for (size_t i = 0; i < unit_mods::End; ++i)
		{
			m_unitMods[i][unit_mod_type::BaseValue] = 0.0f;
			m_unitMods[i][unit_mod_type::TotalValue] = 0.0f;
			m_unitMods[i][unit_mod_type::BasePct] = 1.0f;
			m_unitMods[i][unit_mod_type::TotalPct] = 1.0f;
		}

		m_speedBonus.fill(1.0f);

		// Initialize some values
		Set(object_fields::Type, ObjectTypeId::Unit);
		Set(object_fields::Scale, 1.0f);

		Set(object_fields::Entry, -1);

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
		Set<float>(object_fields::MaxDamage, 2.0f);
	}

	void GameUnitS::TriggerDespawnTimer(GameTime despawnDelay)
	{
		m_despawnCountdown.SetEnd(GetAsyncTimeMs() + despawnDelay);
	}

	void GameUnitS::WriteObjectUpdateBlock(io::Writer& writer, bool creation) const
	{
		GameObjectS::WriteObjectUpdateBlock(writer, creation);

		// Speeds
		writer
			<< io::write<float>(GetSpeed(movement_type::Walk))
			<< io::write<float>(GetSpeed(movement_type::Run))
			<< io::write<float>(GetSpeed(movement_type::Backwards))
			<< io::write<float>(GetSpeed(movement_type::Swim))
			<< io::write<float>(GetSpeed(movement_type::SwimBackwards))
			<< io::write<float>(GetSpeed(movement_type::Flight))
			<< io::write<float>(GetSpeed(movement_type::FlightBackwards))
			<< io::write<float>(GetSpeed(movement_type::Turn));
	}

	void GameUnitS::WriteValueUpdateBlock(io::Writer& writer, bool creation) const
	{
		GameObjectS::WriteValueUpdateBlock(writer, creation);
	}

	void GameUnitS::RefreshStats()
	{
	}

	const Vector3& GameUnitS::GetPosition() const noexcept
	{
		m_lastPosition = m_mover->GetCurrentLocation();
		return m_lastPosition;
	}

	float GameUnitS::GetModifierValue(UnitMods mod, UnitModType type) const
	{
		return m_unitMods[mod][type];
	}

	void GameUnitS::SetModifierValue(UnitMods mod, UnitModType type, float value)
	{
		m_unitMods[mod][type] = value;
	}

	void GameUnitS::UpdateModifierValue(UnitMods mod, UnitModType type, float amount, bool apply)
	{
		if (mod >= unit_mods::End || type >= unit_mod_type::End)
		{
			return;
		}

		switch (type)
		{
		case unit_mod_type::BaseValue:
		case unit_mod_type::TotalValue:
		{
			m_unitMods[mod][type] += (apply ? amount : -amount);
			break;
		}

		case unit_mod_type::BasePct:
		case unit_mod_type::TotalPct:
		{
			if (amount == -100.0f) {
				amount = -99.99f;
			}
			m_unitMods[mod][type] *= (apply ? (100.0f + amount) / 100.0f : 100.0f / (100.0f + amount));
			break;
		}

		default:
			break;
		}

		RefreshStats();
	}

	PendingMovementChange GameUnitS::PopPendingMovementChange()
	{
		ASSERT(!m_pendingMoveChanges.empty());

		PendingMovementChange change = m_pendingMoveChanges.front();
		m_pendingMoveChanges.pop_front();

		return change;
	}

	void GameUnitS::PushPendingMovementChange(PendingMovementChange change)
	{
		m_pendingMoveChanges.emplace_back(change);
	}

	bool GameUnitS::HasTimedOutPendingMovementChange() const
	{
		/// A flat timeout tolerance value in milliseconds. If an expected client ack hasn't been received
		/// within this amount of time, it is handled as a disconnect. Note that this value doesn't mean that you
		/// get kicked immediatly after 750 ms, as the check is performed in the movement packet handler. So,
		/// if you don't move, for example, the ack can be delayed an infinite amount of time until you finally move.
		static constexpr GameTime ClientAckTimeoutToleranceMs = 1500;

		// No pending movement change = no timed out change
		if (m_pendingMoveChanges.empty())
		{
			return false;
		}

		// Compare timestamp
		const GameTime now = GetAsyncTimeMs();
		const GameTime timeout = m_pendingMoveChanges.front().timestamp + ClientAckTimeoutToleranceMs;
		return (timeout <= now);
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

	void GameUnitS::Relocate(const Vector3& position, const Radian& facing)
	{
		GameObjectS::Relocate(position, facing);

		if (GetPosition() != position)
		{
			m_spellCast->StopCast(spell_interrupt_flags::Movement);
		}
	}

	void GameUnitS::ApplyMovementInfo(const MovementInfo& info)
	{
		if (m_movementInfo.position != info.position)
		{
			m_spellCast->StopCast(spell_interrupt_flags::Movement);
		}

		GameObjectS::ApplyMovementInfo(info);
	}

	PowerType GameUnitS::GetPowerTypeByUnitMod(UnitMods mod)
	{
		switch (mod)
		{
		case unit_mods::Rage:
			return power_type::Rage;
		case unit_mods::Energy:
			return power_type::Energy;

		default:
			break;
		}

		return power_type::Mana;
	}

	UnitMods GameUnitS::GetUnitModByStat(uint8 stat)
	{
		switch (stat)
		{
		case 1:
			return unit_mods::StatStrength;
		case 2:
			return unit_mods::StatAgility;
		case 3:
			return unit_mods::StatIntellect;
		case 4:
			return unit_mods::StatSpirit;

		default:
			break;
		}

		return unit_mods::StatStamina;
	}

	UnitMods GameUnitS::GetUnitModByPower(PowerType power)
	{
		switch (power)
		{
		case power_type::Rage:
			return unit_mods::Rage;
		case power_type::Energy:
			return unit_mods::Energy;

		default:
			break;
		}

		return unit_mods::Mana;
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

	SpellCastResult GameUnitS::CastSpell(const SpellTargetMap& target, const proto::SpellEntry& spell, const uint32 castTimeMs, bool isProc, uint64 itemGuid)
	{
		if (itemGuid == 0 && !HasSpell(spell.id()))
		{
			WLOG("Unit does not know spell " << spell.id());
			return spell_cast_result::FailedNotKnown;
		}

		const auto result = m_spellCast->StartCast(spell, target, castTimeMs, isProc, itemGuid);
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

	void GameUnitS::CancelCast(SpellInterruptFlags reason, GameTime interruptCooldown)
	{
		m_spellCast->StopCast(reason, interruptCooldown);
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

		if(Get<uint32>(object_fields::PowerType) == power_type::Rage)
		{
			const uint32 maxHealth = GetMaxHealth();
			const float K = 30.0f;

			int32 rageGenerated = static_cast<int32>((damage / maxHealth) * K);
			if (rageGenerated < 1)
			{
				rageGenerated = 1;
			}

			AddPower(power_type::Rage, rageGenerated);
		}

		// Kill event
		if (health < 1)
		{
			OnKilled(instigator);
		}
	}

	int32 GameUnitS::Heal(uint32 amount, GameUnitS* instigator)
	{
		uint32 health = Get<uint32>(object_fields::Health);
		if (health < 1)
		{
			return 0;
		}

		const uint32 maxHealth = Get<uint32>(object_fields::MaxHealth);
		if (health >= maxHealth)
		{
			return 0;
		}

		if (health + amount > maxHealth)
		{
			amount = maxHealth - health;
		}

		Set<uint32>(object_fields::Health, health + amount);
		return static_cast<int32>(amount);
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

	void GameUnitS::ApplyAura(std::unique_ptr<AuraContainer>&& aura)
	{
		ASSERT(aura);

		// Remove existing auras first
		for (auto it = m_auras.begin(); it != m_auras.end();)
		{
			if (auto& existingAura = *it; aura->ShouldOverwriteAura(*existingAura))
			{
				it = m_auras.erase(it);
			}
			else
			{
				++it;
			}
		}

		// Apply new aura
		const auto& appliedAura = m_auras.emplace_back(std::move(aura));
		appliedAura->SetApplied(true);
	}

	void GameUnitS::BuildAuraPacket(io::Writer& writer) const
	{
		writer << io::write_packed_guid(GetGuid());

		uint32 visibleAuraCount = 0;
		const size_t countPos = writer.Sink().Position();
		writer << io::write<uint32>(visibleAuraCount);

		// Iterate through visible auras
		for (const auto& aura : m_auras)
		{
			if (aura->IsVisible())
			{
				aura->WriteAuraUpdate(writer);
				visibleAuraCount++;
			}
		}

		// Write actual visible aura count
		writer.Sink().Overwrite(countPos, reinterpret_cast<const char*>(&visibleAuraCount), sizeof(uint32));
	}

	void GameUnitS::NotifyManaUsed()
	{
		m_lastManaUse = GetAsyncTimeMs();
	}

	void GameUnitS::TeleportOnMap(const Vector3& position, const Radian& facing)
	{
		// Update position and facing
		Relocate(position, facing);

		// Notify net watcher
		if (m_netUnitWatcher)
		{
			m_netUnitWatcher->OnTeleport(position, facing);
		}
	}

	void GameUnitS::SetVictim(const std::shared_ptr<GameUnitS>& victim)
	{
		m_victimSignals.disconnect();

		m_victim = victim;

		if (victim)
		{
			m_victimSignals += {
				victim->killed.connect(this, &GameUnitS::VictimKilled),
					victim->despawned.connect(this, &GameUnitS::VictimDespawned),
			};
		}
	}

	void GameUnitS::VictimKilled(GameUnitS* killer)
	{
		StopAttack();
	}

	void GameUnitS::VictimDespawned(GameObjectS&)
	{
		StopAttack();
	}

	float GameUnitS::MeleeMissChance(const GameUnitS& victim, weapon_attack::Type attackType, int32 skillDiff, uint32 spellId) const
	{	
		const proto::SpellEntry* spell = spellId ? m_project.spells.getById(spellId) : nullptr;
		// TODO: Check for can't miss attribute on spell and if if it can't miss, return 0.0f

		float missChance = victim.GetUnitMissChance();

		// Off hand attacks have an additional 19% miss chance
		if (!spellId && HasOffhandWeapon() && attackType != weapon_attack::RangedAttack)
		{
			missChance += 19.0f;
		}

		// Calculate skill diff
		int32 diff = -skillDiff;
		missChance += diff > 10 ? 1 + (diff - 10) * 0.4f : diff * 0.1f;

		return std::min(std::max(missChance, 0.0f), 100.0f);
	}

	float GameUnitS::CriticalHitChance(const GameUnitS& victim, weapon_attack::Type attackType) const
	{
		// TODO
		return 5.0f;
	}

	float GameUnitS::DodgeChance()
	{
		return 5.0f;
	}

	float GameUnitS::ParryChance()
	{
		return 5.0f;
	}

	float GameUnitS::BlockChance()
	{
		return 5.0f;
	}

	float GameUnitS::GetUnitMissChance() const
	{
		// Base miss chance is 5%
		float miss_chance = 5.0f;

		// TODO: Maybe increase this for players based on their defense skill

		return miss_chance;
	}

	bool GameUnitS::HasOffhandWeapon() const
	{
		return false;
	}

	bool GameUnitS::CanDualWield() const
	{
		return m_canDualWield;
	}

	int32 GameUnitS::GetMaxSkillValueForLevel(uint32 level) const
	{
		return 5 * level;
	}

	MeleeAttackOutcome GameUnitS::RollMeleeOutcomeAgainst(GameUnitS& victim, const WeaponAttack attackType) const
	{
		const int32 attackerMaxSkillValueForLevel = GetMaxSkillValueForLevel(GetLevel());
		const int32 victimMaxSkillValueForLevel = GetMaxSkillValueForLevel(victim.GetLevel());

		// TODO: Get actual skill values. For now they are considered both at maximum for the respective level
		const int32 attackerWeaponSkill = attackerMaxSkillValueForLevel;
		const int32 victimDefenseSkill = victimMaxSkillValueForLevel;

		// Calculate miss chance
		const float missChance = MeleeMissChance(victim, attackType, attackerWeaponSkill - victimDefenseSkill, 0);

		// Calculate crit chance
		const float critChance = CriticalHitChance(victim, attackType);

		const float dodgeChance = victim.DodgeChance();
		const float blockChance  = victim.BlockChance();
		const float parryChance = victim.ParryChance();

		// Order of checks: Miss -> Dodge -> Parry -> Glancing -> Block -> Crushing -> Crit -> Normal

		// TODO: Add stuff like immunities, miss chance, dodge, parry, glancing, crushing, crit, block, absorb etc.
		std::uniform_real_distribution chanceDistribution(0.0f, 100.0f);
		if (chanceDistribution(randomGenerator) < missChance)
		{
			return MeleeAttackOutcome::Miss;
		}

		if (chanceDistribution(randomGenerator) < dodgeChance)
		{
			return MeleeAttackOutcome::Dodge;
		}

		if (chanceDistribution(randomGenerator) < parryChance)
		{
			return MeleeAttackOutcome::Parry;
		}

		// Glancing check only if target is at our or higher level
		if (GetLevel() <= victim.GetLevel())
		{
			// Per skill diff, 5% chance up to 40%
			const float glancingChance = std::min((attackerWeaponSkill - victimDefenseSkill) * 5.0f, 40.0f);
			if (chanceDistribution(randomGenerator) < glancingChance)
			{
				return MeleeAttackOutcome::Glancing;
			}
		}

		// Crushing check if our level is 4 or more above the target
		if (GetLevel() >= victim.GetLevel() + 4)
		{
			// Per skill diff, 2% chance
			const float crushChance = std::min((attackerWeaponSkill - victimDefenseSkill) * 2.0f, 0.0f);
			if (chanceDistribution(randomGenerator) < crushChance)
			{
				return MeleeAttackOutcome::Crushing;
			}
		}

		// Crit check last
		if (chanceDistribution(randomGenerator) < critChance)
		{
			return MeleeAttackOutcome::Crit;
		}

		return MeleeAttackOutcome::Normal;
	}

	void GameUnitS::StartAttack(const std::shared_ptr<GameUnitS>& victim)
	{
		ASSERT(victim);

		if (IsAttacking(victim))
		{
			return;
		}

		SetTarget(victim ? victim->GetGuid() : 0);
		if (victim->GetGuid() == GetGuid() || UnitIsFriendly(*victim))
		{
			// Unit is not an enemy, so we wont attack
			StopAttack();
			return;
		}

		SetVictim(victim);

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

		// Attacking
		AddFlag<uint32>(object_fields::Flags, unit_flags::Attacking);

		// Trigger next attack swing
		TriggerNextAutoAttack();
	}

	void GameUnitS::StopAttack()
	{
		m_attackSwingCountdown.Cancel();
		SetVictim(nullptr);

		// No longer attacking
		RemoveFlag<uint32>(object_fields::Flags, unit_flags::Attacking);

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
		auto victim = m_victim.lock();
		Set<uint64>(object_fields::TargetUnit, targetGuid);

		if (victim && victim->GetGuid() != targetGuid)
		{
			GameObjectS* object = GetWorldInstance()->FindObjectByGuid(targetGuid);
			if (object)
			{
				SetVictim(std::dynamic_pointer_cast<GameUnitS>(object->shared_from_this()));
			}
			else
			{
				StopAttack();
			}
		}
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

	float GameUnitS::GetBaseSpeed(const MovementType type)
	{
		switch (type)
		{
		case movement_type::Walk:
			return 2.5f;
		case movement_type::Run:
			return 7.0f;
		case movement_type::Backwards:
			return 4.5f;
		case movement_type::Swim:
			return 4.75f;
		case movement_type::SwimBackwards:
			return 2.5f;
		case movement_type::Turn:
			return 3.1415927f;
		case movement_type::Flight:
			return 7.0f;
		case movement_type::FlightBackwards:
			return 4.5f;
		default:
			return 0.0f;
		}
	}

	float GameUnitS::GetSpeed(const MovementType type) const
	{
		const float baseSpeed = GetBaseSpeed(type);
		return baseSpeed * m_speedBonus[type];
	}


	void GameUnitS::NotifySpeedChanged(MovementType type, bool initial)
	{
		float speed = 1.0f;

		MovementChangeType changeType = MovementChangeType::SpeedChangeRun;

		// Apply speed buffs
		int32 mainSpeedMod = 0;
		float stackBonus = 1.0f, nonStackBonus = 1.0f;

		mainSpeedMod = GetMaximumBasePoints(aura_type::ModIncreaseSpeed);
		stackBonus = GetTotalMultiplier(aura_type::ModSpeedAlways);
		nonStackBonus = (100.0f + static_cast<float>(GetMaximumBasePoints(aura_type::ModSpeedNonStacking))) / 100.0f;

		const float bonus = nonStackBonus > stackBonus ? nonStackBonus : stackBonus;
		speed = mainSpeedMod ? bonus * (100.0f + static_cast<float>(mainSpeedMod)) / 100.0f : bonus;

		// Apply slow buffs
		int32 slow = GetMinimumBasePoints(aura_type::ModDecreaseSpeed);
		int32 slowNonStack = GetMinimumBasePoints(aura_type::ModSpeedNonStacking);
		slow = slow < slowNonStack ? slow : slowNonStack;

		// Slow has to be <= 0
		ASSERT(slow <= 0);
		if (slow)
		{
			speed += (speed * static_cast<float>(slow) / 100.0f);
		}

		float oldBonus = m_speedBonus[type];

		// If there is a pending movement change...
		if (!initial)
		{
			if (!m_pendingMoveChanges.empty())
			{
				// Iterate backwards until we find a pending movement change for this move type
				for (auto it = m_pendingMoveChanges.crbegin(); it != m_pendingMoveChanges.crend(); ++it)
				{
					if (it->changeType == changeType)
					{
						DLOG("Found pending move change for type " << type << ": using this as old value");
						oldBonus = it->speed / GetBaseSpeed(type);
						break;
					}
				}
			}
		}

		if (oldBonus != speed)
		{
			// If there is a watcher, we need to notify him about this change first, and he needs
			// to send an ack packet before we finally apply the speed change. If there is no
			// watcher, we simply apply the speed change as this is most likely a creature which isn't
			// controlled by a player (however, mind-controlled creatures will have a m_netWatcher).
			if (m_netUnitWatcher != nullptr && !initial)
			{
				const uint32 ackId = GenerateAckId();
				const float absSpeed = GetBaseSpeed(type) * speed;

				// Expect ack opcode
				PendingMovementChange change;
				change.counter = ackId;
				change.changeType = changeType;
				change.speed = absSpeed;
				change.timestamp = GetAsyncTimeMs();
				PushPendingMovementChange(change);

				// Notify the watcher
				m_netUnitWatcher->OnSpeedChangeApplied(type, absSpeed, ackId);
			}
			else
			{
				// Immediately apply speed change
				ApplySpeedChange(type, speed);
			}
		}
	}

	void GameUnitS::ApplySpeedChange(MovementType type, float speed, bool initial)
	{
		// Now store the speed bonus value
		m_speedBonus[type] = speed;

		// Notify all tile subscribers about this event
		if (!initial)
		{
			// Send packets to all listeners around except ourself
			std::vector<char> buffer;
			io::VectorSink sink(buffer);
			game::Protocol::OutgoingPacket packet(sink);

			static const uint16 moveOpCodes[MovementType::Count] = {
				game::realm_client_packet::MoveSetWalkSpeed,
				game::realm_client_packet::MoveSetRunSpeed,
				game::realm_client_packet::MoveSetRunBackSpeed,
				game::realm_client_packet::MoveSetSwimSpeed,
				game::realm_client_packet::MoveSetSwimBackSpeed,
				game::realm_client_packet::MoveSetTurnRate,
				game::realm_client_packet::SetFlightSpeed,
				game::realm_client_packet::SetFlightBackSpeed
			};

			packet.Start(moveOpCodes[type]);
			packet
				<< io::write_packed_guid(GetGuid())
				<< GetMovementInfo()
				<< io::write<float>(speed * GetBaseSpeed(type));
			packet.Finish();

			ForEachSubscriberInSight(
				[&packet, &buffer, this](TileSubscriber& subscriber)
				{
					if (&subscriber.GetGameUnit() != this)
					{
						subscriber.SendPacket(packet, buffer);
					}
				});
		}

		// Notify the unit mover about this change
		m_mover->OnMoveSpeedChanged(type);
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

	bool GameUnitS::UnitIsEnemy(const GameUnitS& other) const
	{
		const proto::FactionTemplateEntry* faction = GetFactionTemplate();
		const proto::FactionTemplateEntry* otherFaction = other.GetFactionTemplate();

		if (!faction || !otherFaction)
		{
			return false;
		}

		for (int i = 0; i < faction->enemies_size(); ++i)
		{
			const auto& enemy = faction->enemies(i);
			if (enemy == otherFaction->faction())
			{
				return true;
			}
		}

		for (int i = 0; i < faction->friends_size(); ++i)
		{
			const auto& friendly = faction->friends(i);
			if (friendly == otherFaction->faction())
			{
				return false;
			}
		}

		if (faction->enemymask() != 0 &&(faction->enemymask() & otherFaction->selfmask()) != 0)
		{
			return true;
		}

		return false;
	}

	bool GameUnitS::UnitIsFriendly(const GameUnitS& other) const
	{
		const proto::FactionTemplateEntry* faction = GetFactionTemplate();
		const proto::FactionTemplateEntry* otherFaction = other.GetFactionTemplate();

		if (!faction || !otherFaction)
		{
			return false;
		}

		for (int i = 0; i < faction->enemies_size(); ++i)
		{
			const auto& enemy = faction->enemies(i);
			if (enemy == otherFaction->faction())
			{
				return false;
			}
		}

		for (int i = 0; i < faction->friends_size(); ++i)
		{
			const auto& friendly = faction->friends(i);
			if (friendly == otherFaction->faction())
			{
				return true;
			}
		}

		return (faction->friendmask() & otherFaction->selfmask()) != 0;
	}

	const proto::FactionTemplateEntry* GameUnitS::GetFactionTemplate() const
	{
		// Do we have a cache?
		if (m_cachedFactionTemplate)
		{
			// Check if cache is still valid
			if (Get<uint32>(object_fields::FactionTemplate) == m_cachedFactionTemplate->id())
			{
				// All good, return cache
				return m_cachedFactionTemplate;
			}
		}

		// Refresh or build cache
		m_cachedFactionTemplate = m_project.factionTemplates.getById(Get<uint32>(object_fields::FactionTemplate));
		return m_cachedFactionTemplate;
	}

	void GameUnitS::SetBinding(uint32 mapId, const Vector3& position, const Radian& facing)
	{
		m_bindMap = mapId;
		m_bindPosition = position;
		m_bindFacing = facing;
	}

	void GameUnitS::OnKilled(GameUnitS* killer)
	{
		m_spellCast->StopCast(spell_interrupt_flags::Any);

		StopAttack();
		SetTarget(0);
		StopRegeneration();

		Set<uint64>(object_fields::TargetUnit, 0);
		RemoveFlag<uint32>(object_fields::Flags, unit_flags::InCombat);

		// For now, remove all auras
		for (auto& aura : m_auras)
		{
			aura->SetApplied(false);
		}

		m_auras.clear();

		killed(killer);
	}

	void GameUnitS::OnSpellCastEnded(bool succeeded)
	{
		if (std::shared_ptr<GameUnitS> victim = m_victim.lock())
		{
			m_lastMainHand = m_lastOffHand = GetAsyncTimeMs();
			if (!m_attackSwingCountdown.IsRunning())
			{
				TriggerNextAutoAttack();
			}
		}
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

		const uint32 maxHealth = GetMaxHealth();
		uint32 health = GetHealth();

		health += m_healthRegenPerTick;
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

		int32 amount = 0;
		switch(powerType)
		{
		case power_type::Rage:
			amount -= 3;
			break;
		case power_type::Energy:
			amount += 20;
			break;
		case power_type::Mana:
			// Don't regen mana if we used mana in the last 5 seconds
			if (GetAsyncTimeMs() - m_lastManaUse < 5000)
			{
				break;
			}

			amount += static_cast<int32>(m_manaRegenPerTick);
			break;
		}

		AddPower(powerType, amount);
	}

	void GameUnitS::AddPower(PowerType powerType, int32 amount)
	{
		// Get power and max power
		int32 power = Get<int32>(object_fields::Mana + static_cast<uint8>(powerType));
		const int32 maxPower = Get<uint32>(object_fields::MaxMana + static_cast<uint8>(powerType));

		power += amount;

		if (power < 0) power = 0;
		if (power > maxPower) power = maxPower;

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

	int32 GameUnitS::GetMaximumBasePoints(const AuraType type) const
	{
		int32 threshold = 0;

		for (auto& aura : m_auras)
		{
			if (!aura->IsApplied())
			{
				continue;
			}

			const int32 max = aura->GetMaximumBasePoints(type);
			if (max > threshold)
			{
				threshold = max;
			}
		}

		return threshold;
	}

	int32 GameUnitS::GetMinimumBasePoints(const AuraType type) const
	{
		int32 threshold = 0;

		for (auto& aura : m_auras)
		{
			if (!aura->IsApplied())
			{
				continue;
			}

			const int32 min = aura->GetMinimumBasePoints(type);
			if (min < threshold)
			{
				threshold = min;
			}
		}

		return threshold;
	}

	float GameUnitS::GetTotalMultiplier(const AuraType type) const
	{
		float multiplier = 1.0f;

		for (auto& aura : m_auras)
		{
			if (!aura->IsApplied())
			{
				continue;
			}

			multiplier *= aura->GetTotalMultiplier(type);
		}

		return multiplier;
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

		// Calculate melee hit outcome
		const MeleeAttackOutcome outcome = RollMeleeOutcomeAgainst(*victim, WeaponAttack::BaseAttack);

		// Calculate damage between minimum and maximum damage
		std::uniform_real_distribution distribution(Get<float>(object_fields::MinDamage), Get<float>(object_fields::MaxDamage) + 1.0f);
		uint32 totalDamage = victim->CalculateArmorReducedDamage(Get<uint32>(object_fields::Level), static_cast<uint32>(distribution(randomGenerator)));

		if (outcome == MeleeAttackOutcome::Crit)
		{
			totalDamage *= 2;
		}
		else if(outcome == melee_attack_outcome::Crushing)
		{
			totalDamage *= 4;
		}
		else if (outcome == melee_attack_outcome::Glancing)
		{
			totalDamage *= 0.15f;
		}
		else if (outcome == melee_attack_outcome::Miss ||
			outcome == melee_attack_outcome::Parry ||
			outcome == melee_attack_outcome::Dodge)
		{
			totalDamage = 0;
		}

		victim->Damage(totalDamage, spell_school::Normal, this);
		if (m_netUnitWatcher)
		{
			m_netUnitWatcher->OnNonSpellDamageLog(victim->GetGuid(), totalDamage, outcome == melee_attack_outcome::Crit ? damage_flags::Crit : damage_flags::None);
		}

		if (totalDamage > 0 &&
			Get<uint32>(object_fields::PowerType) == power_type::Rage)
		{
			const uint32 level = Get<uint32>(object_fields::Level);
			const float C = 3.0f;
			const float B = 5.0f;	// TODO: Maybe make this depend on weapon speed

			int32 rageGenerated = static_cast<int32>((static_cast<float>(totalDamage) / (C * static_cast<float>(level))) + B);
			if (rageGenerated < 1) rageGenerated = 1;

			AddPower(power_type::Rage, rageGenerated);
		}

		// In case of success, we also want to trigger an event to potentially reset error states from previous attempts
		OnAttackSwingEvent(AttackSwingEvent::Success);
		TriggerNextAutoAttack();
	}

	io::Writer& operator<<(io::Writer& w, GameUnitS const& object)
	{
		w << reinterpret_cast<GameObjectS const&>(object);
		w
			<< io::write<uint32>(object.m_bindMap)
			<< io::write<float>(object.m_bindPosition.x)
			<< io::write<float>(object.m_bindPosition.y)
			<< io::write<float>(object.m_bindPosition.z)
			<< io::write<float>(object.m_bindFacing.GetValueRadians());

		return w;
	}

	io::Reader& operator>>(io::Reader& r, GameUnitS& object)
	{
		// Read values
		r >> reinterpret_cast<GameObjectS&>(object);

		float facing;
		r
			>> io::read<uint32>(object.m_bindMap)
			>> io::read<float>(object.m_bindPosition.x)
			>> io::read<float>(object.m_bindPosition.y)
			>> io::read<float>(object.m_bindPosition.z)
			>> io::read<float>(facing);
		if (r)
		{
			object.m_bindFacing = Radian(facing);
		}

		return r;
	}
}
