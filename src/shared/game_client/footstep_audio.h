// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "audio/audio.h"

namespace mmo
{
	/// Footsteps retain their surface-specific mix, at 70% of the authored volume.
	constexpr float FootstepVolumeScale = 0.7f;

	/// The local listener sits on the player, so a short offset in the direction of
	/// travel gives their footsteps a direction without changing other players' sources.
	/// Vertical motion does not change the direction of a step on the ground.
	[[nodiscard]] inline Vector3 GetFootstepSoundPosition(const Vector3& position, const Vector3& velocity, const bool localPlayer)
	{
		if (!localPlayer)
		{
			return position;
		}

		Vector3 direction(velocity.x, 0.0f, velocity.z);
		if (direction.GetSquaredLength() < 0.0001f)
		{
			return position;
		}

		return position + direction.NormalizedCopy() * 0.75f;
	}

	/// Preserves the footstep's surface mix at a lower volume, and attaches local
	/// steps to the listener so moving past the original source cannot reverse the pan.
	inline void ConfigureFootstepChannel(IAudio& audio, const ChannelIndex channel, const Vector3& listenerOffset, const bool localPlayer)
	{
		if (channel == InvalidChannel)
		{
			return;
		}

		if (IChannelInstance* instance = audio.GetChannelInstance(channel))
		{
			instance->SetVolume(instance->GetVolume() * FootstepVolumeScale);
		}

		if (localPlayer)
		{
			audio.Set3DListenerRelativePosition(channel, listenerOffset);
		}
	}
}
