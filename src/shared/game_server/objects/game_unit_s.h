// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>
#include <set>

#include "game_server/spells/aura_container.h"
#include "game/auto_attack.h"
#include "game_object_s.h"
#include "game_server/spells/spell_cast.h"
#include "game/spell_target_map.h"
#include "game_server/unit_mover.h"
#include "base/countdown.h"
#include "game/chat_type.h"
#include "game/damage_school.h"
#include "game/spell.h"
#include "proto_data/trigger_helper.h"
#include "shared/proto_data/triggers.pb.h"

namespace mmo
{
	/// Enumerates crowd control states of a unit, which do affect control over the unit.
	namespace unit_state
	{
		enum Type
		{
			/// Default state - no effect applied.
			Default = 0,

			/// Unit is stunned.
			Stunned = 1 << 0,

			/// Unit is confused.
			Confused = 1 << 1,

			/// Unit is rooted.
			Rooted = 1 << 2,

			/// Unit is charmed by another unit.
			Charmed = 1 << 3,

			/// Unit is feared.
			Feared = 1 << 4
		};
	}

	namespace regeneration_flags
	{
		enum Type
		{
			None = 0,

			Health = 1 << 0,
			Power = 1 << 1
		};
	}

	namespace unit_mod_type
	{
		enum Type
		{
			/// Absolute base value of this unit based on it's level.
			BaseValue = 0,
			/// Base value mulitplier (1.0 = 100%)
			BasePct = 1,
			/// Absolute total value. Final value: BaseValue * BasePct + TotalValue * TotalPct;
			TotalValue = 2,
			/// Total value multiplier.
			TotalPct = 3,

			End = 4
		};
	}

	typedef unit_mod_type::Type UnitModType;

	namespace unit_visibility
	{
		enum Type
		{
			/// The unit is absolutely invisible for everyone else except game masters. The unit can see all other non-invisible units.
			Off,

			/// Default state. Unit is simply visible.
			On,

			/// Unit is in stealth mode but can be seen by group members.
			GroupStealth,

			/// Unit is invisible but can be seen by other group members.
			GroupInvisibility
		};
	}

	typedef unit_visibility::Type UnitVisibility;

	namespace unit_mods
	{
		enum Type
		{
			/// Strength stat value modifier.
			StatStrength,

			/// Agility stat value modifier.
			StatAgility,

			/// Stamina stat value modifier.
			StatStamina,

			/// Intellect stat value modifier.
			StatIntellect,

			/// Spirit stat value modifier.
			StatSpirit,

			/// Health value modifier.
			Health,

			/// Mana power value modifier.
			Mana,

			/// Rage power value modifier.
			Rage,

			/// Energy power value modifier.
			Energy,

			/// Armor resistance value modifier.
			Armor,

			/// Holy resistance value modifier.
			ResistanceHoly,

			/// Fire resistance value modifier.
			ResistanceFire,

			/// Nature resistance value modifier.
			ResistanceNature,

			/// Frost resistance value modifier.
			ResistanceFrost,

			/// Shadow resistance value modifier.
			ResistanceShadow,

			/// Arcane resistance value modifier.
			ResistanceArcane,

			/// Melee attack power value modifier.
			AttackPower,

			/// Ranged attack power value modifier.
			AttackPowerRanged,

			/// Main hand weapon damage modifier.
			DamageMainHand,

			/// Off hand weapon damage modifier.
			DamageOffHand,

			/// Ranged weapon damage modifier.
			DamageRanged,

			/// Main hand weapon attack speed modifier.
			AttackSpeed,

			/// Ranged weapon attack speed modifier.
			AttackSpeedRanged,

			SpellDamage,

			// Healing Done
			HealingDone,

			// Healing Taken
			HealingTaken,

			End
		};
	}

	typedef unit_mods::Type UnitMods;

	namespace spell_mod_type
	{
		enum Type
		{
			/// Equals aura_type::AddFlatModifier
			Flat,

			/// Equals aura_type::AddPctModifier
			Pct
		};
	}

	typedef spell_mod_type::Type SpellModType;

	// Add procex (extra proc flags) namespace for tracking proc details
	namespace proc_ex_flags
	{
		enum Type
		{
			None             = 0x0000,      // No flags
			
			// Types of damage
			NormalHit        = 0x0001,      // Normal hit
			CriticalHit      = 0x0002,      // Critical hit
			MissHit          = 0x0004,      // Miss
			Absorb           = 0x0008,      // Absorbed damage
			Resist           = 0x0010,      // Resisted damage
			Dodge            = 0x0020,      // Dodged attack
			Parry            = 0x0040,      // Parried attack
			Block            = 0x0080,      // Blocked attack
			Evade            = 0x0100,      // Evaded attack
			Immune           = 0x0200,      // Immune to damage
			Reflect          = 0x0400,      // Reflected damage
			Interrupt        = 0x0800,      // Interrupt cast
			
			// Result masks
			SuccessHit       = NormalHit | CriticalHit,
			AvoidAttack      = MissHit | Dodge | Parry | Evade,
			ReduceDamage     = Absorb | Resist | Block
		};
	}

	typedef proc_ex_flags::Type ProcExFlags;

	/// Represents a spell modifier which is used to modify spells for a GameCharacter.
	/// This is only(?) used by talents, and is thus only available for characters.
	struct SpellModifier final
	{
		/// The spell modifier operation (what should be changed?)
		SpellModOp op;

		/// The modifier type (flag or percentage)
		SpellModType type;

		/// Charge count of this modifier (some are like "Increases damage of the next N casts")
		int16 charges;

		/// The modifier value.
		int32 value;

		/// Mask to determine which spells are modified.
		uint64 mask;

		/// Affected spell index.
		uint32 spellId;

		/// Index of the affected spell index.
		uint8 effectId;
	};

	/// Contains a list of spell modifiers of a unit.
	typedef std::list<SpellModifier> SpellModList;

	/// Stores spell modifiers by it's operation.
	typedef std::map<SpellModOp, SpellModList> SpellModsByOp;

	namespace proto
	{
		class FactionTemplateEntry;
		class SpellEntry;
	}

	class UnitStats final
	{
	public:
		static uint32 DeriveFromBaseWithFactor(const uint32 statValue, const uint32 baseValue, const uint32 factor);

		static uint32 GetMaxHealthFromStamina(const uint32 stamina);

		static uint32 GetMaxManaFromIntellect(const uint32 intellect);
	};

	class NetUnitWatcherS
	{
	public:
		virtual ~NetUnitWatcherS() = default;

	public:
		virtual void OnTeleport(uint32 mapId, const Vector3& position, const Radian& facing) = 0;

		virtual void OnAttackSwingEvent(AttackSwingEvent error) = 0;

		virtual void OnXpLog(uint32 amount) = 0;

		virtual void OnSpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags, const proto::SpellEntry& spell) = 0;

		virtual void OnNonSpellDamageLog(uint64 targetGuid, uint32 amount, DamageFlags flags) = 0;

		virtual void OnSpeedChangeApplied(MovementType type, float speed, uint32 ackId) = 0;

		virtual void OnRootChanged(bool applied, uint32 ackId) = 0;

		virtual void OnLevelUp(uint32 newLevel, int32 healthDiff, int32 manaDiff, int32 staminaDiff, int32 strengthDiff, int32 agilityDiff, int32 intDiff, int32 spiritDiff, int32 talentPoints, int32 attributePoints) = 0;

		virtual void OnSpellModChanged(SpellModType type, uint8 effectIndex, SpellModOp op, int32 value) = 0;
	};

	/// Enumerates possible movement changes which need to be acknowledged by the client.
	enum class MovementChangeType
	{
		/// Default value. Do not use!
		Invalid,

		/// Character has been rooted or unrooted.
		Root,
		/// Character can or can no longer walk on water.
		WaterWalk,
		/// Character is hovering or no longer hovering.
		Hover,
		/// Character can or can no longer fly.
		CanFly,
		/// Character has or has no longer slow fall
		FeatherFall,

		/// Walk speed changed
		SpeedChangeWalk,
		/// Run speed changed
		SpeedChangeRun,
		/// Run back speed changed
		SpeedChangeRunBack,
		/// Swim speed changed
		SpeedChangeSwim,
		/// Swim back speed changed
		SpeedChangeSwimBack,
		/// Turn rate changed
		SpeedChangeTurnRate,
		/// Flight speed changed
		SpeedChangeFlightSpeed,
		/// Flight back speed changed
		SpeedChangeFlightBackSpeed,

		/// Character teleported
		Teleport,
		/// Character was knocked back
		KnockBack
	};

	/// Bundles informations which are only used for knock back acks.
	struct KnockBackInfo
	{
		float vcos = 0.0f;
		float vsin = 0.0f;
		/// 2d speed value
		float speedXY = 0.0f;
		/// z axis speed value
		float speedZ = 0.0f;
	};

	struct TeleportInfo
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float o = 0.0f;
	};

	/// Contains infos about a pending movement change which first needs to
	/// be acknowledged by the client before it's effects can be applied.
	struct PendingMovementChange final
	{
		/// A counter which is used to verify that the ackknowledged change
		/// is for the expected pending change.
		uint32 counter;
		/// Defines what kind of change should be applied.
		MovementChangeType changeType;
		/// A timestamp value used for timeouts.
		GameTime timestamp;

		// Additional data to perform checks whether the ack packet data is correct
		// and hasn't been modified at the client side.
		union
		{
			/// The new value which should be applied. Currently only used for speed changes.
			float speed;

			/// Whether the respective movement flags should be applied or misapplied. This is
			/// only used for Hover / FeatherFall etc., and ignored for speed changes.
			bool apply;

			KnockBackInfo knockBackInfo;

			TeleportInfo teleportInfo;
		};

		PendingMovementChange();
	};

	namespace combat_capabilities
	{
		enum Type
		{
			None = 0,

			/// The unit can block attacks.
			CanBlock = 0x1,

			/// The unit can parry attacks.
			CanParry = 0x2,

			/// The unit can dodge attacks.
			CanDodge = 0x4
		};
	}

	/// Represents a living object (unit) in the game world.
	/// Units can move, cast spells, fight, and interact with other objects.
	class GameUnitS : public GameObjectS
	{
	public:
		/// Signal fired when this unit is killed.
		/// @param killer The unit that killed this unit (may be nullptr).
		signal<void(GameUnitS*)> killed;
		/// Signal fired when this unit is threatened by another unit.
		/// @param threatSource The unit that is threatening this unit.
		/// @param threatAmount The amount of threat generated.
		signal<void(GameUnitS&, float)> threatened;
		/// Signal fired when this unit takes damage.
		/// @param attacker The unit that caused the damage (may be nullptr).
		/// @param school The damage school type.
		/// @param damageType The type of damage taken.
		signal<void(GameUnitS*, uint32, DamageType)> takenDamage;
		/// Signal fired when this unit deals damage to another unit.
		/// @param victim The unit that received damage from this unit.
		/// @param school The damage school type.
		/// @param damageType The type of damage dealt.
		signal<void(GameUnitS&, uint32, DamageType)> doneDamage;
		/// Signal fired when this unit begins casting a spell.
		/// @param spell The spell entry being cast.
		signal<void(const proto::SpellEntry&)> startedCasting;
		/// Signal fired when a unit trigger should be executed.
		/// @param trigger The trigger entry to be executed.
		/// @param unit The unit that is affected by the trigger.
		/// @param source The unit that caused the trigger (may be nullptr).
		signal<void(const proto::TriggerEntry&, GameUnitS&, GameUnitS*)> unitTrigger;
		/// Signal fired when this unit completes a melee attack.
		/// @param victim The unit that was attacked.
		signal<void(GameUnitS&)> meleeAttackDone;

	public:
		/// Constructs a new unit object.
		/// @param project The project containing game configuration.
		/// @param timers The timer queue for scheduling timed events.
		GameUnitS(const proto::Project& project, TimerQueue& timers);

		/// Destructor. Removes all auras before destroying the unit.
		virtual ~GameUnitS() override;

		/// Initializes the unit's state with default values.
		virtual void Initialize() override;

		/// Sets a timer after which the unit will be despawned.
		/// @param despawnDelay Time in milliseconds after which the unit will be despawned.
		void TriggerDespawnTimer(GameTime despawnDelay);

		/// Writes object update information to the provided writer for network transmission.
		/// @param writer The writer to write the update information to.
		/// @param creation Whether this is an update for object creation.
		virtual void WriteObjectUpdateBlock(io::Writer& writer, bool creation = true) const override;

		/// Writes value update information to the provided writer for network transmission.
		/// @param writer The writer to write the update information to.
		/// @param creation Whether this is an update for object creation.
		virtual void WriteValueUpdateBlock(io::Writer& writer, bool creation = true) const override;

		/// Determines if this unit has movement information.
		/// @returns Always true for units, as they can move.
		virtual bool HasMovementInfo() const override { return true; }

		/// Refreshes the unit's stats after a change in attributes or equipment.
		virtual void RefreshStats();

		/// Sets the network unit watcher for this unit.
		/// @param watcher Pointer to the network unit watcher.
		void SetNetUnitWatcher(NetUnitWatcherS* watcher) { m_netUnitWatcher = watcher; }

		/// Gets the current position of the unit.
		/// @returns The position vector of the unit.
		const Vector3& GetPosition() const noexcept override;

		/// Gets the specified unit modifier value.
		/// @param mod The unit modifier to retrieve.
		/// @param type The type of the modifier value to retrieve.
		/// @returns The modifier value as a float.
		float GetModifierValue(UnitMods mod, UnitModType type) const;

		/// Gets the calculated unit modifier value after applying all modifications.
		/// @param mod The unit modifier to calculate.
		/// @returns The calculated modifier value as a float.
		float GetCalculatedModifierValue(UnitMods mod) const;

		/// Sets the unit modifier value to the given value.
		/// @param mod The unit modifier to set.
		/// @param type The type of modifier value to set.
		/// @param value The new value to set.
		void SetModifierValue(UnitMods mod, UnitModType type, float value);

		/// Modifies the given unit modifier value by adding or subtracting it from the current value.
		/// @param mod The unit modifier to update.
		/// @param type The type of modifier value to update.
		/// @param amount The amount to add or subtract.
		/// @param apply If true, adds the amount; if false, subtracts it.
		void UpdateModifierValue(UnitMods mod, UnitModType type, float amount, bool apply);

		/// Gets the next pending movement change and removes it from the queue of pending movement changes.
		/// @returns The pending movement change that was removed from the queue.
		PendingMovementChange PopPendingMovementChange();

		/// Adds a new pending movement change to the queue.
		/// @param change The movement change to add to the queue.
		void PushPendingMovementChange(PendingMovementChange change);

		/// Checks if there are any pending movement changes in the queue.
		/// @returns true if there are pending movement changes, false otherwise.
		bool HasPendingMovementChange() const { return !m_pendingMoveChanges.empty(); }

		/// Determines whether there is a timed out pending movement change.
		/// @returns true if there is a timed out movement change, false otherwise.
		bool HasTimedOutPendingMovementChange() const;

		/// Checks if this unit can be interacted with by the specified unit.
		/// @param interactor The unit attempting to interact with this unit.
		/// @returns true if interaction is possible, false otherwise.
		virtual bool IsInteractable(const GameUnitS& interactor) const override;

		/// Gets the maximum distance at which this unit can be interacted with.
		/// @returns The interaction distance as a float.
		virtual float GetInteractionDistance() const;

		///
		/// Raises a trigger event for this unit.
		/// @param e The type of trigger event.
		/// @param triggeringUnit The unit that triggered the event (may be nullptr).
		virtual void RaiseTrigger(trigger_event::Type e, GameUnitS* triggeringUnit = nullptr);

		///
		/// Raises a trigger event with additional data for this unit.
		/// @param e The type of trigger event.
		/// @param data Additional data for the trigger event.
		/// @param triggeringUnit The unit that triggered the event (may be nullptr).
		virtual void RaiseTrigger(trigger_event::Type e, const std::vector<uint32>& data, GameUnitS* triggeringUnit = nullptr);

		/// Gets the current power type of this unit (mana, rage, energy, etc.).
		/// @returns The power type as a uint32.
		uint32 GetPowerType() const { return Get<uint32>(object_fields::PowerType); }

		/// Gets the current amount of power for the current power type.
		/// @returns The amount of power as a uint32.
		uint32 GetPower() const { return Get<uint32>(object_fields::Mana + GetPowerType()); }

		/// Gets the maximum amount of power for the current power type.
		/// @returns The maximum amount of power as a uint32.
		uint32 GetMaxPower() const { return Get<uint32>(object_fields::MaxMana + GetPowerType()); }

	public:
		/// Sets the level of the unit.
		/// @param newLevel The new level to set.
		virtual void SetLevel(uint32 newLevel);

		/// Relocates the unit to a new position and facing direction.
		/// @param position The new position vector.
		/// @param facing The new facing direction.
		virtual void Relocate(const Vector3& position, const Radian& facing) override;

		/// Applies movement information to the unit.
		/// @param info The movement information to apply.
		virtual void ApplyMovementInfo(const MovementInfo& info) override;

		/// Determines if this unit is a game master.
		/// @returns true if the unit is a game master, false otherwise.
		virtual bool IsGameMaster() const { return false;}

		bool CanBeSeenBy(const GameUnitS& other) const;

	public:
		/// Gets the power type associated with a unit modifier.
		/// @param mod The unit modifier.
		/// @returns The power type as a PowerType enum.
		static PowerType GetPowerTypeByUnitMod(UnitMods mod);

		/// Gets the unit modifier associated with a stat.
		/// @param stat The stat index.
		/// @returns The unit modifier as a UnitMods enum.
		static UnitMods GetUnitModByStat(uint8 stat);

		/// Gets the unit modifier associated with a power type.
		/// @param power The power type.
		/// @returns The unit modifier as a UnitMods enum.
		static UnitMods GetUnitModByPower(PowerType power);

		/// Checks if a spell has a cooldown.
		/// @param spellId The ID of the spell.
		/// @param spellCategory The category of the spell.
		/// @returns true if the spell has a cooldown, false otherwise.
		bool SpellHasCooldown(uint32 spellId, uint32 spellCategory) const;

		/// Checks if the unit has a specific spell.
		/// @param spellId The ID of the spell.
		/// @returns true if the unit has the spell, false otherwise.
		bool HasSpell(uint32 spellId) const;

		/// Sets the initial spells for the unit.
		/// @param spellIds A vector of spell IDs to set.
		void SetInitialSpells(const std::vector<uint32>& spellIds);

		/// Adds a spell to the unit.
		/// @param spellId The ID of the spell to add.
		void AddSpell(uint32 spellId);

		/// Removes a spell from the unit.
		/// @param spellId The ID of the spell to remove.
		void RemoveSpell(uint32 spellId);

		/// Gets the set of spells known by the unit.
		/// @returns A set of pointers to spell entries.
		const std::set<const proto::SpellEntry*>& GetSpells() const;

		/// Sets the cooldown for a spell.
		/// @param spellId The ID of the spell.
		/// @param cooldownTimeMs The cooldown time in milliseconds.
		void SetCooldown(uint32 spellId, GameTime cooldownTimeMs);

		/// Sets the cooldown for a spell category.
		/// @param spellCategory The category of the spell.
		/// @param cooldownTimeMs The cooldown time in milliseconds.
		void SetSpellCategoryCooldown(uint32 spellCategory, GameTime cooldownTimeMs);

		/// Casts a spell.
		/// @param target The target map for the spell.
		/// @param spell The spell entry to cast.
		/// @param castTimeMs The cast time in milliseconds.
		/// @param isProc Whether the spell is a proc.
		/// @param itemGuid The GUID of the item used to cast the spell (optional).
		/// @returns The result of the spell cast.
		SpellCastResult CastSpell(const SpellTargetMap& target, const proto::SpellEntry& spell, uint32 castTimeMs, bool isProc = false, uint64 itemGuid = 0);

		/// Cancels the current spell cast.
		/// @param reason The reason for the interruption.
		/// @param interruptCooldown The cooldown time for the interruption (optional).
		void CancelCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) const;

		/// Deals damage to the unit.
		/// @param damage The amount of damage to deal.
		/// @param school The damage school type.
		/// @param instigator The unit that caused the damage.
		/// @param damageType The type of damage.
		void Damage(uint32 damage, uint32 school, GameUnitS* instigator, DamageType damageType);

		/// Heals the unit.
		/// @param amount The amount of healing to apply.
		/// @param instigator The unit that caused the healing.
		/// @returns The actual amount of healing applied.
		int32 Heal(uint32 amount, GameUnitS* instigator);

		/// Logs spell damage dealt by the unit.
		/// @param targetGuid The GUID of the target unit.
		/// @param amount The amount of damage dealt.
		/// @param school The damage school type.
		/// @param flags The damage flags.
		/// @param spell The spell entry used to deal the damage.
		void SpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags, const proto::SpellEntry& spell);

		/// Kills the unit.
		/// @param killer The unit that killed this unit.
		void Kill(GameUnitS* killer);

		/// Gets the current health of the unit.
		/// @returns The current health as a uint32.
		uint32 GetHealth() const { return Get<uint32>(object_fields::Health); }

		/// Gets the maximum health of the unit.
		/// @returns The maximum health as a uint32.
		uint32 GetMaxHealth() const { return Get<uint32>(object_fields::MaxHealth); }

		/// Checks if the unit is alive.
		/// @returns true if the unit is alive, false otherwise.
		bool IsAlive() const { return GetHealth() > 0; }

		/// Starts the regeneration countdown.
		void StartRegeneration() const;

		/// Stops the regeneration countdown.
		void StopRegeneration() const;

		/// Applies an aura to the unit.
		/// @param aura The aura container to apply.
		void ApplyAura(std::shared_ptr<AuraContainer>&& aura);

		/// Removes all auras applied by a specific item.
		/// @param itemGuid The GUID of the item.
		void RemoveAllAurasDueToItem(uint64 itemGuid);

		/// Removes all auras applied by a specific caster.
		/// @param casterGuid The GUID of the caster.
		void RemoveAllAurasFromCaster(uint64 casterGuid);

		/// Removes a specific aura from the unit.
		/// @param aura The aura container to remove.
		void RemoveAura(const std::shared_ptr<AuraContainer>& aura);

		/// Checks if the unit has an aura from a specific caster.
		/// @param spellId The ID of the spell.
		/// @param casterId The GUID of the caster.
		/// @returns true if the unit has the aura, false otherwise.
		bool HasAuraSpellFromCaster(uint32 spellId, uint64 casterId);

		/// Builds an aura packet for network transmission.
		/// @param writer The writer to write the packet to.
		void BuildAuraPacket(io::Writer& writer) const;

		/// Notifies that mana has been used.
		void NotifyManaUsed();

		/// Sets the regeneration flags for the unit.
		/// @param regenerationFlags The regeneration flags to set.
		void SetRegeneration(uint32 regenerationFlags) { m_regeneration = regenerationFlags; }

		/// Gets the regeneration flags for the unit.
		/// @returns The regeneration flags as a uint32.
		uint32 GetRegeneration() const { return m_regeneration; }

		/// Checks if the unit regenerates health.
		/// @returns true if the unit regenerates health, false otherwise.
		bool RegeneratesHealth() const { return (m_regeneration & regeneration_flags::Health) != 0; }

		/// Checks if the unit regenerates power.
		/// @returns true if the unit regenerates power, false otherwise.
		bool RegeneratesPower() const { return (m_regeneration & regeneration_flags::Power) != 0; }

		/// Executed when an attack was successfully parried.
		virtual void OnParry();

		/// Executed when an attack was successfully dodged.
		virtual void OnDodge();

		/// Executed when an attack was successfully blocked.
		virtual void OnBlock();

		/// Teleports the unit to a new location on the same map.
		/// @param position The new position vector.
		/// @param facing The new facing direction.
		void TeleportOnMap(const Vector3& position, const Radian& facing);

		/// Teleports the unit to a new location on a different map.
		/// @param mapId The ID of the new map.
		/// @param position The new position vector.
		/// @param facing The new facing direction.
		virtual void Teleport(uint32 mapId, const Vector3& position, const Radian& facing);

		/// Modifies the character spell modifiers by applying or misapplying a new mod.
		/// @param mod The spell modifier to apply or misapply.
		/// @param apply Whether to apply or misapply the spell mod.
		void ModifySpellMod(const SpellModifier& mod, bool apply);

		void NotifyVisibilityChanged();

		/// Gets the total amount of spell mods for one type and one spell.
		/// @param type The spell modifier type.
		/// @param op The spell modifier operation.
		/// @param spellId The ID of the spell.
		/// @returns The total amount of spell mods as an int32.
		int32 GetTotalSpellMods(SpellModType type, SpellModOp op, uint32 spellId) const;

		/// Applies all matching spell mods of this character to a given value.
		/// @param op The spell modifier operation to apply.
		/// @param spellId Id of the spell, to know which modifiers do match.
		/// @param ref_value Reference of the base value, which will be modified by this method.
		/// @returns Delta value or 0 if ref_value didn't change.
		template<class T>
		T ApplySpellMod(SpellModOp op, uint32 spellId, T& ref_value) const
		{
			float totalPct = 1.0f;
			int32 totalFlat = 0;

			totalFlat += GetTotalSpellMods(spell_mod_type::Flat, op, spellId);
			totalPct += static_cast<float>(GetTotalSpellMods(spell_mod_type::Pct, op, spellId)) * 0.01f;

			const float diff = static_cast<float>(ref_value) * (totalPct - 1.0f) + static_cast<float>(totalFlat);
			ref_value = T(static_cast<float>(ref_value) + diff);

			return T(diff);
		}

		/// Sends a chat message of type "say" from the unit.
		/// @param message The message to send.
		void ChatSay(const String& message);

		/// Sends a chat message of type "yell" from the unit.
		/// @param message The message to send.
		void ChatYell(const String& message);

		void NotifyRootChanged();

		void NotifyStunChanged();

		void NotifySleepChanged();

		void NotifyFearChanged();

		/// Determines if a unit is rooted and should be able to move.
		bool IsRooted() const
		{
			return (m_movementInfo.movementFlags & movement_flags::Rooted) != 0;
			}

		/// Called when a proc event occurs to check if any auras should proc
		void TriggerProcEvent(SpellProcFlags eventFlags, GameUnitS* target = nullptr, uint32 damage = 0, uint32 procEx = 0, uint8 school = 0, bool isProc = false, uint64 familyFlags = 0);

	protected:
		/// Sends a local chat message from the unit.
		/// @param type The type of chat message.
		/// @param message The message to send.
		virtual void DoLocalChatMessage(ChatType type, const String& message);

	private:
		/// Sets the current victim of the unit.
		/// @param victim The new victim.
		void SetVictim(const std::shared_ptr<GameUnitS>& victim);

		/// Called when the current victim is killed.
		/// @param killer The unit that killed the victim.
		void VictimKilled(GameUnitS* killer);

		/// Called when the current victim despawns.
		/// @param victim The victim that despawned.
		void VictimDespawned(GameObjectS&);

	protected:
		/// Gets the miss chance for the unit.
		/// @returns The miss chance as a float.
		virtual float GetUnitMissChance() const;

		/// Checks if the unit has an offhand weapon.
		/// @returns true if the unit has an offhand weapon, false otherwise.
		virtual bool HasOffhandWeapon() const;

		/// Checks if the unit can dual wield weapons.
		/// @returns true if the unit can dual wield, false otherwise.
		bool CanDualWield() const;

		/// Gets the maximum skill value for a given level.
		/// @param level The level to check.
		/// @returns The maximum skill value as an int32.
		int32 GetMaxSkillValueForLevel(uint32 level) const;

		/// Rolls the outcome of a melee attack against a victim.
		/// @param victim The victim of the attack.
		/// @param attackType The type of weapon attack.
		/// @returns The outcome of the melee attack.
		MeleeAttackOutcome RollMeleeOutcomeAgainst(GameUnitS& victim, WeaponAttack attackType) const;

	public:
		/// Gets the miss chance for a melee attack against a victim.
		/// @param victim The victim of the attack.
		/// @param attackType The type of weapon attack.
		/// @param skillDiff The skill difference between the attacker and the victim.
		/// @param spellId The ID of the spell used in the attack.
		/// @returns The miss chance as a float.
		float MeleeMissChance(const GameUnitS& victim, weapon_attack::Type attackType, int32 skillDiff, uint32 spellId) const;

		/// Gets the critical hit chance for a melee attack against a victim.
		/// @param victim The victim of the attack.
		/// @param attackType The type of weapon attack.
		/// @returns The critical hit chance as a float.
		float CriticalHitChance(const GameUnitS& victim, weapon_attack::Type attackType) const;

		/// Returns the dodge chance in percent, ranging from 0 to 100.0f. If the unit can't dodge at all, this will always return 0.
		float DodgeChance() const;

		/// Returns the parry chance in percent, ranging from 0 to 100.0f. If the unit can't parry at all, this will always return 0.
		float ParryChance() const;

		/// Returns the block chance in percent, ranging from 0 to 100.0f. If the unit can't block at all, this will always return 0.
		float BlockChance() const;

		/// Returns true if the unit can block attacks.
		bool CanBlock() const { return (m_combatCapabilities & combat_capabilities::CanBlock) != 0; }

		/// Re-evaluates if the unit can block attacks based on spell effects.
		///	@param gainedEffect Set this to true if you are sure the unit should can block attacks. This is just a performance shortcut,
		///	so we don't iterate through spell effects if we don't need to. If this is set to false, spell effects will be checked.
		void NotifyCanBlock(bool gainedEffect);

		/// Returns true if the unit can parry melee attacks.
		bool CanParry() const { return (m_combatCapabilities & combat_capabilities::CanParry) != 0; }

		/// Re-evaluates if the unit can parry attacks based on spell effects.
		///	@param gainedEffect Set this to true if you are sure the unit should can parry attacks. This is just a performance shortcut,
		///	so we don't iterate through spell effects if we don't need to. If this is set to false, spell effects will be checked.
		void NotifyCanParry(bool gainedEffect);

		/// Returns true if the unit can dodge melee attacks.
		bool CanDodge() const { return (m_combatCapabilities & combat_capabilities::CanDodge) != 0; }

		/// Re-evaluates if the unit can dodge attacks based on spell effects.
		///	@param gainedEffect Set this to true if you are sure the unit should can dodge attacks. This is just a performance shortcut,
		///	so we don't iterate through spell effects if we don't need to. If this is set to false, spell effects will be checked.
		void NotifyCanDodge(bool gainedEffect);

		/// Returns true if the unit has an active aura effect of the given type. Don't use this too often as it's iterating through all active auras,
		///	which is not a constant complexity operation.
		bool HasAuraEffect(AuraType type) const;

		/// Returns true if the unit has a spell with a spell effect of the given type. Don't use this too often as it's iterating through all spells the unit knows,
		///	which is not a constant complexity operation.
		bool HasSpellEffect(SpellEffect type) const;

		/// Checks if the unit is currently attacking a specific victim.
		/// @param victim The victim to check.
		/// @returns true if the unit is attacking the victim, false otherwise.
		bool IsAttacking(const std::shared_ptr<GameUnitS>& victim) const { return m_victim.lock() == victim; }

		/// Checks if the unit is currently attacking any victim.
		/// @returns true if the unit is attacking, false otherwise.
		bool IsAttacking() const { return GetVictim() != nullptr; }

		/// Gets the current victim of the unit.
		/// @returns A pointer to the current victim.
		GameUnitS* GetVictim() const { return m_victim.lock().get(); }

		/// Starts attacking a specific victim.
		/// @param victim The victim to attack.
		void StartAttack(const std::shared_ptr<GameUnitS>& victim);

		/// Stops attacking the current victim.
		void StopAttack();

		/// Sets the target of the unit.
		/// @param targetGuid The GUID of the target.
		void SetTarget(uint64 targetGuid);

		/// Checks if the unit is in combat.
		/// @returns true if the unit is in combat, false otherwise.
		bool IsInCombat() const { return (Get<uint32>(object_fields::Flags) & unit_flags::InCombat) != 0; }

		/// Sets the combat state of the unit.
		/// @param inCombat true to set the unit in combat, false to set it out of combat.
		void SetInCombat(bool inCombat, bool pvp);

		/// Gets the melee reach of the unit.
		/// @returns The melee reach as a float.
		float GetMeleeReach() const;

		/// Adds an attacking unit to the list of attackers.
		/// @param attacker The unit that is attacking.
		void AddAttackingUnit(const GameUnitS& attacker);

		/// Removes an attacking unit from the list of attackers.
		/// @param attacker The unit to remove.
		void RemoveAttackingUnit(const GameUnitS& attacker);

		/// Removes all attacking units from the list of attackers.
		void RemoveAllAttackingUnits();

		/// Gets the base speed for a specific movement type.
		/// @param type The movement type.
		/// @returns The base speed as a float.
		float GetBaseSpeed(const MovementType type) const;

		/// Sets the base speed for a specific movement type.
		/// @param type The movement type.
		/// @param speed The new base speed.
		void SetBaseSpeed(const MovementType type, float speed);

		/// Gets the current speed for a specific movement type.
		/// @param type The movement type.
		/// @returns The current speed as a float.
		float GetSpeed(const MovementType type) const;

		/// Called by spell auras to notify that a speed aura has been applied or misapplied.
		/// If this unit is player controlled, a client notification is sent and the speed is
		/// only changed after the client has acknowledged this change. Otherwise, the speed
		/// is immediately applied using ApplySpeedChange.
		void NotifySpeedChanged(MovementType type, bool initial = false);

		/// Immediately applies a speed change for this unit. Never call this method directly
		/// unless you know what you're doing.
		/// @param type The movement type.
		/// @param speed The new speed value.
		/// @param initial Whether this is the initial speed change.
		void ApplySpeedChange(MovementType type, float speed, bool initial = false);

		/// Calculates the damage reduced by armor.
		/// @param attackerLevel The level of the attacker.
		/// @param damage The initial damage amount.
		/// @returns The reduced damage amount.
		uint32 CalculateArmorReducedDamage(uint32 attackerLevel, uint32 damage) const;

		/// Checks if another unit is an enemy.
		/// @param other The other unit to check.
		/// @returns true if the other unit is an enemy, false otherwise.
		bool UnitIsEnemy(const GameUnitS& other) const;

		/// Checks if another unit is friendly.
		/// @param other The other unit to check.
		/// @returns true if the other unit is friendly, false otherwise.
		bool UnitIsFriendly(const GameUnitS& other) const;

		/// Gets the faction template of the unit.
		/// @returns A pointer to the faction template entry.
		const proto::FactionTemplateEntry* GetFactionTemplate() const;

		/// Gets the level of the unit.
		/// @returns The level as uint32.
		uint32 GetLevel() const { return Get<uint32>(object_fields::Level); }

		/// Gets the max level of the unit.
		/// @returns The max level as uint32.
		uint32 GetMaxLevel() const { return Get<uint32>(object_fields::MaxLevel); }

		/// Gets the amount of xp required to reach the next level (not the remaining xp, but the total amount).
		/// @returns The next level xp as uint32.
		uint32 GetNextLevelXp() const { return Get<uint32>(object_fields::NextLevelXp); }

		/// Sets the binding location of the unit.
		/// @param mapId The ID of the map.
		/// @param position The position vector.
		/// @param facing The facing direction.
		void SetBinding(uint32 mapId, const Vector3& position, const Radian& facing);

		/// Gets the map ID of the binding location.
		/// @returns The map ID as a uint32.
		uint32 GetBindMap() const { return m_bindMap; }

		/// Gets the position of the binding location.
		/// @returns The position vector.
		const Vector3& GetBindPosition() const { return m_bindPosition; }

		/// Gets the facing direction of the binding location.
		/// @returns The facing direction.
		const Radian& GetBindFacing() const { return m_bindFacing; }

	protected:
		/// Called when the unit is killed.
		/// @param killer The unit that killed this unit.
		virtual void OnKilled(GameUnitS* killer);

		/// Called when a spell is learned by the unit.
		/// @param spell The spell entry that was learned.
		virtual void OnSpellLearned(const proto::SpellEntry& spell) {}

		/// Called when a spell is unlearned by the unit.
		/// @param spell The spell entry that was unlearned.
		virtual void OnSpellUnlearned(const proto::SpellEntry& spell) {}

		/// Called when a spell cast ends.
		/// @param succeeded Whether the spell cast succeeded.
		virtual void OnSpellCastEnded(bool succeeded);

		/// Called when the unit regenerates.
		virtual void OnRegeneration();

		/// Regenerates the unit's health.
		virtual void RegenerateHealth();

		/// Regenerates the unit's power.
		/// @param powerType The type of power to regenerate.
		virtual void RegeneratePower(PowerType powerType);

		/// Adds power to the unit.
		/// @param powerType The type of power to add.
		/// @param amount The amount of power to add.
		virtual void AddPower(PowerType powerType, int32 amount);

		/// Called when an attack swing event occurs.
		/// @param attackSwingEvent The attack swing event.
		void OnAttackSwingEvent(AttackSwingEvent attackSwingEvent) const;

	public:
		/// Gets the maximum base points for a specific aura type.
		/// @param type The aura type.
		/// @returns The maximum base points as an int32.
		int32 GetMaximumBasePoints(AuraType type) const;

		/// Gets the minimum base points for a specific aura type.
		/// @param type The aura type.
		/// @returns The minimum base points as an int32.
		int32 GetMinimumBasePoints(AuraType type) const;

		/// Gets the total multiplier for a specific aura type.
		/// @param type The aura type.
		/// @returns The total multiplier as a float.
		float GetTotalMultiplier(AuraType type) const;

		/// Called when an attack swing occurs.
		void OnAttackSwing();

		void OnPvpCombatTimer();

		/// Sets the stand state of the unit.
		/// @param standState The new stand state.
		void SetStandState(const unit_stand_state::Type standState) { Set<uint32>(object_fields::StandState, standState); }

		/// Gets the stand state of the unit.
		/// @returns The stand state as a unit_stand_state::Type enum.
		unit_stand_state::Type GetStandState() const { return static_cast<unit_stand_state::Type>(Get<uint32>(object_fields::StandState)); }

		/// Checks if the unit is sitting.
		/// @returns true if the unit is sitting, false otherwise.
		bool IsSitting() const { return GetStandState() == unit_stand_state::Sit; }

		// Visibility system
		[[nodiscard]] UnitVisibility GetVisibility() const { return m_visibility; }

		void SetVisibility(UnitVisibility x);

		virtual void UpdateVisibilityAndView();

	protected:
		/// Prepares the field map for the unit.
		virtual void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::UnitFieldCount);
		}

	private:
		/// Called when the despawn timer expires.
		void OnDespawnTimer();

		/// Triggers the next auto attack.
		void TriggerNextAutoAttack();

	public:
		/// Gets the timer queue for the unit.
		/// @returns A reference to the timer queue.
		TimerQueue& GetTimers() const { return m_timers; }

		/// Gets the unit mover for the unit.
		/// @returns A reference to the unit mover.
		UnitMover& GetMover() const { return *m_mover; }

		/// Generates the next client ack ID for the unit.
		/// @returns The next client ack ID as a uint32.
		inline uint32 GenerateAckId() { return m_ackGenerator.GenerateId(); }

	protected:
		typedef LinearSet<const GameUnitS*> AttackingUnitSet;

		TimerQueue& m_timers;
		Countdown m_despawnCountdown;
		std::unique_ptr<UnitMover> m_mover;
		Countdown m_attackSwingCountdown;
		GameTime m_lastMainHand = 0, m_lastOffHand = 0;
		Countdown m_regenCountdown;
		GameTime m_lastManaUse = 0;

		std::weak_ptr<GameUnitS> m_victim;

		std::set<const proto::SpellEntry*> m_spells;
		std::unique_ptr<SpellCast> m_spellCast;

		std::map<uint32, GameTime> m_spellCooldowns;
		std::map<uint32, GameTime> m_spellCategoryCooldowns;

		AttackingUnitSet m_attackingUnits;

		NetUnitWatcherS* m_netUnitWatcher = nullptr;
		mutable Vector3 m_lastPosition;

		stable_list<std::shared_ptr<AuraContainer>> m_auras;

		typedef std::array<float, unit_mod_type::End> UnitModTypeArray;
		typedef std::array<UnitModTypeArray, unit_mods::End> UnitModArray;
		UnitModArray m_unitMods;

		std::array<float, movement_type::Count> m_speedBonus;
		IdGenerator<uint32> m_ackGenerator;
		std::list<PendingMovementChange> m_pendingMoveChanges;

		float m_manaRegenPerTick = 0.0f;
		float m_healthRegenPerTick = 0.0f;

		mutable const proto::FactionTemplateEntry* m_cachedFactionTemplate = nullptr;
		scoped_connection_container m_victimSignals;

		uint32 m_bindMap;
		Vector3 m_bindPosition;
		Radian m_bindFacing;

		bool m_canDualWield = false;
		SpellModsByOp m_spellModsByOp;

		uint8 m_combatCapabilities = combat_capabilities::None;

		std::map<uint8, float> m_baseSpeeds;

		uint32 m_regeneration = regeneration_flags::None;

		UnitVisibility m_visibility = UnitVisibility::On;

		Countdown m_pvpCombatCountdown;
		uint32 m_state = 0;

	private:
		/// Serializes a GameUnitS object to a Writer for binary serialization.
		/// Used to convert the object state into a binary format for storage or network transmission.
		/// @param w The Writer to write to.
		/// @param object The GameUnitS object to serialize.
		/// @returns Reference to the Writer for chaining.
		friend io::Writer& operator << (io::Writer& w, GameUnitS const& object);

		/// Deserializes a GameUnitS object from a Reader during binary deserialization.
		/// Used to reconstruct the object state from a binary format after storage or network transmission.
		/// @param r The Reader to read from.
		/// @param object The GameUnitS object to deserialize into.
		/// @returns Reference to the Reader for chaining.
		friend io::Reader& operator >> (io::Reader& r, GameUnitS& object);
	};

	io::Writer& operator << (io::Writer& w, GameUnitS const& object);
	io::Reader& operator >> (io::Reader& r, GameUnitS& object);
}
