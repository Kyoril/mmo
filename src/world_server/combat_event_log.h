// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <deque>

namespace mmo
{
	/// Kinds of combat events kept for bug reports.
	namespace combat_event_type
	{
		enum Type : uint8
		{
			/// Spell or melee damage dealt by the player.
			DamageDone,
			/// Damage taken by the player.
			DamageTaken,
			/// Healing done to the player.
			HealTaken,
			/// The player started casting a spell.
			CastStarted,
			/// The player's cast finished (amount = 1 if it succeeded).
			CastFinished,
			/// An aura was applied to the player.
			AuraApplied,
			/// An aura was removed from the player.
			AuraRemoved,
		};

		inline const char* GetName(const Type type)
		{
			switch (type)
			{
			case DamageDone: return "damage_done";
			case DamageTaken: return "damage_taken";
			case HealTaken: return "heal_taken";
			case CastStarted: return "cast_started";
			case CastFinished: return "cast_finished";
			case AuraApplied: return "aura_applied";
			case AuraRemoved: return "aura_removed";
			}
			return "unknown";
		}
	}

	typedef combat_event_type::Type CombatEventType;

	/// One entry of a player's recent combat history.
	struct CombatEvent
	{
		GameTime time = 0;
		CombatEventType type = combat_event_type::DamageDone;
		/// The other party (target, attacker, caster), 0 if none or unknown.
		uint64 otherGuid = 0;
		/// Spell involved, 0 for melee and environmental events.
		uint32 spellId = 0;
		/// Damage or heal amount; for CastFinished 1 = success, 0 = failure.
		uint32 amount = 0;
		/// Damage school for damage events.
		uint32 school = 0;
	};

	/// Fixed-size ring buffer of a player's most recent combat events, always recording, so a
	/// bug report carries the history that led up to the problem.
	class CombatEventLog final
	{
	public:
		explicit CombatEventLog(const size_t capacity = 50)
			: m_capacity(capacity)
		{
		}

		void Add(const CombatEvent& event)
		{
			if (m_capacity == 0)
			{
				return;
			}

			if (m_events.size() >= m_capacity)
			{
				m_events.pop_front();
			}
			m_events.push_back(event);
		}

		/// Events, oldest first.
		[[nodiscard]] const std::deque<CombatEvent>& GetEvents() const
		{
			return m_events;
		}

		/// The most recent CastFinished event for a spell, or nullptr.
		[[nodiscard]] const CombatEvent* FindLastCastResult(const uint32 spellId) const
		{
			for (auto it = m_events.rbegin(); it != m_events.rend(); ++it)
			{
				if (it->type == combat_event_type::CastFinished && it->spellId == spellId)
				{
					return &*it;
				}
			}
			return nullptr;
		}

	private:
		size_t m_capacity;
		std::deque<CombatEvent> m_events;
	};
}
