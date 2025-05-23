// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include <algorithm>

#include "game_server/spells/aura_container.h"

#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_unit_s.h"
#include "game_server/spells/spell_cast.h"
#include "base/clock.h"
#include "base/utilities.h"
#include "binary_io/vector_sink.h"
#include "proto_data/project.h"

namespace mmo
{
	AuraContainer::AuraContainer(GameUnitS& owner, const uint64 casterId, const proto::SpellEntry& spell, const GameTime duration, const uint64 itemGuid)
		: m_owner(owner)
		, m_casterId(casterId)
		, m_spell(spell)
		, m_duration(duration)
		, m_expirationCountdown(owner.GetTimers())
		, m_itemGuid(itemGuid)
		, m_areaAuraTick(owner.GetTimers())
	{
		m_areaAuraTickConnection = m_areaAuraTick.ended.connect(this, &AuraContainer::HandleAreaAuraTick);

		// Apply proc chance
		m_procChance = m_spell.procchance();
		GetCaster()->ApplySpellMod(spell_mod_op::ChanceOfSuccess, spell.id(), m_procChance);

		// Initialize proc charges if applicable
		if (m_spell.proccharges() > 0)
		{
			m_baseProcCharges = m_spell.proccharges();
			m_procCharges = m_baseProcCharges;
		}
	}

	AuraContainer::~AuraContainer()
	{
		m_expirationCountdown.Cancel();

		if (m_applied)
		{
			SetApplied(false);
		}

		m_auras.clear();
	}

	void AuraContainer::AddAuraEffect(const proto::SpellEffect& effect, int32 basePoints)
	{
		// Check if this aura is an area aura and thus needs special handling
		if (effect.type() == spell_effects::ApplyAreaAura)
		{
			m_areaAura = true;
		}

		// Add aura to the list of effective auras
		m_auras.emplace_back(std::make_shared<AuraEffect>(
			*this,
			effect,
			m_owner.GetTimers(),
			basePoints));
	}

	void AuraContainer::SetApplied(bool apply, bool notify)
	{
		if ((m_applied && apply) ||
			(!m_applied && !apply))
		{
			return;
		}

		m_applied = apply;

		if (notify && m_owner.GetWorldInstance())
		{
			// TODO: Flag this aura as updated so we only sync changed auras to units which already know about this unit's auras instead
			// of having to sync ALL unit auras over and over again

			// Auras changed, flag object for next update loop
			m_owner.GetWorldInstance()->AddObjectUpdate(m_owner);
		}

		// Does this aura expire?
		if (apply)
		{			// Start ticking area auras
			if (IsAreaAura())
			{
				// Do an initial tick
				m_areaAuraTick.SetEnd(GetAsyncTimeMs() + constants::OneSecond);
			}

			m_ownerEventConnections += m_owner.takenDamage.connect(this, &AuraContainer::OnOwnerDamaged);

			// Register aura for proc events if needed
			if (CanProc() && !m_procRegistered)
			{
				// Register proc handlers based on which proc flags are set
				const uint32 procFlags = GetProcFlags();

				// Reset proc charges if applicable
				if (m_baseProcCharges > 0)
				{
					m_procCharges = m_baseProcCharges;
				}

				// Connect to the owner's proc events
				if (procFlags != 0)
				{
					// Connect to all applicable proc events
					// Specific proc connections would be added here based on proc flags
					// These would connect to GameUnitS event signals
					m_procRegistered = true;
				}
			}

			if (m_duration > 0)
			{
				if (!m_expiredConnection)
				{
					std::weak_ptr<AuraContainer> weakThis = weak_from_this();
					m_expiredConnection = m_expirationCountdown.ended += [weakThis]()
						{
							if (const auto strong = weakThis.lock())
							{
								strong->RemoveSelf();
							}
						};
				}

				m_expiration = GetAsyncTimeMs() + m_duration;
				m_expirationCountdown.SetEnd(m_expiration);
			}
		}		else
		{
			m_ownerEventConnections.disconnect();
			m_expirationCountdown.Cancel();
			m_expiredConnection.disconnect();

			// Stop ticking area aura update
			if (IsAreaAura())
			{
				m_areaAuraTick.Cancel();
			}

			// Clean up proc registration
			if (m_procRegistered)
			{
				m_procRegistered = false;
			}
		}

		for(const auto& aura : m_auras)
		{
			aura->HandleEffect(m_applied);
		}
	}

	bool AuraContainer::IsExpired() const
	{
		return m_duration > 0 && m_expiration <= GetAsyncTimeMs();
	}

	void AuraContainer::WriteAuraUpdate(io::Writer& writer) const
	{
		const uint32 now = GetAsyncTimeMs();
		writer << io::write<uint32>(m_spell.id());
		writer << io::write<uint32>(m_expiration > now ? m_expiration - now : 0);
		writer << io::write_packed_guid(m_casterId);

		writer << io::write<uint8>(m_auras.size());
		for (uint32 i = 0; i < m_auras.size(); ++i)
		{
			const auto& aura = m_auras[i];
			writer << io::write<int32>(aura->GetBasePoints());
		}
	}

	bool AuraContainer::HasEffect(const AuraType type) const
	{
		for (const auto& aura : m_auras)
		{
			if (aura->GetType() == type)
			{
				return true;
			}
		}

		return false;
	}

	void AuraContainer::NotifyOwnerMoved()
	{
		if (!m_applied)
		{
			return;
		}

		// Should aura be removed when moving?
		if (m_spell.aurainterruptflags() & spell_aura_interrupt_flags::Move)
		{
			RemoveSelf();
		}
	}

	void AuraContainer::RemoveSelf()
	{
		SetApplied(false);
		m_owner.RemoveAura(shared_from_this());
	}

	bool AuraContainer::ShouldRemoveAreaAuraDueToCasterConditions(const std::shared_ptr<GameUnitS>& caster, const uint64 ownerGroupId, const Vector3& position, const float range) const
	{
		// If caster pointer is invalid
		if (!caster)
		{
			return true;
		}

		// Must be a player
		if (!caster->IsPlayer())
		{
			return true;
		}

		const GamePlayerS& casterPlayer = caster->AsPlayer();

		// Must be in same group; group must be non-zero
		if (casterPlayer.GetGroupId() != ownerGroupId || ownerGroupId == 0)
		{
			return true;
		}

		// Must be friendly
		if (!casterPlayer.UnitIsFriendly(m_owner))
		{
			return true;
		}

		// Must be in range
		if (const float distanceSq = casterPlayer.GetSquaredDistanceTo(position, true); distanceSq > (range * range))
		{
			return true;
		}

		return false;
	}

	void AuraContainer::OnOwnerDamaged(GameUnitS* instigator, uint32 school, const DamageType type)
	{
		if (m_spell.aurainterruptflags() & spell_aura_interrupt_flags::Damage)
		{
			RemoveSelf();
			return;
		}

		if (m_spell.aurainterruptflags() & spell_aura_interrupt_flags::DirectDamage)
		{
			if (type != damage_type::Periodic)
			{
				RemoveSelf();
				return;
			}
		}

		if (m_spell.aurainterruptflags() & spell_aura_interrupt_flags::HitBySpell)
		{
			if (type == damage_type::MagicalAbility)
			{
				RemoveSelf();
				return;
			}
		}
	}

	void AuraContainer::HandleAreaAuraTick()
	{
		// Should only ever be active for players right now!
		ASSERT(m_owner.IsPlayer());
		ASSERT(m_applied);

		GamePlayerS& owner = m_owner.AsPlayer();
		const uint64 groupId = owner.GetGroupId();

		const auto* rangeType = owner.GetProject().ranges.getById(m_spell.rangetype());
		ASSERT(rangeType);

		const float range = rangeType->range();
		const Vector3& position = m_owner.GetPosition();

		// Check if this is our area aura or if we got this from someone else
		if (GetCasterId() == m_owner.GetGuid())
		{
			// It's our own aura
			if (groupId != 0)
			{
				// Search for nearby party members and apply the aura to them
				m_owner.GetWorldInstance()->GetUnitFinder().FindUnits(Circle(position.x, position.z, range), [&owner, this](GameUnitS& unit) -> bool
					{
						// Skip ourselves!
						if (unit.GetGuid() == owner.GetGuid())
						{
							return true;
						}

						// Only players
						if (!unit.IsPlayer())
						{
							return true;
						}

						// Must be in the same group and friendly
						if (GamePlayerS& player = unit.AsPlayer(); player.GetGroupId() == owner.GetGroupId() && player.UnitIsFriendly(owner))
						{
							// Aura already active from same caster?
							if (!player.HasAuraSpellFromCaster(m_spell.id(), m_casterId))
							{
								// Apply the aura
								auto container = std::make_shared<AuraContainer>(player, m_casterId, m_spell, m_duration, m_itemGuid);
								for (const auto& effect : m_auras)
								{
									container->AddAuraEffect(effect->GetEffect(), effect->GetBasePoints());
								}
								player.ApplyAura(std::move(container));
							}
						}

						return true;
					});
			}
		}
		else
		{
			// It's from someone else; check if we should remove it
			const std::shared_ptr<GameUnitS> caster = m_caster.lock();
			if (ShouldRemoveAreaAuraDueToCasterConditions(caster, groupId, position, range))
			{
				RemoveSelf();
				return;
			}
		}

		m_areaAuraTick.SetEnd(GetAsyncTimeMs() + constants::OneSecond);
	}

	uint32 AuraContainer::GetSpellId() const
	{
		return m_spell.id();
	}

	int32 AuraContainer::GetMaximumBasePoints(const AuraType type) const
	{
		int32 threshold = 0;

		for (auto it = m_auras.begin(); it != m_auras.end(); ++it)
		{
			if (it->get()->GetType() != type)
			{
				continue;
			}

			threshold = std::max(it->get()->GetBasePoints(), threshold);
		}

		return threshold;
	}

	int32 AuraContainer::GetMinimumBasePoints(AuraType type) const
	{
		int32 threshold = 0;

		for (auto it = m_auras.begin(); it != m_auras.end(); ++it)
		{
			if (it->get()->GetType() != type)
			{
				continue;
			}

			threshold = std::min(it->get()->GetBasePoints(), threshold);
		}

		return threshold;
	}

	float AuraContainer::GetTotalMultiplier(const AuraType type) const
	{
		float multiplier = 1.0f;

		for (auto it = m_auras.begin(); it != m_auras.end(); ++it)
		{
			if (it->get()->GetType() != type)
			{
				continue;
			}

			multiplier *= (100.0f + static_cast<float>(it->get()->GetBasePoints())) / 100.0f;
		}

		return multiplier;
	}

	bool AuraContainer::ShouldOverwriteAura(const AuraContainer& other) const
	{
		// NOTE: If we return true here, the other aura will be removed and replaced by this aura container instead

		if (&other == this)
		{
			return true;
		}

		const bool sameBaseSpellId = HasSameBaseSpellId(other.GetSpell());
		const bool sameSpellId = sameBaseSpellId || (other.GetSpellId() == GetSpellId());
		const bool onlyOneStackTotal = (m_spell.attributes(0) & spell_attributes::OnlyOneStackTotal) != 0;
		const bool sameCaster = other.GetCasterId() == GetCasterId();
		const bool sameItem = other.GetItemGuid() == GetItemGuid();

		// Right now, same caster and same spell id means we overwrite the old aura with this one
		// TODO: maybe add some settings here to explicitly allow stacking?
		if (sameCaster && sameSpellId && sameItem)
		{
			return true;
		}

		// Same spell but different casters: If we allow stacking for different casters
		if (sameSpellId && !sameCaster && onlyOneStackTotal)
		{
			return true;
		}

		// Should not overwrite, but create a whole new aura
		return false;
	}

	bool AuraContainer::HasSameBaseSpellId(const proto::SpellEntry& spell) const
	{
		if (spell.baseid() == 0)
		{
			return false;
		}

		const uint32 baseSpellId = GetBaseSpellId();
		if (baseSpellId == spell.baseid())
		{
			return true;
		}

		return false;
	}

	GameUnitS* AuraContainer::GetCaster() const
	{
		std::shared_ptr<GameUnitS> strongCaster = m_caster.lock();
		if (!strongCaster)
		{
			if (!m_owner.GetWorldInstance())
			{
				return nullptr;
			}

			if (GameUnitS* caster = m_owner.GetWorldInstance()->FindByGuid<GameUnitS>(m_casterId))
			{
				m_caster = std::static_pointer_cast<GameUnitS>(caster->shared_from_this());
				return caster;
			}

			return nullptr;
		}
		
		return strongCaster.get();
	}

	bool AuraContainer::HandleProc(uint32 procFlags, uint32 procEx, GameUnitS* target, uint32 damage, uint8 school, bool triggerByAura, uint64 familyFlags)
	{
		// First check if the aura can proc at all
		if (!IsApplied() || !CanProc())
		{
			return false;
		}

		// Check if we already triggered by another proc to avoid infinite loops
		if (triggerByAura)
		{
			return false;
		}

		// Check cooldown
		if (GetProcCooldown() > 0)
		{
			const uint32 currentTime = GetAsyncTimeMs();
			if (currentTime < m_lastProcTime + GetProcCooldown())
			{
				return false;
			}
		}

		// Check if this proc type matches our proc flags
		if (!CheckProcFlags(procFlags))
		{
			return false;
		}

		// Check if the school matches if a specific school was specified
		if (GetProcSchool() != 0 && school != 0 && GetProcSchool() != school)
		{
			return false;
		}

		// Check proc family flags if applicable
		if (GetProcFamily() != 0 && !CheckProcFamilyFlags(familyFlags))
		{
			return false;
		}

		// Roll the proc chance
		const uint32 procChance = GetProcChance();
		if (procChance == 0)
		{
			return false;
		}

		// Random roll (0-100)
		std::uniform_real_distribution<float> distribution(0.0f, 100.0f);
		const float roll = distribution(randomGenerator);
		if (roll > static_cast<float>(procChance))
		{
			return false;  // Failed the proc chance roll
		}

		// We passed all checks - the proc will activate!
		m_lastProcTime = GetAsyncTimeMs();

		// Execute the proc effects
		ExecuteProcEffects(target);

		// If we have charges, decrease them and check if we need to remove the aura
		if (m_baseProcCharges > 0)
		{
			if (m_procCharges > 0)
			{
				--m_procCharges;

				// If charges are depleted, remove the aura
				if (m_procCharges == 0)
				{
					RemoveSelf();
					return true;
				}
			}
		}

		return true;
	}

	bool AuraContainer::CheckProcFlags(uint32 eventFlags) const
	{
		// If no proc flags set on aura, it can't proc
		if (GetProcFlags() == 0)
		{
			return false;
		}

		// Check if any of the event flags match our proc flags
		return (GetProcFlags() & eventFlags) != 0;
	}

	bool AuraContainer::CheckProcFamilyFlags(uint64 familyFlags) const
	{
		// If no proc family flags set, any spell can trigger
		if (GetProcFamily() == 0 || familyFlags == 0)
		{
			return true;
		}

		// We need to check that the trigger was a spell with the given flag set
		if ((m_spell.procfamily() & familyFlags) == 0)
		{
			return false;
		}

		return true;
	}

	void AuraContainer::ExecuteProcEffects(GameUnitS* target)
	{
		// Check each aura effect to see if it's a proc effect
		for (const auto& aura : m_auras)
		{
			aura->HandleProcEffect(target);
		}
	}
}
