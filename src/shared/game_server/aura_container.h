#pragma once

#include <vector>

#include "base/countdown.h"
#include "base/typedefs.h"
#include "game/aura.h"

namespace mmo
{
	namespace proto
	{
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
		explicit AuraContainer(GameUnitS &owner, uint64 casterId, uint32 spellId, GameTime duration);

		~AuraContainer();

	public:
		void AddAuraEffect(const proto::SpellEffect& effect, int32 basePoints);

		void SetApplied(bool apply);

		bool IsExpired() const;

	public:
		GameUnitS& GetOwner() const
		{
			return m_owner;
		}

		bool IsApplied() const
		{
			return m_applied;
		}

		uint64 GetCasterId() const
		{
			return m_casterId;
		}

		uint32 GetSpellId() const
		{
			return m_spellId;
		}

		GameTime GetDuration() const
		{
			return m_duration;
		}

		bool DoesExpire() const
		{
			return m_duration > 0;
		}

		int32 GetMaximumBasePoints(AuraType type) const;

		int32 GetMinimumBasePoints(AuraType type) const;

		float GetTotalMultiplier(AuraType type) const;

	private:
		void HandleModStat(const Aura& aura, bool apply);

		void HandleModResistance(const Aura& aura, bool apply);

		void HandleRunSpeedModifier(const Aura& aura, bool apply);

		void HandleSwimSpeedModifier(const Aura& aura, bool apply);

		void HandleFlySpeedModifier(const Aura& aura, bool apply);

	private:

		GameUnitS &m_owner;

		uint64 m_casterId;

		uint32 m_spellId;

		std::vector<Aura> m_auras;

		bool m_applied = false;

		GameTime m_duration;

		GameTime m_expiration;

		Countdown m_expirationCountdown;
	};

}
