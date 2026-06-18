// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cooldown_manager.h"
#include "base/clock.h"
#include "frame_ui/frame_mgr.h"
#include "game/spell.h"

#include <algorithm>

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

		m_cooldowns[spellId] = CooldownInfo{ GetAsyncTimeMs(), durationMs };

		// Fire signal
		CooldownStarted(spellId, durationMs);

		// Trigger Lua event
		FrameManager::Get().TriggerLuaEvent("SPELL_COOLDOWN_STARTED", spellId, durationMs);
	}

	void CooldownManager::StartGlobalCooldown(const GameTime durationMs)
	{
		if (durationMs == 0)
		{
			ClearGlobalCooldown();
			return;
		}

		m_globalCooldown = CooldownInfo{ GetAsyncTimeMs(), durationMs };

		// Fire signal (spell ID 0 represents the global cooldown).
		CooldownStarted(0, durationMs);

		// Trigger Lua event
		FrameManager::Get().TriggerLuaEvent("SPELL_COOLDOWN_STARTED", 0, durationMs);
	}

	void CooldownManager::ClearCooldown(const uint32 spellId)
	{
		if (m_cooldowns.erase(spellId) > 0)
		{
			// Fire signal
			CooldownEnded(spellId);

			// Trigger Lua event
			FrameManager::Get().TriggerLuaEvent("SPELL_COOLDOWN_ENDED", spellId);
		}
	}

	void CooldownManager::ClearGlobalCooldown()
	{
		if (m_globalCooldown.has_value())
		{
			m_globalCooldown.reset();

			// Fire signal (spell ID 0 represents the global cooldown).
			CooldownEnded(0);

			// Trigger Lua event
			FrameManager::Get().TriggerLuaEvent("SPELL_COOLDOWN_ENDED", 0);
		}
	}

	float CooldownManager::GetCooldownProgress(const uint32 spellId) const
	{
		// The effective progress is the lesser of the spell's own cooldown progress and the
		// global cooldown progress (the one furthest from being ready dominates).
		float progress = 1.0f;

		if (const auto it = m_cooldowns.find(spellId); it != m_cooldowns.end())
		{
			progress = GetCooldownProgress(it->second);
		}

		if (m_globalCooldown.has_value() && IsAffectedByGlobalCooldown(spellId))
		{
			progress = std::min(progress, GetCooldownProgress(*m_globalCooldown));
		}

		return progress;
	}

	float CooldownManager::GetCooldownRemaining(const uint32 spellId) const
	{
		// The effective remaining time is the greater of the spell's own cooldown and the
		// global cooldown.
		float remaining = 0.0f;

		if (const auto it = m_cooldowns.find(spellId); it != m_cooldowns.end())
		{
			remaining = GetCooldownRemaining(it->second);
		}

		if (m_globalCooldown.has_value() && IsAffectedByGlobalCooldown(spellId))
		{
			remaining = std::max(remaining, GetCooldownRemaining(*m_globalCooldown));
		}

		return remaining;
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

	bool CooldownManager::IsAffectedByGlobalCooldown(const uint32 spellId) const
	{
		const auto* spell = m_spells.getById(spellId);
		if (!spell)
		{
			return false;
		}

		// Every spell is affected by the global cooldown unless it is explicitly flagged to opt out.
		return (spell->cooldownflags() & spell_cooldown_flags::NoGlobalCooldown) == 0;
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
