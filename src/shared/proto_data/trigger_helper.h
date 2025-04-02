#pragma once

namespace mmo
{
	namespace trigger_flags
	{
		enum Type
		{
			/// No trigger flags set.
			None				= 0x0000,
			/// Trigger execution is cancelled as soon as the owner dies.
			AbortOnOwnerDeath	= 0x0001,
			/// Trigger is only executed while owner is in combat (and is aborted if owner leaves combat).
			OnlyInCombat		= 0x0002,
			/// Only one trigger of this type should be running concurrently.
			OnlyOneInstance		= 0x0004,

			/// Used for automatic enumeration.
			Count_				= 2
		};
	}

	namespace trigger_event
	{
		enum Type
		{
			/// Executed when the unit is spawned.
			/// Data: NONE;
			OnSpawn,
			/// Executed when the unit will despawn.
			/// Data: NONE;
			OnDespawn,
			/// Executed when the unit enters the combat.
			/// Data: NONE;
			OnAggro,
			/// Executed when the unit was killed.
			/// Data: NONE;
			OnKilled,
			/// Executed when the unit killed another unit.
			/// Data: NONE;
			OnKill,
			/// Executed when the unit was damaged.
			/// Data: NONE;
			OnDamaged,
			/// Executed when the unit was healed.
			/// Data: NONE;
			OnHealed,
			/// Executed when the unit made an auto attack swing.
			/// Data: NONE;
			OnAttackSwing,
			/// Executed when the unit resets.
			/// Data: NONE;
			OnReset,
			/// Executed when the unit reached it's home point after reset.
			/// Data: NONE;
			OnReachedHome,
			/// Executed when a player is interacting with this object. Only works on GameObjects right now, but could also be used
			/// for npc interaction.
			/// Data: NONE;
			OnInteraction,
			/// Executed when a units health drops below a certain percentage.
			/// Data: <HEALTH_PERCENTAGE:0-100>;
			OnHealthDroppedBelow,
			/// Executed when a unit reaches it's target point for a move that was triggered by a trigger.
			/// Data: NONE;
			OnReachedTriggeredTarget,
			/// Executed when a unit was hit by a specific spell.
			/// Data: <SPELL-ID>;
			OnSpellHit,
			/// Executed when a spell aura is removed.
			/// Data: <SPELL-ID>;
			OnSpellAuraRemoved,
			/// Executed when a unit is target of a specific emote.
			/// Data: <EMOTE-ID>;
			OnEmote,
			/// Executed when a unit successfully casted a specific spell.
			/// Data: <SPELL-ID>;
			OnSpellCast,

			Invalid,
			Count_ = Invalid
		};
	}

	namespace trigger_actions
	{
		enum Type
		{
			/// Execute another trigger.
			/// Targets: NONE; Data: <TRIGGER-ID>; Texts: NONE;
			Trigger = 0,
			/// Makes a unit say a text.
			/// Targets: UNIT; Data: <SOUND-ID>,<LANGUAGE>; Texts: <TEXT>;
			Say = 1,
			/// Makes a unit say a text.
			/// Targets: UNIT; Data: <SOUND-ID>,<LANGUAGE>; Texts: <TEXT>;
			Yell = 2,
			/// Sets the state of a world object.
			/// Targets: NAMED_OBJECT; Data: <NEW-STATE>; Texts: NONE;
			SetWorldObjectState = 3,
			/// Activates or deactivates a creature or object spawner.
			/// Targets: NAMED_CREATURE/NAMED_OBJECT; Data: <0/1>; Texts: NONE;
			SetSpawnState = 4,
			/// Activates or deactivates respawn of a creature or object spawner.
			/// Targets: NAMED_CREATURE/NAMED_OBJECT; Data: <0/1>; Texts: NONE;
			SetRespawnState = 5,
			/// Casts a spell.
			/// Targets: UNIT; Data: <SPELL-ID>; Texts: NONE;
			CastSpell = 6,
			/// Delays the following actions.
			/// Targets: NONE; Data: <DELAY-TIME-MS>; Texts: NONE;
			Delay = 7,
			/// Makes a unit move towards a specified position.
			/// Targets: UNIT; Data: <X>, <Y>, <Z>; Texts: NONE;
			MoveTo = 8,
			/// Enables or disables a units combat movement.
			/// Targets: UNIT; Data: <0/1>; Texts: NONE;
			SetCombatMovement = 9,
			/// Stops auto attacking the current victim.
			/// Targets: UNIT; Data: NONE; Texts: NONE;
			StopAutoAttack = 10,
			/// Cancels the current cast (if any).
			/// Targets: UNIT; Data: NONE; Texts: NONE;
			CancelCast = 11,
			/// Updates the target units stand state.
			/// Targets: UNIT; Data: <STAND-STATE>; Texts: NONE;
			SetStandState = 12,
			/// Updates the target units virtual equipment slot.
			/// Targets: UNIT; Data: <SLOT:0-2>, <ITEM-ENTRY>; Texts: NONE;
			SetVirtualEquipmentSlot = 13,
			/// Updates the target creatures AI combat phase.
			/// Targets: UNIT; Data: <PHASE>; Texts: NONE;
			SetPhase = 14,
			/// Sets spell cooldown for a unit.
			/// Targets: UNIT; Data: <SPELL-ID>,<TIME-MS>; Texts: NONE;
			SetSpellCooldown = 15,
			/// Rewards a player character with a kill credit of a certain unit.
			/// Targets: PLAYER; Data: <CREATURE-ENTRY-ID>; Texts: NONE;
			QuestKillCredit = 16,
			/// Sets spell cooldown for a unit.
			/// Targets: PLAYER; Data: <QUEST-ID>; Texts: NONE;
			QuestEventOrExploration = 17,
			/// Sets an object variable.
			/// Targets: OBJECT; Data: <VARIABLE-ID>, [<NUMERIC-VALUE>]; Texts: [<STRING_VALUE>];
			SetVariable = 18,
			/// Dismounts a unit if mounted.
			/// Targets: UNIT; Data: NONE; Texts: NONE;
			Dismount = 19,
			/// Sets the mount display id of a unit and makes it enter the mounted state.
			/// Targets: UNIT; Data: <MOUNT-ID>; Texts: NONE;
			SetMount = 20,
			/// Despawns an object by removing it from the world.
			/// Targets: UNIT; Data: NONE; Texts: NONE;
			Despawn = 21,

			Invalid,
			Count_ = Invalid
		};
	}

	namespace trigger_action_target
	{
		enum Type
		{
			/// No target. May be invalid for some actions.
			None = 0,
			/// Unit which owns this trigger. May be invalid for some triggers.
			OwningObject = 1,
			/// Current victim of the unit which owns this trigger. May be invalid.
			OwningUnitVictim = 2,
			/// Random unit in the map instance.
			RandomUnit = 3,
			/// Named world object.
			NamedWorldObject = 4,
			/// Named creature.
			NamedCreature = 5,
			/// Unit which raised this trigger by causing an event.
			TriggeringUnit = 6,

			Invalid,
			Count_ = Invalid
		};
	}

	namespace trigger_spell_cast_target
	{
		enum Type
		{
			/// Target is the casting unit.
			Caster = 0,
			/// Target is the casting units target. Will fail if the unit does not have a current target.
			CurrentTarget = 1,
			
			Invalid,
			Count_ = Invalid
		};
	}
}
