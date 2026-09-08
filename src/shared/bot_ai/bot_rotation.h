// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cstddef>
#include <vector>

namespace mmo
{
	class BotContext;

	namespace proto
	{
		class Project;
		class SpellEntry;
	}

	/// One offensive spell the bot knows, reduced to the handful of numbers the decision needs.
	///
	/// Resolved once when the spell book changes rather than looked up per tick: the proto lookup
	/// plus the range table indirection is not free, and a bot asks this question several times a
	/// second for the rest of its life.
	struct BotRotationSpell final
	{
		uint32 spellId { 0 };
		float minRange { 0.0f };

		/// Zero means the spell has no range limit of its own.
		float maxRange { 0.0f };

		/// Flat power cost, and the percentage-of-maximum cost, exactly as the spell declares
		/// them. Both are applied: a spell may carry either or both.
		uint32 cost { 0 };
		uint32 costPercent { 0 };

		uint32 castTimeMs { 0 };

		/// Rough expected damage, used only to order the rotation. Comparing spells is all this
		/// has to do, so the estimate does not have to match what the server rolls.
		float estimatedDamage { 0.0f };
	};

	/// The offensive spells a bot knows, in the order it should try them.
	///
	/// Derived entirely from the proto data rather than authored per class. The bot process
	/// already has the full spell table, so "what can this character do to that creature" is a
	/// question about data we hold - and a hand-written rotation per class would have to be
	/// rewritten every time the spells change, which is exactly the kind of drift a bot swarm
	/// exists to catch rather than to suffer from.
	class BotRotation final
	{
	public:
		/// Re-derives the rotation from the spells the bot currently knows.
		/// @return Number of usable offensive spells found.
		std::size_t Rebuild(const proto::Project& project, const BotContext& world);

		/// Picks the best spell that is off cooldown, affordable and in range of a target at the
		/// given distance, or nullptr when nothing is usable and the bot should fall back to
		/// swinging its weapon.
		[[nodiscard]] const BotRotationSpell* SelectSpellEntry(const BotContext& world, float targetDistance) const;

		/// Id of the spell SelectSpellEntry would pick, or 0.
		[[nodiscard]] uint32 SelectSpell(const BotContext& world, float targetDistance) const;

		[[nodiscard]] const std::vector<BotRotationSpell>& GetSpells() const { return m_spells; }
		[[nodiscard]] bool IsEmpty() const { return m_spells.empty(); }

		/// Number of known spells the rotation was built from. The cheapest way to notice that
		/// the spell book changed and the rotation is stale.
		[[nodiscard]] std::size_t GetSourceSpellCount() const { return m_sourceSpellCount; }

	private:
		std::vector<BotRotationSpell> m_spells;
		std::size_t m_sourceSpellCount { 0 };
	};

	/// Whether this spell is something a bot would cast at an enemy: it damages, it is not
	/// passive, and it is not a beneficial effect aimed at a friend.
	[[nodiscard]] bool IsOffensiveBotSpell(const proto::SpellEntry& spell);

	/// Rough expected damage of a spell, for ordering only.
	[[nodiscard]] float EstimateBotSpellDamage(const proto::SpellEntry& spell);
}
