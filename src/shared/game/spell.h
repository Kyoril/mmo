#pragma once

#include "base/typedefs.h"

#include <map>

namespace mmo
{
	namespace spell_facing_flags
	{
		enum Type
		{
			None = 0,

			TargetInFront = 1 << 0,

			BehindTarget = 1 << 1,
		};
	}

	namespace power_type
	{
		enum Type
		{
			/// The most common one, mobs actually have this or rage.
			Mana,

			/// This is what warriors use to cast their spells.
			Rage,

			/// Used by rogues to do their spells.
			Energy,

			///
			Health,

			Count_,
			Invalid_ = Count_
		};
	}

	typedef power_type::Type PowerType;

	namespace spell_mechanic
	{
		enum Type
		{
			None = 0,

			Charm,
			Disorient,
			Disarm,
			Distract,
			Fear,
			Root,
			Silence,
			Sleep,
			Snare,
			Stun,
			Freeze,
			Knockout,
			Bleed,
			Polymorph,
			Banish,
			Shield,
			Mount,
			Daze,

			Count_
		};
	}

	typedef spell_mechanic::Type SpellMechanic;

	namespace spell_power_bonus_type
	{
		enum Type
		{
			None = 0,

			SpellDamage = 1,

			AttackPower = 2
		};
	}

	typedef spell_power_bonus_type::Type SpellPowerBonusType;

	namespace spell_cast_target_flags
	{
		enum Type
		{
			Self = 0x00000000,

			Unit = 0x00000002,

			Item = 0x00000010,

			SourceLocation = 0x00000020,

			DestLocation = 0x00000040,

			Object = 0x00000800,

			TradeItem = 0x0000100,

			String = 0x0000200,

			Corpse = 0x0000400
		};
	}

	typedef spell_cast_target_flags::Type SpellCastTargetFlags;

	namespace spell_miss_info
	{
		enum Type
		{
			None = 0,
			Miss = 1,
			Resist = 2,
			Dodge = 3,
			Parry = 4,
			Block = 5,
			Evade = 6,
			Immune = 7,
			Deflect = 8,
			Absorb = 9,
			Reflect = 10
		};
	}

	typedef spell_miss_info::Type SpellMissInfo;

	namespace spell_dmg_class
	{
		enum Type
		{
			None = 0,
			Magic = 1,
			Melee = 2,
			Ranged = 3
		};
	}

	typedef spell_dmg_class::Type SpellDmgClass;

	namespace spell_school
	{
		enum Type
		{
			Normal = 0,
			Holy = 1,
			Fire = 2,
			Nature = 3,
			Frost = 4,
			Shadow = 5,
			Arcane = 6,
			End = 7
		};
	}

	typedef spell_school::Type SpellSchool;

	namespace spell_mod_op
	{
		enum Type
		{
			/// Spell damage modified.
			Damage,

			/// Spell aura duration modified.
			Duration,

			/// Spell threat modified.
			Threat,

			/// Spell charges modified.
			Charges,

			/// Spell range modified.
			Range,

			/// Spell radius modified.
			Radius,

			/// Spell critical hit chance modified.
			CritChance,

			/// All effect base points modified.
			AllEffects,

			/// Amount of spell delay on hit while casting modified.
			PreventSpellDelay,

			/// Spell cast time modified (also cast time for channeled spells)
			CastTime,

			/// Spell cooldown modified.
			Cooldown,

			/// Spell cost modified.
			Cost,

			/// Critical spell damage modified.
			CritDamageBonus,

			/// Chance to miss or resist this spell modified.
			ResistMissChance,

			/// Increases the proc chance
			ChanceOfSuccess,

			ActivationTime,

			/// Global cooldown modified.
			GlobalCooldown,

			/// Bonus damage modified (? we already have Damage...).
			BonusDamage,

			PeriodicBasePoints,

			/// Resist dispel chance modified.
			ResistDispelChance,

			/// Since this is a bitmask, this marks the maximum number of spellmods so far
			Count_
		};
	}

	typedef spell_mod_op::Type SpellModOp;

	namespace spell_cast_result
	{
		enum Type
		{
			FailedAffectingCombat,
			FailedAlreadyAtFullHealth,
			FailedAlreadyAtFullMana,
			FailedAlreadyAtFullPower,
			FailedAlreadyBeingTamed,
			FailedAlreadyHaveCharm,
			FailedAlreadyHaveSummon,
			FailedAlreadyOpen,
			FailedAuraBounced,
			FailedAutotrackInterrupted,
			FailedBadImplicitTargets,
			FailedBadTargets,
			FailedCantBeCharmed,
			FailedCantBeDisenchanted,
			FailedCantBeDisenchantedSkill,
			FailedCantBeProspected,
			FailedCantCastOnTapped,
			FailedCantDuelWhileInvisible,
			FailedCantDuelWhileStealthed,
			FailedCantStealth,
			FailedCasterAurastate,
			FailedCasterDead,
			FailedCharmed,
			FailedChestInUse,
			FailedConfused,
			FailedDontReport,
			FailedEquippedItem,
			FailedEquippedItemClass,
			FailedEquippedItemClassMainhand,
			FailedEquippedItemClassOffhand,
			FailedError,
			FailedFizzle,
			FailedFleeing,
			FailedFoodLowLevel,
			FailedHighLevel,
			FailedHungerSatiated,
			FailedImmune,
			FailedInterrupted,
			FailedInterruptedCombat,
			FailedItemAlreadyEnchanted,
			FailedItemGone,
			FailedItemNotFound,
			FailedItemNotReady,
			FailedLevelRequirement,
			FailedLineOfSight,
			FailedLowLevel,
			FailedLowCastLevel,
			FailedMainhandEmpty,
			FailedMoving,
			FailedNeedAmmo,
			FailedNeedAmmoPouch,
			FailedNeedExoticAmmo,
			FailedNoPath,
			FailedNotBehind,
			FailedNotFishable,
			FailedNotFlying,
			FailedNotHere,
			FailedNotInfront,
			FailedNotInControl,
			FailedNotKnown,
			FailedNotMounted,
			FailedNotOnTaxi,
			FailedNotOnTransport,
			FailedNotReady,
			FailedNotShapeshift,
			FailedNotStanding,
			FailedNotTradeable,
			FailedNotTrading,
			FailedNotUnsheathed,
			FailedNotWhileGhost,
			FailedNoAmmo,
			FailedNoChargesRemain,
			FailedNoChampion,
			FailedNoComboPoints,
			FailedNoDueling,
			FailedNoEndurance,
			FailedNoFish,
			FailedNoItemsWhileShapeshifted,
			FailedNoMountsAllowed,
			FailedNoPet,
			FailedNoPower,
			FailedNothingToDispel,
			FailedNothingToSteal,
			FailedOnlyAboveWater,
			FailedOnlyDaytime,
			FailedOnlyIndoors,
			FailedOnlyMounted,
			FailedOnlyNighttime,
			FailedOnlyOutdoors,
			FailedOnlyShapeshift,
			FailedOnlyStealthed,
			FailedOnlyUnderwater,
			FailedOutOfRange,
			FailedPacified,
			FailedPossessed,
			FailedReagents,
			FailedRequiresArea,
			FailedRequiresSpellFocus,
			FailedRooted,
			FailedSilenced,
			FailedSpellInProgress,
			FailedSpellLearned,
			FailedSpellUnavailable,
			FailedStunned,
			FailedTargetsDead,
			FailedTargetAffectingCombat,
			FailedTargetAuraState,
			FailedTargetDueling,
			FailedTargetEnemy,
			FailedTargetEnraged,
			FailedTargetFriendly,
			FailedTargetInCombat,
			FailedTargetIsPlayer,
			FailedTargetIsPlayerControlled,
			FailedTargetNotDead,
			FailedTargetNotInParty,
			FailedTargetNotLooted,
			FailedTargetNotPlayer,
			FailedTargetNoPockets,
			FailedTargetNoWeapons,
			FailedTargetUnskinnable,
			FailedThirstSatiated,
			FailedTooClose,
			FailedTooManyOfItem,
			FailedTotemCategory,
			FailedTotems,
			FailedTrainingPoints,
			FailedTryAgain,
			FailedUnitNotBehind,
			FailedUnitNotInfront,
			FailedWrongPetFood,
			FailedNotWhileFatigued,
			FailedTargetNotInInstance,
			FailedNotWhileTrading,
			FailedTargetNotInRaid,
			FailedDisenchantWhileLooting,
			FailedProspectWhileLooting,
			FailedProspectNeedMore,
			FailedTargetFreeForAll,
			FailedNoEdibleCorpses,
			FailedOnlyBattlegrounds,
			FailedTargetNotGhost,
			FailedTooManySkills,
			FailedTransformUnusable,
			FailedWrongWeather,
			FailedDamageImmune,
			FailedPreventedByMechanic,
			FailedPlayTime,
			FailedReputation,
			FailedMinSkill,
			FailedNotInArena,
			FailedNotOnShapeshift,
			FailedNotOnStealthed,
			FailedNotOnDamageImmune,
			FailedNotOnMounted,
			FailedTooShallow,
			FailedTargetNotInSanctuary,
			FailedTargetIsTrivial,
			FailedBMOrInvisGod,
			FailedExpertRidingRequirement,
			FailedArtisanRidingRequirement,
			FailedNotIdle,
			FailedNotInactive,
			FailedPartialPlaytime,
			FailedNoPlaytime,
			FailedNotInBattleground,
			FailedOnlyInArena,
			FailedTargetLockedToRaidInstance,

			/// Custom value used if no error occurred (will not be sent to the client)
			CastOkay = 0xFF
		};
	}

	typedef spell_cast_result::Type SpellCastResult;

	namespace spell_attributes
	{
		enum Type
		{
			Channeled = 0x00000001,

			/// Spell requires ammo.
			Ranged = 0x00000002,

			/// Spell is executed on next weapon swing.
			OnNextSwing = 0x00000004,

			/// Allows the aura to be applied by multiple casters on the same target.
			OnlyOneStackTotal = 0x00000008,

			/// Spell is a player ability.
			Ability = 0x00000010,

			///
			TradeSpell = 0x00000020,

			/// Spell is a passive spell-
			Passive = 0x00000040,

			/// Spell does not appear in the players spell book.
			HiddenClientSide = 0x00000080,

			/// Spell won't display cast time.
			HiddenCastTime = 0x00000100,

			/// Client will automatically target the mainhand item.
			TargetMainhandItem = 0x00000200,

			/// Spell can be cast on dead units. If this is not set, spells can't be cast on dead units.
			CanTargetDead = 0x00000400,

			/// Starts the first tick immediately on application.
			StartPeriodicAtApply = 0x00000800,

			/// Spell is only executable at day.
			DaytimeOnly = 0x00001000,

			/// Spell is only executable at night
			NightOnly = 0x00002000,

			/// Spell is only executable while indoor.
			IndoorOnly = 0x00004000,

			/// Spell is only executable while outdoor.
			OutdoorOnly = 0x00008000,

			/// Spell is only executable while not shape shifted.
			NotShapeshifted = 0x00010000,

			/// Spell is only executable while in stealth mode.
			OnlyStealthed = 0x00020000,

			/// Spell does not change the players sheath state.
			DontAffectSheathState = 0x00040000,

			///
			LevelDamageCalc = 0x00080000,

			/// Spell will stop auto attack.
			StopAttackTarget = 0x00100000,

			/// Spell can't be blocked / dodged / parried
			NoDefense = 0x00200000,

			/// Executer will always look at target while casting this spell.
			CastTrackTarget = 0x00400000,

			/// Spell is usable while caster is dead.
			CastableWhileDead = 0x00800000,

			/// Spell is usable while caster is mounted.
			CastableWhileMounted = 0x01000000,

			///
			DisabledWhileActive = 0x02000000,

			///
			Negative = 0x04000000,

			/// Cast is usable while caster is sitting.
			CastableWhileSitting = 0x08000000,

			/// Cast is not usable while caster is in combat.
			NotInCombat = 0x10000000,

			/// Spell is usable even on invulnerable targets.
			IgnoreInvulnerability = 0x20000000,

			/// Aura of this spell will break on damage.
			BreakableByDamage = 0x40000000,

			/// Aura can't be cancelled by player.
			CantCancel = 0x80000000
		};
	}

	typedef spell_attributes::Type SpellAttributes;

	namespace spell_attributes_b
	{
		enum Type
		{
			MeleeCombatStart = 0x00000001,

			HiddenAura = 0x00000002,
		};
	}

	typedef spell_attributes::Type SpellAttributes;

	namespace spell_proc_flags
	{
		enum Type
		{
			/// No proc.
			None = 0x00000000,

			/// Killed by aggressor.
			Killed = 0x00000001,

			/// Killed a target.
			Kill = 0x00000002,

			/// Done melee attack.
			DoneMeleeAutoAttack = 0x00000004,

			/// Taken melee attack.
			TakenMeleeAutoAttack = 0x00000008,

			///
			DoneSpellMeleeDmgClass = 0x00000010,

			///
			TakenSpellMeleeDmgClass = 0x00000020,

			/// Done ranged auto attack.
			DoneRangedAutoAttack = 0x00000040,

			/// Taken ranged auto attack.
			TakenRangedAutoAttack = 0x00000080,

			///
			DoneSpellRangedDmgClass = 0x00000100,

			///
			TakenSpellRangedDmgClass = 0x00000200,

			///
			DoneSpellNoneDmgClassPos = 0x00000400,

			///
			TakenSpellNoneDmgClassPos = 0x00000800,

			///
			DoneSpellNoneDmgClassNeg = 0x00001000,

			///
			TakenSpellNoneDmgClassNeg = 0x00002000,

			///
			DoneSpellMagicDmgClassPos = 0x00004000,

			///
			TakenSpellMagicDmgClassPos = 0x00008000,

			///
			DoneSpellMagicDmgClassNeg = 0x00010000,

			///
			TakenSpellMagicDmgClassNeg = 0x00020000,

			/// On periodic tick done.
			DonePeriodicDamage = 0x00040000,

			/// On periodic tick received.
			TakenPeriodicDamage = 0x00080000,

			/// On any damage taken.
			TakenDamage = 0x00100000,

			/// On trap activation.
			DoneTrapActivation = 0x00200000,

			/// Done main hand attack.
			DoneMainhandAttack = 0x00400000,

			/// Done off hand attack.
			DoneOffhandAttack = 0x00800000,

			/// Died in any way.
			Death = 0x01000000,

			DonePeriodicHeal = 0x02000000,

			TakenPeriodicHeal = 0x04000000,
		};
	}

	typedef spell_proc_flags::Type SpellProcFlags;

	namespace spell_proc_flags_ex
	{
		enum Type
		{
			None = 0x0000000,
			NormalHit = 0x0000001,
			CriticalHit = 0x0000002,
			Miss = 0x0000004,
			Resist = 0x0000008,
			Dodge = 0x0000010,
			Parry = 0x0000020,
			Block = 0x0000040,
			Evade = 0x0000080,
			Immune = 0x0000100,
			Deflect = 0x0000200,
			Absorb = 0x0000400,
			Reflect = 0x0000800,
			Interrupt = 0x0001000,
			TriggerAlways = 0x0002000,
			TriggerOnce = 0x0004000,
			InternalHot = 0x0008000,
			InternalDot = 0x0010000,
		};
	}

	typedef spell_proc_flags_ex::Type SpellProcFlagsEx;

	namespace victim_state
	{
		enum Type
		{
			Unknown1 = 0,
			Normal = 1,
			Dodge = 2,
			Parry = 3,
			Interrupt = 4,
			Blocks = 5,
			Evades = 6,
			IsImmune = 7,
			Deflects = 8
		};
	}

	typedef victim_state::Type VictimState;

	namespace hit_info
	{
		enum Type
		{
			NormalSwing = 0x00000000,
			LeftSwing = 0x00000001,
			Miss = 0x00000002,
			Absorb = 0x00000004,
			Resist = 0x00000008,
			CriticalHit = 0x00000010,
			Glancing = 0x00000020,
			Crushing = 0x00000040,
			NoAction = 0x00000080,
			Block = 0x00000100,
		};
	}

	typedef hit_info::Type HitInfo;

	struct HitResult final
	{
		uint32 procAttacker;
		uint32 procVictim;
		uint32 procEx;
		uint32 amount;

		explicit HitResult(const uint32 attackerProc, const uint32 victimProc, const HitInfo& hitInfo, const VictimState& victimState, const float resisted = 0.0f, const uint32 damage = 0, const uint32 absorbed = 0, const bool isDamage = false)
			: procAttacker(attackerProc)
			, procVictim(victimProc)
			, procEx(spell_proc_flags_ex::None)
			, amount(0)
		{
			Add(hitInfo, victimState, resisted, damage, absorbed, isDamage);
		}

		void Add(const HitInfo& hitInfo, const VictimState& victimState, const float resisted = 0.0f, const uint32 damage = 0, const uint32 absorbed = 0, const bool isDamage = false)
		{
			amount += damage - absorbed;
			amount += damage * (resisted / 100.0f);

			switch (hitInfo)
			{
			case hit_info::Miss:
				procEx |= spell_proc_flags_ex::Miss;
				break;
			case hit_info::CriticalHit:
				procEx |= spell_proc_flags_ex::CriticalHit;
				break;
			default:
				procEx |= spell_proc_flags_ex::NormalHit;
				break;
			}

			switch (victimState)
			{
			case victim_state::Dodge:
				procEx |= spell_proc_flags_ex::Dodge;
				break;
			case victim_state::Parry:
				procEx |= spell_proc_flags_ex::Parry;
				break;
			case victim_state::Interrupt:
				procEx |= spell_proc_flags_ex::Interrupt;
				break;
			case victim_state::Blocks:
				procEx |= spell_proc_flags_ex::Block;
				break;
			case victim_state::Evades:
				procEx |= spell_proc_flags_ex::Evade;
				break;
			case victim_state::IsImmune:
				procEx |= spell_proc_flags_ex::Immune;
				break;
			case victim_state::Deflects:
				procEx |= spell_proc_flags_ex::Deflect;
				break;
			default:
				break;
			}

			if (resisted == 100.0f)
			{
				procEx |= spell_proc_flags_ex::Resist;
				procEx &= ~spell_proc_flags_ex::NormalHit;
			}
			else if (damage && absorbed)
			{
				if (absorbed == damage)
				{
					procEx &= ~spell_proc_flags_ex::NormalHit;
				}
				procEx |= spell_proc_flags_ex::Absorb;
			}
			else if (amount && isDamage)
			{
				procVictim |= spell_proc_flags::TakenDamage;
			}
		}
	};

	typedef std::map<uint64, HitResult> HitResultMap;

	namespace spell_effects
	{
		enum Type
		{
			None,
			InstantKill = 1,
			SchoolDamage,
			Dummy,
			PortalTeleport,
			TeleportUnits,
			ApplyAura,
			EnvironmentalDamage,
			PowerDrain,
			HealthLeech,
			Heal,
			Bind,
			Portal,
			QuestComplete,
			WeaponDamageNoSchool,
			Resurrect,
			AddExtraAttacks,
			Dodge,
			Evade,
			Parry,
			Block,
			CreateItem,
			Weapon,
			Defense,
			PersistentAreaAura,
			Summon,
			Leap,
			Energize,
			WeaponPercentDamage,
			TriggerMissile,
			OpenLock,
			LearnSpell,
			SpellDefense,
			Dispel,
			Language,
			DualWield,
			TeleportUnitsFaceCaster,
			SkillStep,
			Spawn,
			TradeSkill,
			Stealth,
			Detect,
			TameCreature,
			SummonPet,
			LearnPetSpell,
			WeaponDamage,

			ResetAttributePoints,

			HealPct,

			Charge,

			ApplyAreaAura,
			InterruptSpellCast,

			Count_,
			Invalid_ = 0
		};
	}

	typedef spell_effects::Type SpellEffect;
}
