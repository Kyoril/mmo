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
		[[nodiscard]] int32 GetHealth() const;
		[[nodiscard]] int32 GetMaxHealth() const;
		[[nodiscard]] int32 GetLevel() const;
		[[nodiscard]] int32 GetPower(int32 powerType) const;
		[[nodiscard]] int32 GetMaxPower(int32 powerType) const;
		[[nodiscard]] uint32 GetAuraCount() const;
		[[nodiscard]] std::shared_ptr<AuraHandle> GetAura(uint32 index) const;
		[[nodiscard]] const std::string& GetName() const;
		[[nodiscard]] int32 GetPowerType() const;
		[[nodiscard]] float GetMinDamage() const;
		[[nodiscard]] float GetMaxDamage() const;
		[[nodiscard]] uint32 GetAttackTime() const;
		[[nodiscard]] float GetAttackPower() const;
		[[nodiscard]] int32 GetStat(int32 statId) const;
		[[nodiscard]] int32 GetPosStat(int32 statId) const;
		[[nodiscard]] int32 GetNegStat(int32 statId) const;
		[[nodiscard]] int32 GetArmor() const;
		[[nodiscard]] float GetArmorReductionFactor() const;
		[[nodiscard]] int32 GetAvailableAttributePoints() const;

	private:
		[[nodiscard]] bool CheckNonNull() const;
	};
}