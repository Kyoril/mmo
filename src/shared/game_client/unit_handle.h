#pragma once

#include "base/weak_handle.h"
#include "base/typedefs.h"

namespace mmo
{
	namespace proto_client
	{
		class SpellEntry;
	}

	class GameUnitC;
	class GameAuraC;

	namespace proficiency_type
	{
		enum Type
		{
			Weapon = 0,
			Armor = 1
		};
	}

	class AuraHandle final : public WeakHandle<GameAuraC>
	{
	public:
		explicit AuraHandle(GameAuraC& aura);
		explicit AuraHandle();
		~AuraHandle() override = default;

	public:
		[[nodiscard]] bool CanExpire() const;

		[[nodiscard]] bool IsExpired() const;

		[[nodiscard]] uint32 GetDuration() const;

		[[nodiscard]] const proto_client::SpellEntry* GetSpell() const;

		[[nodiscard]] bool IsNegative() const;

		[[nodiscard]] uint8 GetStackCount() const;

		/// Gets the GUID of the unit that cast the spell creating this aura.
		[[nodiscard]] uint64 GetCasterId() const;

	private:
		[[nodiscard]] bool CheckNonNull() const;
	};

	class UnitHandle : public WeakHandle<GameUnitC>
	{
	public:
		explicit UnitHandle(GameUnitC& unit);
		explicit UnitHandle();
		virtual ~UnitHandle() override = default;

	public:
		[[nodiscard]] virtual uint64 GetGuid() const;
		[[nodiscard]] virtual int32 GetHealth() const;
		[[nodiscard]] virtual int32 GetMaxHealth() const;
		[[nodiscard]] virtual int32 GetLevel() const;
		[[nodiscard]] virtual const char* GetClass() const;

		/// Number of classes the player has learned (multi-class system). 0 for non-players.
		[[nodiscard]] virtual uint32 GetKnownClassCount() const;

		/// Localized/display name of the known class at the given index, or nullptr if out of range.
		[[nodiscard]] virtual const char* GetKnownClassName(uint32 index) const;

		/// Per-class level of the known class at the given index, or 0 if out of range.
		[[nodiscard]] virtual int32 GetKnownClassLevel(uint32 index) const;

		/// True if the known class at the given index is the character's currently active class.
		[[nodiscard]] virtual bool IsKnownClassActive(uint32 index) const;

		/// Gets the unit's guild name when the unit is a player.
		/// @return The guild name, an empty string for a guildless player, or nullptr for a non-player.
		[[nodiscard]] virtual const char* GetGuildName() const;

		[[nodiscard]] virtual int32 GetPower(int32 powerType) const;
		[[nodiscard]] virtual int32 GetMaxPower(int32 powerType) const;
		[[nodiscard]] virtual uint32 GetAuraCount() const;
		[[nodiscard]] virtual std::shared_ptr<AuraHandle> GetAura(uint32 index) const;
		[[nodiscard]] virtual const std::string& GetName() const;
		[[nodiscard]] virtual int32 GetPowerType() const;
		[[nodiscard]] virtual float GetMinDamage() const;
		[[nodiscard]] virtual float GetMaxDamage() const;
		[[nodiscard]] virtual uint32 GetAttackTime() const;
		[[nodiscard]] virtual float GetAttackPower() const;
		[[nodiscard]] virtual int32 GetStat(int32 statId) const;
		[[nodiscard]] virtual int32 GetPosStat(int32 statId) const;
		[[nodiscard]] virtual int32 GetNegStat(int32 statId) const;
		[[nodiscard]] virtual int32 GetArmor() const;
		[[nodiscard]] virtual float GetArmorReductionFactor() const;
		[[nodiscard]] virtual int32 GetAvailableAttributePoints() const;
		[[nodiscard]] virtual int32 GetTalentPoints() const;
		[[nodiscard]] virtual bool IsAlive() const;
		[[nodiscard]] virtual bool IsFriendly() const;
		[[nodiscard]] virtual bool IsHostile() const;
		[[nodiscard]] virtual const char* GetType() const;

		[[nodiscard]] virtual int32 GetHealthFromStat(int32 statId) const;
		[[nodiscard]] virtual int32 GetManaFromStat(int32 statId) const;
		[[nodiscard]] virtual int32 GetAttackPowerFromStat(int32 statId) const;
		[[nodiscard]] virtual uint8 GetAttributeCost(uint32 attribute) const;

		/**
		 * @brief Checks if the unit has a specific proficiency.
		 * @param proficiency The proficiency ID to check for.
		 * @return True if the unit has the proficiency, false otherwise.
		 */
		[[nodiscard]] virtual bool HasProficiency(uint32 proficiency) const;

		[[nodiscard]] bool operator==(const UnitHandle & other) const;

	private:
		[[nodiscard]] bool CheckNonNull() const;
	};
}
