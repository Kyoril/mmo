#pragma once

#include <set>

#include "game_unit_s.h"

namespace mmo
{
	/// @brief Represents a playable character in the game world.
	class GamePlayerS final : public GameUnitS
	{
	public:

		signal<void(GameUnitS&, const proto::SpellEntry&)> spellLearned;
		signal<void(GameUnitS&, const proto::SpellEntry&)> spellUnlearned;

	public:
		GamePlayerS(const proto::Project& project, TimerQueue& timerQueue);

		~GamePlayerS() override = default;


		virtual void Initialize() override;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Player; }

	public:
		void RewardExperience(const uint32 xp);

		virtual void SetLevel(uint32 newLevel) override;

	protected:
		void OnSpellLearned(const proto::SpellEntry& spell) override
		{
			spellLearned(*this, spell);
		}

		void OnSpellUnlearned(const proto::SpellEntry& spell) override
		{
			spellUnlearned(*this, spell);
		}

	protected:
		void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::PlayerFieldCount);
		}
	};
}
