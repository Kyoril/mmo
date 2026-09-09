// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <functional>

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "client_data/project.h"
#include "combat_sound_resolver.h"
#include "combat_voice_gate.h"
#include "math/vector3.h"

namespace mmo
{
	class SoundEntryPlayer;

	/// @brief Plays the audio of a melee auto attack: the swing whoosh and the attacker's
	///	effort voice at swing time, and the impact, crit layer, whiff, parry, block and the
	///	victim's pain react once the weapon connects.
	class CombatSoundPlayer final : public NonCopyable
	{
	public:
		/// @param project Client data the sounds are resolved from.
		/// @param soundPlayer Sound entry playback used for every sound this class plays.
		CombatSoundPlayer(const proto_client::Project& project, SoundEntryPlayer& soundPlayer);

	public:
		/// @brief Plays the swing whoosh and rolls the attacker's effort voice.
		/// @param attacker Weapon and model of the swinging unit.
		/// @param attackerGuid Guid of the swinging unit, used for the voice throttle.
		/// @param attackerPos World position the swing sound plays at.
		/// @param crit Whether this swing is going to be a critical hit.
		void PlaySwing(const CombatSoundAttacker& attacker, ObjectGuid attackerGuid, const Vector3& attackerPos, bool crit);

		/// @brief Plays everything the swing outcome calls for, and rolls the victim's
		///	pain react. Call this when the weapon visually connects.
		void PlayOutcome(const CombatSoundAttacker& attacker, const CombatSoundVictim& victim,
			const CombatSwingOutcome& outcome, ObjectGuid victimGuid, const Vector3& victimPos);

		/// @brief Drops the voice throttle state, for example on a world change.
		void Clear();

		/// @brief Overrides the chance roll source. The provider returns values in [0, 99].
		///	Tests inject a deterministic one.
		void SetRollProvider(std::function<uint32()> roll);

		/// @brief Overrides the clock. Tests inject a controllable one.
		void SetClock(std::function<GameTime()> clock);

	private:
		/// @brief Rolls the chance, checks the throttle and plays the line at the position.
		void TryPlayVoice(const CombatVoiceRequest& request, ObjectGuid unit, uint32 displayId, const Vector3& position);

		/// @brief How long the unit stays silent after playing the given sound entry.
		[[nodiscard]] GameTime GetVoiceBlockDuration(uint32 displayId, uint32 sound) const;

		/// @brief Plays a sound entry positionally, ignoring 0.
		void Play(uint32 sound, const Vector3& position) const;

	private:
		const proto_client::Project& m_project;
		SoundEntryPlayer& m_soundPlayer;
		CombatVoiceGate m_voiceGate;
		std::function<uint32()> m_roll;
		std::function<GameTime()> m_clock;
	};
}
