#pragma once

#include "game_client/unit_handle.h"

namespace mmo
{
	struct PartyMember;
}

namespace mmo
{
	class PartyInfo;
}

namespace mmo
{
	class PartyUnitHandle : public UnitHandle
	{
	public:
		explicit PartyUnitHandle(PartyInfo& party, int32 partyIndex);
		explicit PartyUnitHandle(PartyInfo& party, GameUnitC& unit, int32 partyIndex);
		~PartyUnitHandle() override;

	public:
		[[nodiscard]] virtual int32 GetHealth() const override;
		[[nodiscard]] virtual int32 GetMaxHealth() const override;
		[[nodiscard]] virtual int32 GetLevel() const override;
		[[nodiscard]] virtual int32 GetPower(int32 powerType) const override;
		[[nodiscard]] virtual int32 GetMaxPower(int32 powerType) const override;
		[[nodiscard]] virtual const std::string& GetName() const override;
		[[nodiscard]] virtual int32 GetPowerType() const override;
		[[nodiscard]] virtual bool IsAlive() const override;

	private:
		std::shared_ptr<GameUnitC> GetRealUnit() const;

		const PartyMember* GetPartyMember() const;

	private:
		PartyInfo& m_partyInfo;
		std::weak_ptr<GameUnitC> m_realUnit;
		int32 m_partyIndex;
	};
}
