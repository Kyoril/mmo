// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <vector>

#include "spell_cast.h"
#include "base/countdown.h"
#include "base/typedefs.h"
#include "binary_io/writer.h"
#include "game/aura.h"
#include "game/damage_school.h"
#include "aura_effect.h"

namespace mmo
{
	class Vector3;

	namespace proto
	{
		class SpellEntry;
		class SpellEffect;
	}

	class GameUnitS;

	/// Holds and manages instances of auras for one unit.
	class AuraContainer final : public NonCopyable, public std::enable_shared_from_this<AuraContainer>
	{
	public:

		/// Initializes a new AuraContainer for a specific owner unit.
		explicit AuraContainer(GameUnitS &owner, uint64 casterId, const proto::SpellEntry& spell, GameTime duration, uint64 itemGuid);

		~AuraContainer() override;

	public:
		/// Adds a new aura effect to the container (an Aura can have multiple different effects like a movement speed slow and a damage over time effect - these would be two aura effects).
		///	Effects are grouped by the spell which applied the aura (a spell can have multiple ApplyAura effects). ApplyAura effects with the same target are grouped together in one such aura container.
		void AddAuraEffect(const proto::SpellEffect& effect, int32 basePoints);

		/// Marks the aura as applied or misapplied. If set to true, this will also make the aura effective!
		void SetApplied(bool apply, bool notify = true);

		/// Returns true if the aura can ever expire and is currently expired.
		bool IsExpired() const;

		/// Writes aura update data to a given writer (usually an outgoing network packet for clients).
		void WriteAuraUpdate(io::Writer& writer) const;

		bool HasEffect(AuraType type) const;

		void NotifyOwnerMoved();

		// Called when a proc event occurs to check if this aura should proc
		bool HandleProc(uint32 procFlags, uint32 procEx, GameUnitS* target, uint32 damage, uint8 school = 0, bool triggerByAura = false, uint64 familyFlags = 0);

	private:
		void HandleAreaAuraTick();

		void RemoveSelf();

		bool ShouldRemoveAreaAuraDueToCasterConditions(const std::shared_ptr<GameUnitS>& caster, uint64 ownerGroupId, const Vector3& position, float range) const;

		void OnOwnerDamaged(GameUnitS* instigator, uint32 school, DamageType type);

		// Executes the proc effects if available
		void ExecuteProcEffects(GameUnitS* target);
		
		// Checks if the spell procFlags match the triggered event
		bool CheckProcFlags(uint32 eventFlags) const;
		
		// Checks if the proc family flags match (if applicable)
		bool CheckProcFamilyFlags(uint64 familyFlags) const;

	public:
		/// Gets the owning unit of this aura (the target of the aura).
		GameUnitS& GetOwner() const
		{
			return m_owner;
		}

		/// Determines whether the aura container is currently applied (which means it is active). Only active auras can have an effect and be visible on the client.
		bool IsApplied() const
		{
			return m_applied;
		}

		/// Gets the guid of the caster who caused this aura.
		uint64 GetCasterId() const
		{
			return m_casterId;
		}

		/// Gets the spell id of the spell which caused this aura.
		uint32 GetSpellId() const;

		/// Gets the total aura duration (not the remaining time!) in milliseconds.
		GameTime GetDuration() const
		{
			return m_duration;
		}

		/// Determines whether the aura can ever expire.
		bool DoesExpire() const
		{
			return m_duration > 0;
		}

		/// Determines whether the aura is visible on client side.
		bool IsVisible() const
		{
			return
				IsApplied() && 
				!IsExpired() &&
				(m_spell.attributes(0) & spell_attributes::HiddenClientSide) == 0 &&
				(m_spell.attributes(1) & spell_attributes_b::HiddenAura) == 0;
		}
		bool IsAreaAura() const { return m_areaAura; }

		/// Determines if this aura container should react to proc events
		bool CanProc() const { return m_procChance > 0; }

		/// Gets the proc flags of the aura container's spell
		uint32 GetProcFlags() const { return m_spell.procflags(); }

		/// Gets the extended proc flags of the aura container's spell
		uint32 GetProcFlagsEx() const { return m_spell.procexflags(); }

		/// Gets the proc chance of the aura container's spell
		uint32 GetProcChance() const { return m_procChance; }

		/// Gets the proc cooldown of the aura container's spell
		uint32 GetProcCooldown() const { return m_spell.proccooldown(); }

		/// Gets the proc family of the aura container's spell (0 means no family restrictions)
		uint64 GetProcFamily() const { return m_spell.procfamily(); }

		/// Gets the school that can trigger this proc (or 0 for any school)
		uint32 GetProcSchool() const { return m_spell.procschool(); }

		/// Gets the maximum number of base points for a specific aura type.
		int32 GetMaximumBasePoints(AuraType type) const;

		/// Gets the minimum number of base points for a specified aura type.
		int32 GetMinimumBasePoints(AuraType type) const;

		/// Gets the total multiplier value for a specific aura type.
		float GetTotalMultiplier(AuraType type) const;

		/// Returns true if an aura container should be overwritten by this aura container.
		bool ShouldOverwriteAura(const AuraContainer& other) const;

		const proto::SpellEntry& GetSpell() const { return m_spell; }

		uint32 GetBaseSpellId() const { return GetSpell().baseid(); }

		uint32 GetSpellRank() const { return GetSpell().rank(); }

		bool HasSameBaseSpellId(const proto::SpellEntry& spell) const;

		GameUnitS* GetCaster() const;

		uint64 GetItemGuid() const { return m_itemGuid; }

	private:

		GameUnitS &m_owner;

		uint64 m_casterId;

		const proto::SpellEntry& m_spell;

		std::vector<std::shared_ptr<AuraEffect>> m_auras;

		bool m_applied = false;

		GameTime m_duration = 0;

		GameTime m_expiration = 0;

		Countdown m_expirationCountdown;

		mutable std::weak_ptr<GameUnitS> m_caster;

		uint64 m_itemGuid = 0;

		scoped_connection m_expiredConnection;

		bool m_areaAura = false;
		Countdown m_areaAuraTick;

		scoped_connection m_areaAuraTickConnection;

		scoped_connection_container m_ownerEventConnections;

		// Proc-related variables
		uint32 m_procCharges = 0;                   // Current remaining proc charges
		uint32 m_baseProcCharges = 0;               // Initial proc charges from the spell
		uint32 m_lastProcTime = 0;                  // Last time the aura proc'ed (for cooldown)
		bool m_procRegistered = false;              // Whether this aura has been registered for proc events
		uint32 m_procChance = 0;
	};
}
