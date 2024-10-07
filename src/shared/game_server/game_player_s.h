#pragma once

#include <set>

#include "game_unit_s.h"

namespace mmo
{
	namespace proto
	{
		class RaceEntry;
		class ClassEntry;
	}

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

		void SetClass(const proto::ClassEntry& classEntry);

		void SetRace(const proto::RaceEntry& raceEntry);

		void SetGender(uint8 gender);

		uint8 GetGender() const;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Player; }

	public:
		void RewardExperience(const uint32 xp);

		void RefreshStats() override;

		virtual void SetLevel(uint32 newLevel) override;

		void UpdateDamage();

	protected:
		void UpdateStat(int32 stat);
		
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

	private:
		const proto::ClassEntry* m_classEntry;
		const proto::RaceEntry* m_raceEntry;

	private:
		friend io::Writer& operator << (io::Writer& w, GamePlayerS const& object);
		friend io::Reader& operator >> (io::Reader& r, GamePlayerS& object);
	};

	io::Writer& operator<<(io::Writer& w, GamePlayerS const& object);

	io::Reader& operator>> (io::Reader& r, GamePlayerS& object);
}
