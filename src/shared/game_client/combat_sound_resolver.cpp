// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combat_sound_resolver.h"

namespace mmo
{
	namespace
	{
		/// Picks the impact sound for a material out of an authored row list. An exact
		/// material match wins; the row with target_material 0 is the fallback.
		template<typename TRows>
		uint32 findImpactSound(const TRows& rows, const uint32 material)
		{
			uint32 fallback = 0;
			for (const auto& row : rows)
			{
				if (row.target_material() == 0)
				{
					fallback = row.sound();
				}
				else if (row.target_material() == material)
				{
					return row.sound();
				}
			}

			return fallback;
		}

		const proto_client::CombatSounds* findModelSounds(const proto_client::ModelDataManager& models, const uint32 displayId)
		{
			const auto* model = models.getById(displayId);
			if (!model)
			{
				return nullptr;
			}

			return &model->combat_sounds();
		}

		uint32 resolveImpact(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker, const uint32 material)
		{
			if (const auto* subclass = subclasses.getById(attacker.weaponSubclass))
			{
				if (const uint32 sound = findImpactSound(subclass->impact_sounds(), material))
				{
					return sound;
				}
			}

			if (const auto* modelSounds = findModelSounds(models, attacker.displayId))
			{
				return findImpactSound(modelSounds->impact_sounds(), material);
			}

			return 0;
		}

		/// Reads one sound id off the attacker's weapon subclass, falling back to the same
		/// slot on the attacker model's natural weapon when the weapon has none authored.
		/// The accessor is a generic lambda because it is applied to two unrelated proto
		/// message types that happen to carry identically named fields.
		template<typename TAccessor>
		uint32 resolveWeaponSound(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker, TAccessor accessor)
		{
			if (const auto* subclass = subclasses.getById(attacker.weaponSubclass))
			{
				if (const uint32 sound = accessor(*subclass))
				{
					return sound;
				}
			}

			if (const auto* modelSounds = findModelSounds(models, attacker.displayId))
			{
				return accessor(*modelSounds);
			}

			return 0;
		}
	}

	namespace combat_sounds
	{
		uint32 ResolveVictimMaterial(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundVictim& victim)
		{
			if (const auto* chest = subclasses.getById(victim.chestSubclass))
			{
				if (chest->hit_material() != 0)
				{
					return chest->hit_material();
				}
			}

			if (const auto* modelSounds = findModelSounds(models, victim.displayId))
			{
				return modelSounds->hit_material();
			}

			return 0;
		}

		uint32 ResolveSwingSound(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker)
		{
			return resolveWeaponSound(subclasses, models, attacker,
				[](const auto& source) { return source.swing_sound(); });
		}

		ResolvedSwingSounds ResolveSwingSounds(const proto_client::ItemSubclassManager& subclasses,
			const proto_client::ModelDataManager& models, const CombatSoundAttacker& attacker,
			const CombatSoundVictim& victim, const CombatSwingOutcome& outcome)
		{
			ResolvedSwingSounds resolved;

			if (outcome.landed && !outcome.fullBlock)
			{
				const uint32 material = ResolveVictimMaterial(subclasses, models, victim);
				resolved.impactSound = resolveImpact(subclasses, models, attacker, material);

				if (outcome.crit)
				{
					resolved.critLayerSound = resolveWeaponSound(subclasses, models, attacker,
						[](const auto& source) { return source.crit_layer_sound(); });
				}
			}

			if (outcome.miss)
			{
				resolved.missSound = resolveWeaponSound(subclasses, models, attacker,
					[](const auto& source) { return source.miss_sound(); });
			}

			if (outcome.parry)
			{
				// A parry is weapon on weapon, so the sound belongs to the defender's weapon.
				// Without one authored, the attacker's whiff is the next best thing.
				if (const auto* defenderWeapon = subclasses.getById(victim.mainHandSubclass))
				{
					resolved.parrySound = defenderWeapon->parry_sound();
				}

				if (resolved.parrySound == 0)
				{
					resolved.parrySound = resolveWeaponSound(subclasses, models, attacker,
						[](const auto& source) { return source.miss_sound(); });
				}
			}

			if (outcome.block)
			{
				if (const auto* shield = subclasses.getById(victim.offHandSubclass))
				{
					resolved.blockSound = shield->block_sound();
				}
			}

			return resolved;
		}

		CombatVoiceRequest ResolveAttackVoice(const proto_client::ModelDataManager& models, const uint32 displayId, const bool crit)
		{
			CombatVoiceRequest request;

			const auto* sounds = findModelSounds(models, displayId);
			if (!sounds)
			{
				return request;
			}

			if (crit && sounds->attack_crit_voice_sound() != 0)
			{
				request.sound = sounds->attack_crit_voice_sound();
				request.chance = sounds->attack_crit_voice_chance();
				return request;
			}

			request.sound = sounds->attack_voice_sound();
			request.chance = sounds->attack_voice_chance();
			return request;
		}

		CombatVoiceRequest ResolveHitVoice(const proto_client::ModelDataManager& models, const uint32 displayId, const bool crit)
		{
			CombatVoiceRequest request;

			const auto* sounds = findModelSounds(models, displayId);
			if (!sounds)
			{
				return request;
			}

			if (crit && sounds->crit_hit_voice_sound() != 0)
			{
				request.sound = sounds->crit_hit_voice_sound();
				request.chance = sounds->crit_hit_voice_chance();
				return request;
			}

			request.sound = sounds->hit_voice_sound();
			request.chance = sounds->hit_voice_chance();
			return request;
		}
	}
}
