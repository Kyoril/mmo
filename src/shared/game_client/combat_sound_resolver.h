// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "client_data/project.h"

namespace mmo
{
	/// @brief Identity of the swinging unit, as far as combat audio is concerned.
	struct CombatSoundAttacker
	{
		/// Item subclass id of the weapon in the swinging hand. 0 = unarmed or creature.
		uint32 weaponSubclass = 0;

		/// Display model id, used for natural weapon sounds and the combat voice.
		uint32 displayId = 0;
	};

	/// @brief Identity of the unit being swung at, as far as combat audio is concerned.
	struct CombatSoundVictim
	{
		/// Item subclass id of the victim's main hand weapon, which owns the parry sound.
		uint32 mainHandSubclass = 0;

		/// Item subclass id of the victim's off hand item, which owns the block sound.
		uint32 offHandSubclass = 0;

		/// Item subclass id of the victim's chest armor, which owns the hit material.
		uint32 chestSubclass = 0;

		/// Display model id, used for the body material and the pain react voice.
		uint32 displayId = 0;
	};

	/// @brief What happened to a single auto attack swing.
	struct CombatSwingOutcome
	{
		/// The swing connected. True for a fully absorbed hit as well, which deals no damage
		/// but should still sound like metal meeting armor.
		bool landed = false;

		bool crit = false;

		/// Missed outright or was dodged. Both whiff.
		bool miss = false;

		bool parry = false;

		bool block = false;

		/// The block stopped everything, so no impact sound plays.
		bool fullBlock = false;
	};

	/// @brief SoundEntry ids resolved for one swing. 0 means play nothing for that slot.
	struct ResolvedSwingSounds
	{
		uint32 impactSound = 0;
		uint32 critLayerSound = 0;
		uint32 missSound = 0;
		uint32 parrySound = 0;
		uint32 blockSound = 0;
	};

	/// @brief A voice line candidate together with the chance that it plays at all.
	struct CombatVoiceRequest
	{
		/// SoundEntry id, 0 = nothing to play.
		uint32 sound = 0;

		/// Chance in percent that the line plays when rolled.
		uint32 chance = 0;
	};

	namespace combat_sounds
	{
		/// @brief Resolves what the victim's body sounds like when struck.
		///	Worn chest armor wins over the model's own material.
		/// @return SurfaceType id, or 0 when neither defines one.
		uint32 ResolveVictimMaterial(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundVictim& victim);

		/// @brief Resolves the swing whoosh of the attacker's weapon, falling back to the
		///	attacker model's natural weapon.
		/// @return SoundEntry id, or 0.
		uint32 ResolveSwingSound(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker);

		/// @brief Resolves every sound the given swing outcome calls for.
		ResolvedSwingSounds ResolveSwingSounds(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker,
			const CombatSoundVictim& victim, const CombatSwingOutcome& outcome);

		/// @brief Resolves the attacker's effort voice for a swing.
		CombatVoiceRequest ResolveAttackVoice(const proto_client::ModelDataManager& models, uint32 displayId, bool crit);

		/// @brief Resolves the victim's pain react voice for a landed hit.
		CombatVoiceRequest ResolveHitVoice(const proto_client::ModelDataManager& models, uint32 displayId, bool crit);
	}
}
