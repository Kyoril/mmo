// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	namespace spell_aura_interrupt_flags
	{
		enum Type
		{
			None = 0x0000000,

			/// Removed when getting hit by a negative spell.
			HitBySpell = 0x00000001,

			/// Removed by any damage.
			Damage = 0x00000002,

			/// Removed on crowd control effect.
			CrowdControl = 0x00000004,

			/// Removed by any movement.
			Move = 0x00000008,

			/// Removed by any turning.
			Turning = 0x00000010,

			/// Removed by entering combat.
			EnterCombat = 0x00000020,

			/// Removed by unmounting.
			NotMounted = 0x00000040,

			/// Removed by entering water (start swimming).
			NotAboveWater = 0x00000080,

			/// Removed by leaving water.
			NotUnderWater = 0x00000100,

			/// Removed by unsheathing.
			NotSheathed = 0x00000200,

			/// Removed when talking to an npc or loot a creature.
			Talk = 0x00000400,

			/// Removed when mining/using/opening/interact with game object.
			Use = 0x00000800,

			/// Removed by attacking.
			Attack = 0x00001000,

			Cast = 0x00002000,

			/// Removed on transformation.
			Transform = 0x00008000,

			/// Removed when mounting.
			Mount = 0x00020000,

			/// Removed when standing up.
			NotSeated = 0x00040000,

			/// Removed when leaving the map.
			ChangeMap = 0x00080000,

			Unattackable = 0x00100000,

			/// Removed when teleported.
			Teleported = 0x00400000,

			/// Removed by entering pvp combat.
			EnterPvPCombat = 0x00800000,

			/// Removed by any direct damage.
			DirectDamage = 0x01000000,

			/// TODO
			NotVictim = (HitBySpell | Damage | DirectDamage)
		};
	}

	typedef spell_aura_interrupt_flags::Type SpellAuraInterruptFlags;

	namespace aura_type
	{
		enum Type
		{
			None,

			Dummy,
			PeriodicHeal,
			ModAttackSpeed,
			ModDamageDone,
			ModDamageTaken,
			ModHealth,
			ModMana,
			ModResistance,
			PeriodicTriggerSpell,
			PeriodicEnergize,
			ModStat,
			ProcTriggerSpell,
			PeriodicDamage,
			ModIncreaseSpeed,
			ModDecreaseSpeed,
			ModSpeedAlways,
			ModSpeedNonStacking,

			AddFlatModifier,
			AddPctModifier,

			ModHealingDone,
			ModAttackPower,

			ModHealingTaken,

			ModDamageDonePct,
			ModDamageTakenPct,

			ModRoot,
			ModSleep,
			ModStun,
			ModFear,

			ModVisibility,

			ModResistancePct,

			Count_,
			Invalid_ = Count_
		};
	}

	typedef aura_type::Type AuraType;
}
