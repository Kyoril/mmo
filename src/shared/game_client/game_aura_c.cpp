#include "game_aura_c.h"

#include "game_unit_c.h"
#include "base/clock.h"
#include "shared/client_data/proto_client/spells.pb.h"

namespace mmo
{
	GameAuraC::GameAuraC(GameUnitC& owner, const proto_client::SpellEntry& spell, const uint64 caster, const GameTime expiration)
		: m_spell(&spell)
		, m_expiration(0)
		, m_casterId(caster)
		, m_targetId(owner.GetGuid())
	{
		if (expiration > 0)
		{
			m_expiration = GetAsyncTimeMs() + expiration;
		}

		m_onOwnerRemoved = owner.removed.connect([this]() 
		{ 
			removed(); 
		});
	}

	GameAuraC::~GameAuraC()
	{
		removed();
	}

	bool GameAuraC::IsExpired() const
	{
		if (!CanExpire())
		{
			return false;
		}

		return GetAsyncTimeMs() >= m_expiration;
	}
}
