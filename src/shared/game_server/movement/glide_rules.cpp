// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glide_rules.h"

#include "game_protocol/game_protocol.h"
#include "shared/proto_data/area_triggers.pb.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	GlideStartDecision CanStartGlide(const MovementInfo& current, const bool knowsGlider, const float vigor, const bool incapacitated)
	{
		if (!current.IsFalling())
		{
			return GlideStartDecision::NotFalling;
		}

		if (current.IsGliding())
		{
			return GlideStartDecision::AlreadyGliding;
		}

		if (current.IsSwimming())
		{
			return GlideStartDecision::Swimming;
		}

		if (!knowsGlider)
		{
			return GlideStartDecision::NoGlider;
		}

		if (vigor <= 0.0f)
		{
			return GlideStartDecision::NoVigor;
		}

		if (incapacitated || (current.movementFlags & movement_flags::Rooted) != 0)
		{
			return GlideStartDecision::Incapacitated;
		}

		return GlideStartDecision::Allowed;
	}

	GlideFlagChange ClassifyGlideFlagChange(const uint16 clientOpCode, const MovementInfo& previous, const MovementInfo& next)
	{
		// The glider only exists in the air: the flag is never valid without Falling.
		if (next.IsGliding() && !next.IsFalling())
		{
			return GlideFlagChange::Invalid;
		}

		const bool wasGliding = previous.IsGliding();
		const bool isGliding = next.IsGliding();

		switch (clientOpCode)
		{
		case game::client_realm_packet::MoveStartGlide:
			return !wasGliding && isGliding ? GlideFlagChange::Start : GlideFlagChange::Invalid;

		case game::client_realm_packet::MoveStopGlide:
			if (isGliding)
			{
				return GlideFlagChange::Invalid;
			}
			return wasGliding ? GlideFlagChange::Stop : GlideFlagChange::Unchanged;

		case game::client_realm_packet::MoveFallLand:
		case game::client_realm_packet::MoveStartSwim:
		case game::client_realm_packet::MoveEnded:
			if (wasGliding && !isGliding)
			{
				return GlideFlagChange::ImplicitStop;
			}
			break;

		default:
			break;
		}

		return wasGliding == isGliding ? GlideFlagChange::Unchanged : GlideFlagChange::Invalid;
	}

	float GetGlideRiseCarry(const float upwardSpeed, const TraversalSettings& settings)
	{
		if (upwardSpeed <= 0.0f || settings.glideVerticalAccel <= 0.0f)
		{
			return 0.0f;
		}

		return upwardSpeed * upwardSpeed / (2.0f * settings.glideVerticalAccel);
	}

	bool IsGlideSpeedValid(const Vector3& from, const Vector3& to, const GameTime dt, const float updraftStrength, const TraversalSettings& settings, const float carriedRise)
	{
		const float seconds = static_cast<float>(dt) / 1000.0f;

		if (dt > 0)
		{
			const float dx = to.x - from.x;
			const float dz = to.z - from.z;
			const float horizontal = std::sqrt(dx * dx + dz * dz);
			if (horizontal > settings.glideForwardSpeed * glide_rules::SpeedTolerance * seconds)
			{
				return false;
			}
		}

		// Sinking is never limited here: the glider may have been opened in a fast fall.
		float allowedRise = glide_rules::RiseToleranceMeters + std::max(0.0f, carriedRise);
		if (updraftStrength > 0.0f)
		{
			allowedRise += updraftStrength * glide_rules::SpeedTolerance * seconds;
		}

		return to.y - from.y <= allowedRise;
	}

	bool IsWithinEmptyGrace(const GameTime now, const GameTime stopTime)
	{
		return now <= stopTime + glide_rules::StopGraceMs;
	}

	float ComputeFallDamagePercent(const float startY, const float landY, const float minHeight, const float lethalHeight)
	{
		const float fallDistance = startY - landY;
		if (fallDistance <= minHeight)
		{
			return 0.0f;
		}

		const float damageRange = lethalHeight - minHeight;
		if (damageRange <= 0.0f)
		{
			return 0.0f;
		}

		return std::min(1.0f, (fallDistance - minHeight) / damageRange);
	}

	std::vector<UpdraftVolume> BuildUpdraftVolumes(const proto::AreaTriggers& triggers, const uint32 mapId)
	{
		std::vector<UpdraftVolume> volumes;

		for (const auto& trigger : triggers.entry())
		{
			if (trigger.map() != mapId || !trigger.has_updraft_strength() || trigger.updraft_strength() <= 0.0f)
			{
				continue;
			}

			UpdraftVolume volume;
			volume.center = Vector3(trigger.x(), trigger.y(), trigger.z());
			volume.strength = trigger.updraft_strength();

			// Same shape rules as the area-trigger containment test (Player::IsPointInTrigger): a
			// radius wins, box_x/y/z are the full size of the box.
			if (trigger.has_radius())
			{
				if (trigger.radius() <= 0.0f)
				{
					continue;
				}

				volume.radius = trigger.radius();
			}
			else if (trigger.has_box_x() && trigger.has_box_y() && trigger.has_box_z())
			{
				volume.halfExtents = Vector3(trigger.box_x() * 0.5f, trigger.box_y() * 0.5f, trigger.box_z() * 0.5f);
				volume.orientation = trigger.has_box_o() ? trigger.box_o() : 0.0f;
			}
			else
			{
				continue;
			}

			volumes.push_back(volume);
		}

		return volumes;
	}

	void GlideMoveValidator::Begin(const Vector3& position, const GameTime time, const float upwardSpeed, const TraversalSettings& settings)
	{
		m_settings = settings;
		m_lastPosition = position;
		m_lastTime = time;
		m_carriedRise = GetGlideRiseCarry(std::min(upwardSpeed, settings.defaultUpdraftStrength), settings);
		m_active = true;
	}

	GlideMoveValidator::Result GlideMoveValidator::Check(const Vector3& position, const GameTime time, const std::vector<UpdraftVolume>& updrafts)
	{
		if (!m_active || time < m_lastTime + glide_rules::MinCheckIntervalMs)
		{
			return Result::Skipped;
		}

		const GameTime dt = time - m_lastTime;

		// Lift counts if either end of the interval is inside an updraft: the glider may have
		// entered or left it anywhere in between.
		const float updraft = std::max(GetUpdraftStrength(updrafts, m_lastPosition), GetUpdraftStrength(updrafts, position));
		if (updraft > 0.0f)
		{
			// Inside an updraft the glider may pick up its full upward speed again.
			m_carriedRise = GetGlideRiseCarry(updraft, m_settings);
		}

		const bool valid = IsGlideSpeedValid(m_lastPosition, position, dt, updraft, m_settings, m_carriedRise);

		// Outside of an updraft, every bit of height gained uses up carried upward speed.
		if (updraft <= 0.0f)
		{
			const float rise = position.y - m_lastPosition.y;
			m_carriedRise = std::max(0.0f, m_carriedRise - std::max(0.0f, rise));
		}

		m_lastPosition = position;
		m_lastTime = time;

		return valid ? Result::Ok : Result::Violation;
	}
}
