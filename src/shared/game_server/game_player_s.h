#pragma once

#include <set>

#include "game_unit_s.h"
#include "inventory.h"

namespace mmo
{
	class GameItemS;

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

		void SetAttributeCost(uint32 attribute, uint8 cost);

		uint8 GetAttributeCost(uint32 attribute) const;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Player; }

		/// Applies or removes item stats for this character.
		void ApplyItemStats(GameItemS& item, bool apply);

		/// Gets a reference to the characters inventory component.
		Inventory& GetInventory() { return m_inventory; }

		const proto::ClassEntry* GetClassEntry() const { return m_classEntry; }

		const proto::RaceEntry* GetRaceEntry() const { return m_raceEntry; }

		bool AddAttributePoint(uint32 attribute);

		void ResetAttributePoints();

		static uint8 CalculateAttributeCost(uint32 pointsSpent);

	protected:

		float GetUnitMissChance() const override;

		bool HasOffhandWeapon() const override;

	public:
		void RewardExperience(const uint32 xp);

		void RefreshStats() override;

		virtual void SetLevel(uint32 newLevel) override;

		void UpdateDamage();

		void UpdateArmor();

		void UpdateAttributePoints();

		uint32 GetAttributePointsByAttribute(const uint32 attribute) const { ASSERT(attribute < 5); return m_attributePointEnhancements[attribute]; }

	protected:
		void UpdateStat(int32 stat);

		void RecalculateTotalAttributePointsConsumed(const uint32 attribute);

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
		Inventory m_inventory;
		const proto::ClassEntry* m_classEntry;
		const proto::RaceEntry* m_raceEntry;
		std::array<uint32, 5> m_attributePointEnhancements;
		std::array<uint32, 5> m_attributePointsSpent;
		uint32 m_totalAvailablePointsAtLevel;

	private:
		friend io::Writer& operator << (io::Writer& w, GamePlayerS const& object);
		friend io::Reader& operator >> (io::Reader& r, GamePlayerS& object);
	};


	io::Writer& operator<<(io::Writer& w, GamePlayerS const& object);

	io::Reader& operator>> (io::Reader& r, GamePlayerS& object);
}
