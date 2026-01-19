// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cooldown_manager.h"
#include "base/clock.h"
#include "frame_ui/frame_mgr.h"

namespace mmo
{
	void CooldownManager::OnEnterWorld()
	{
		m_cooldowns.clear();
	}

	void CooldownManager::OnLeftWorld()
	{
		m_cooldowns.clear();
	}

	void CooldownManager::StartCooldown(const uint32 spellId, const GameTime durationMs)
	{
		if (durationMs == 0)
		{
			ClearCooldown(spellId);
			return;
		}

		CooldownInfo info;
		info.startTime = GetAsyncTimeMs();
		info.duration = durationMs;

		m_cooldowns[spellId] = info;

		// Fire signal
		CooldownStarted(spellId, durationMs);

		// Trigger Lua event
		FrameManager::Get().TriggerLuaEvent("SPELL_COOLDOWN_STARTED", spellId, durationMs);
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

	float CooldownManager::GetCooldownProgress(const uint32 spellId) const
	{
		const auto it = m_cooldowns.find(spellId);
		if (it == m_cooldowns.end())
		{
			return 1.0f; // No cooldown = ready
		}

		const CooldownInfo& info = it->second;
		const GameTime now = GetAsyncTimeMs();
		const GameTime elapsed = now - info.startTime;

		if (elapsed >= info.duration)
		{
			return 1.0f; // Cooldown expired
		}

		return static_cast<float>(elapsed) / static_cast<float>(info.duration);
	}

	float CooldownManager::GetCooldownRemaining(const uint32 spellId) const
	{
		const auto it = m_cooldowns.find(spellId);
		if (it == m_cooldowns.end())
		{
			return 0.0f; // No cooldown
		}

		const CooldownInfo& info = it->second;
		const GameTime now = GetAsyncTimeMs();
		const GameTime elapsed = now - info.startTime;

		if (elapsed >= info.duration)
		{
			return 0.0f; // Cooldown expired
		}

		return static_cast<float>(info.duration - elapsed) / 1000.0f; // Convert to seconds
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
			const GameTime elapsed = now - info.startTime;
			if (elapsed >= info.duration)
			{
				expiredCooldowns.push_back(spellId);
			}
		}

		// Clear expired cooldowns
		for (const uint32 spellId : expiredCooldowns)
		{
			ClearCooldown(spellId);
		}
	}
}
