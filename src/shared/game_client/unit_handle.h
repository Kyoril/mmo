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

	private:
		[[nodiscard]] bool CheckNonNull() const;
	};
}