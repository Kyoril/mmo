
#include "party_unit_handle.h"
#include "party_info.h"
#include "game_client/game_unit_c.h"

namespace mmo
{
	PartyUnitHandle::PartyUnitHandle(PartyInfo& party, int32 partyIndex)
		: m_partyInfo(party)
		, m_partyIndex(partyIndex)
	{
	}

	PartyUnitHandle::PartyUnitHandle(PartyInfo& party, GameUnitC& unit, int32 partyIndex)
		: UnitHandle(unit)
		, m_partyInfo(party)
		, m_partyIndex(partyIndex)
	{
		m_realUnit = std::static_pointer_cast<GameUnitC>(unit.shared_from_this());
	}

	PartyUnitHandle::~PartyUnitHandle()
	{
	}

	uint64 PartyUnitHandle::GetGuid() const
	{
		if (auto unit = GetRealUnit(); unit)
		{
			return unit->GetGuid();
		}

		if (auto member = GetPartyMember(); member)
		{
			return member->guid;
		}

		return 0;
	}

	int32 PartyUnitHandle::GetHealth() const
	{
		if (auto unit = GetRealUnit(); unit)
		{
			return unit->GetHealth();
		}

		if (auto member = GetPartyMember(); member)
		{
			return member->health;
		}

		return 0;
	}

	int32 PartyUnitHandle::GetMaxHealth() const
	{
		if (auto unit = GetRealUnit(); unit)
		{
			return unit->GetMaxHealth();
		}

		if (auto member = GetPartyMember(); member)
		{
			return member->maxHealth;
		}

		return 1;
	}

	int32 PartyUnitHandle::GetLevel() const
	{
		if (const auto unit = GetRealUnit(); unit)
		{
			return unit->GetMaxHealth();
		}

		if (const auto member = GetPartyMember(); member)
		{
			return member->level;
		}

		return 1;
	}

	int32 PartyUnitHandle::GetPower(int32 powerType) const
	{
		if (const auto unit = GetRealUnit(); unit)
		{
			return unit->GetPower(powerType);
		}

		if (const auto member = GetPartyMember(); member)
		{
			return member->power;
		}

		return 1;
	}

	int32 PartyUnitHandle::GetMaxPower(int32 powerType) const
	{
		if (const auto unit = GetRealUnit(); unit)
		{
			return unit->GetMaxPower(powerType);
		}

		if (const auto member = GetPartyMember(); member)
		{
			return member->maxPower;
		}

		return 1;
	}

	const std::string& PartyUnitHandle::GetName() const
	{
		if (const auto unit = GetRealUnit(); unit)
		{
			return unit->GetName();
		}

		if (const auto* member = GetPartyMember(); member)
		{
			return member->name;
		}

		static const String s_unknown = "UNKNOWN";
		return s_unknown;
	}

	int32 PartyUnitHandle::GetPowerType() const
	{
		if (const auto unit = GetRealUnit(); unit)
		{
			return unit->GetPowerType();
		}

		if (const auto member = GetPartyMember(); member)
		{
			return member->powerType;
		}

		return 1;
	}

	bool PartyUnitHandle::IsAlive() const
	{
		return GetHealth() > 0;
	}

	uint32 PartyUnitHandle::GetAuraCount() const
	{
		if (const auto unit = GetRealUnit(); unit)
		{
			return unit->GetAuraCount();
		}

		return 0;
	}

	bool PartyUnitHandle::IsFriendly() const
	{
		// TODO: We assume party members are always friendly right now but this must not be true later on (dueling, mind control and so on!)
		return true;
	}

	bool PartyUnitHandle::IsHostile() const
	{
		// TODO: We assume party members are always friendly right now but this must not be true later on (dueling, mind control and so on!)
		return false;
	}

	std::shared_ptr<GameUnitC> PartyUnitHandle::GetRealUnit() const
	{
		auto unit = m_realUnit.lock();
		return unit;
	}

	const PartyMember* PartyUnitHandle::GetPartyMember() const
	{
		return m_partyInfo.GetMember(m_partyIndex);
	}
}
