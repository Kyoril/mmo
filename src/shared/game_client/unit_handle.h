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
		[[nodiscard]] virtual int32 GetHealth() const;
		[[nodiscard]] virtual int32 GetMaxHealth() const;
		[[nodiscard]] virtual int32 GetLevel() const;
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
		[[nodiscard]] virtual bool IsAlive() const;

	private:
		[[nodiscard]] bool CheckNonNull() const;
	};
}