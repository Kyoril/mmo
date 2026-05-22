// Stub implementations for symbols in mmo_client that are referenced transitively
// by game_client (object_mgr.cpp). mmo_client is an executable target (not a lib),
// so these stubs satisfy the linker in headless movement tests.

#include "party_unit_handle.h"
#include "systems/party_info.h"

namespace mmo
{
	// ---------------------------------------------------------------------------
	// PartyUnitHandle stubs
	// ---------------------------------------------------------------------------
	PartyUnitHandle::PartyUnitHandle(PartyInfo& party, int32 partyIndex)
		: m_partyInfo(party), m_realUnit(), m_partyIndex(partyIndex)
	{
	}

	PartyUnitHandle::PartyUnitHandle(PartyInfo& party, GameUnitC& /*unit*/, int32 partyIndex)
		: m_partyInfo(party), m_realUnit(), m_partyIndex(partyIndex)
	{
	}

	PartyUnitHandle::~PartyUnitHandle() = default;

	uint64 PartyUnitHandle::GetGuid() const { return 0; }
	int32  PartyUnitHandle::GetHealth() const { return 0; }
	int32  PartyUnitHandle::GetMaxHealth() const { return 0; }
	int32  PartyUnitHandle::GetLevel() const { return 0; }
	int32  PartyUnitHandle::GetPower(int32 /*powerType*/) const { return 0; }
	int32  PartyUnitHandle::GetMaxPower(int32 /*powerType*/) const { return 0; }

	const std::string& PartyUnitHandle::GetName() const
	{
		static const std::string s_empty;
		return s_empty;
	}

	int32  PartyUnitHandle::GetPowerType() const { return 0; }
	bool   PartyUnitHandle::IsAlive() const { return false; }
	uint32 PartyUnitHandle::GetAuraCount() const { return 0; }
	bool   PartyUnitHandle::IsFriendly() const { return false; }
	bool   PartyUnitHandle::IsHostile() const { return false; }

	// ---------------------------------------------------------------------------
	// PartyInfo stubs
	// ---------------------------------------------------------------------------
	int32 PartyInfo::GetMemberIndexByGuid(uint64 /*memberGuid*/) const
	{
		return -1;
	}

	uint64 PartyInfo::GetMemberGuid(int32 /*index*/) const
	{
		return 0;
	}
}
