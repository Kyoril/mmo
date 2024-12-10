#pragma once

#include <vector>

#include "spell_cast.h"
#include "base/countdown.h"
#include "base/typedefs.h"
#include "binary_io/writer.h"
#include "game/aura.h"

namespace mmo
{
	namespace proto
	{
		class SpellEntry;
		class SpellEffect;
	}

	class GameUnitS;

	struct Aura
	{
		AuraType type;
		int32 basePoints;
		GameTime tickInterval;
		const proto::SpellEffect* effect;
	};

	/// Holds and manages instances of auras for one unit.
	class AuraContainer final
	{
	public:

		/// Initializes a new AuraContainer for a specific owner unit.
		explicit AuraContainer(GameUnitS &owner, uint64 casterId, const proto::SpellEntry& spell, GameTime duration);

		~AuraContainer();

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
				(m_spell.attributes(0) & spell_attributes::HiddenClientSide) == 0;
		}

		/// Gets the maximum number of base points for a specific aura type.
		int32 GetMaximumBasePoints(AuraType type) const;

		/// Gets the minimum number of base points for a specified aura type.
		int32 GetMinimumBasePoints(AuraType type) const;

		/// Gets the total multiplier value for a specific aura type.
		float GetTotalMultiplier(AuraType type) const;

		/// Returns true if an aura container should be overwritten by this aura container.
		bool ShouldOverwriteAura(AuraContainer& other) const;

	private:
		void HandleModStat(const Aura& aura, bool apply);

		void HandleModResistance(const Aura& aura, bool apply);

		void HandleRunSpeedModifier(const Aura& aura, bool apply);

		void HandleSwimSpeedModifier(const Aura& aura, bool apply);

		void HandleFlySpeedModifier(const Aura& aura, bool apply);

	private:

		GameUnitS &m_owner;

		uint64 m_casterId;

		const proto::SpellEntry& m_spell;

		std::vector<Aura> m_auras;

		bool m_applied = false;

		GameTime m_duration;

		GameTime m_expiration;

		Countdown m_expirationCountdown;
	};

}
