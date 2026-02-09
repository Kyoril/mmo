// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cooldown_manager.h"
#include "base/clock.h"
#include "frame_ui/frame_mgr.h"
#include "game/spell.h"

namespace mmo
{
	CooldownManager::CooldownManager(const proto_client::SpellManager& spells)
		: m_spells(spells)
	{
	}

	void CooldownManager::OnEnterWorld()
	{
		m_cooldowns.clear();
		m_globalCooldown.reset();
	}

	void CooldownManager::OnLeftWorld()
	{
		m_cooldowns.clear();
		m_globalCooldown.reset();
	}

	void CooldownManager::StartCooldown(const uint32 spellId, const GameTime durationMs)
	{
		if (durationMs == 0)
		{
			ClearCooldown(spellId);
			return;
		}

		const CooldownInfo info{ GetAsyncTimeMs(), durationMs };
		if (UsesGlobalCooldown(spellId))
		{
			m_globalCooldown = info;
		}
		else
		{
			m_cooldowns[spellId] = info;
		}

		// Fire signal
		CooldownStarted(spellId, durationMs);

		// Trigger Lua event
		FrameManager::Get().TriggerLuaEvent("SPELL_COOLDOWN_STARTED", spellId, durationMs);
	}

	void CooldownManager::ClearCooldown(const uint32 spellId)
	{
		bool wasCleared = false;
		if (UsesGlobalCooldown(spellId))
		{
			wasCleared = m_globalCooldown.has_value();
			m_globalCooldown.reset();
		}
		else
		{
			wasCleared = m_cooldowns.erase(spellId) > 0;
		}

		if (wasCleared)
		{
			// Fire signal
			CooldownEnded(spellId);

			// Trigger Lua event
			FrameManager::Get().TriggerLuaEvent("SPELL_COOLDOWN_ENDED", spellId);
		}
	}

	float CooldownManager::GetCooldownProgress(const uint32 spellId) const
	{
		if (UsesGlobalCooldown(spellId))
		{
			if (!m_globalCooldown.has_value())
			{
				return 1.0f;
			}

			return GetCooldownProgress(*m_globalCooldown);
		}

		const auto it = m_cooldowns.find(spellId);
		if (it == m_cooldowns.end())
		{
			return 1.0f;
		}

		return GetCooldownProgress(it->second);
	}

	float CooldownManager::GetCooldownRemaining(const uint32 spellId) const
	{
		if (UsesGlobalCooldown(spellId))
		{
			if (!m_globalCooldown.has_value())
			{
				return 0.0f;
			}

			return GetCooldownRemaining(*m_globalCooldown);
		}

		const auto it = m_cooldowns.find(spellId);
		if (it == m_cooldowns.end())
		{
			return 0.0f;
		}

		return GetCooldownRemaining(it->second);
	}

	bool CooldownManager::IsOnCooldown(const uint32 spellId) const
	{
		return GetCooldownProgress(spellId) < 1.0f;
	}

	void CooldownManager::Update(float deltaSeconds)
	{
		const GameTime now = GetAsyncTimeMs();

		// Check for expired cooldowns
		std::vector<uint32> expiredCooldowns;

		for (const auto& [spellId, info] : m_cooldowns)
		{
			if (IsCooldownExpired(info, now))
			{
				expiredCooldowns.push_back(spellId);
			}
		}

		// Clear expired cooldowns
		for (const uint32 spellId : expiredCooldowns)
		{
			ClearCooldown(spellId);
		}

		if (m_globalCooldown.has_value() && IsCooldownExpired(*m_globalCooldown, now))
		{
			m_globalCooldown.reset();
			CooldownEnded(0);
			FrameManager::Get().TriggerLuaEvent("SPELL_COOLDOWN_ENDED", 0);
		}
	}

	bool CooldownManager::UsesGlobalCooldown(const uint32 spellId) const
	{
		const auto* spell = m_spells.getById(spellId);
		if (!spell)
		{
			return false;
		}

		return (spell->cooldownflags() & spell_cooldown_flags::UseGlobalCooldown) != 0;
	}

	float CooldownManager::GetCooldownProgress(const CooldownInfo& info) const
	{
		const GameTime now = GetAsyncTimeMs();
		const GameTime elapsed = now - info.startTime;
		if (elapsed >= info.duration)
		{
			return 1.0f;
		}

		return static_cast<float>(elapsed) / static_cast<float>(info.duration);
	}

	float CooldownManager::GetCooldownRemaining(const CooldownInfo& info) const
	{
		const GameTime now = GetAsyncTimeMs();
		const GameTime elapsed = now - info.startTime;
		if (elapsed >= info.duration)
		{
			return 0.0f;
		}

		return static_cast<float>(info.duration - elapsed) / 1000.0f;
	}

	bool CooldownManager::IsCooldownExpired(const CooldownInfo& info, const GameTime now) const
	{
		return now - info.startTime >= info.duration;
	}
}
