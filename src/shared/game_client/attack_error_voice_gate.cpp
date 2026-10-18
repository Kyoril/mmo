// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "attack_error_voice_gate.h"

namespace mmo
{
	bool AttackErrorVoiceGate::CanPlay(const uint32 soundId, const GameTime now) const
	{
		const auto it = m_nextAllowed.find(soundId);
		return it == m_nextAllowed.end() || now >= it->second;
	}

	void AttackErrorVoiceGate::NotifyPlayed(const uint32 soundId, const GameTime now, const GameTime repeatCooldown)
	{
		m_nextAllowed[soundId] = now + repeatCooldown;
	}

	void AttackErrorVoiceGate::Clear()
	{
		m_nextAllowed.clear();
	}
}
