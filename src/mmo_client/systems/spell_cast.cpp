// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_cast.h"
#include "shared/game_client/spell_visualization_service.h"

#include "frame_ui/frame_mgr.h"
#include "game/spell_target_map.h"
#include "game_client/game_player_c.h"
#include "game_client/object_mgr.h"
#include "net/realm_connector.h"

namespace mmo
{
	SpellCast::SpellCast(RealmConnector& connector, const proto_client::SpellManager& spells, const proto_client::RangeManager& ranges)
		: m_connector(connector)
		, m_spells(spells)
		, m_ranges(ranges)
	{
	}

	namespace spell_target_requirements
	{
		enum Type
		{
			None = 0,

			FriendlyUnitTarget = 1 << 0,
			HostileUnitTarget = 1 << 1,

			AreaTarget = 1 << 2,

			PartyMemberTarget = 1 << 3,
			PetTarget = 1 << 4,
			ObjectTarget = 1 << 5,

			Self = 1 << 6,

			AnyUnitTarget = FriendlyUnitTarget | HostileUnitTarget | Self,
		};
	}

	static uint64 GetSpellTargetRequirements(const proto_client::SpellEntry& spell)
	{
		uint64 targetRequirements = spell_target_requirements::None;

		for (const auto& effect : spell.effects())
		{
			switch (effect.targeta())
			{
			case spell_effect_targets::TargetAlly:
				targetRequirements |= spell_target_requirements::FriendlyUnitTarget;
				break;
			case spell_effect_targets::TargetAny:
				targetRequirements |= spell_target_requirements::AnyUnitTarget;
				break;
			case spell_effect_targets::TargetEnemy:
				targetRequirements |= spell_target_requirements::HostileUnitTarget;
				break;
			case spell_effect_targets::ObjectTarget:
				targetRequirements |= spell_target_requirements::ObjectTarget;
				break;
			case spell_effect_targets::Pet:
				targetRequirements |= spell_target_requirements::PetTarget;
				break;
			}
		}

		return targetRequirements;
	}

	void SpellCast::OnEnterWorld()
	{
		m_spellCastId = 0;

		// Register packet handlers
	}

	void SpellCast::OnLeftWorld()
	{
		// We are no longer casting a spell
		m_spellCastId = 0;
	}

	void SpellCast::OnSpellStart(const proto_client::SpellEntry& spell, GameTime castTime)
	{
		// Visualization: start cast
		SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::StartCast, spell, ObjectMgr::GetActivePlayer().get(), {});

		// If there's a cast time, trigger CASTING event for looped visuals
		if (castTime > 0)
		{
			SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::Casting, spell, ObjectMgr::GetActivePlayer().get(), {});
		}

		m_spellCastId = spell.id();
		FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_START", &spell, castTime);
	}

	void SpellCast::OnSpellGo(uint32 spellId)
	{
		if (GetCastingSpellId() != spellId)
		{
			return;
		}

		if (const auto* spell = m_spells.getById(spellId))
		{
			SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::CastSucceeded, *spell, ObjectMgr::GetActivePlayer().get(), {});
		}

		FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FINISH", true);
		m_spellCastId = 0;
	}

	void SpellCast::OnSpellFailure(uint32 spellId)
	{
		if (GetCastingSpellId() != spellId)
		{
			return;
		}

		if (const auto* spell = m_spells.getById(spellId))
		{
			SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::CancelCast, *spell, ObjectMgr::GetActivePlayer().get(), {});
		}

		m_spellCastId = 0;
	}

	bool SpellCast::SetSpellTargetMap(SpellTargetMap& targetMap, const proto_client::SpellEntry& spell)
	{
		std::shared_ptr<GamePlayerC> unit = ObjectMgr::GetActivePlayer();
		const uint64 targetUnitGuid = ObjectMgr::GetSelectedObjectGuid();

		// Check if we need to provide a target unit
		const uint64 requirements = GetSpellTargetRequirements(spell);
		if ((requirements & spell_target_requirements::AnyUnitTarget) != 0)
		{
			// Validate if target unit exists
			std::shared_ptr<GameUnitC> targetUnit = ObjectMgr::Get<GameUnitC>(targetUnitGuid);
			if (!targetUnit)
			{
				// If friendly unit target is required and we have none, target ourself instead automatically
				if ((requirements & spell_target_requirements::FriendlyUnitTarget) != 0 && (requirements & spell_target_requirements::HostileUnitTarget) == 0)
				{
					targetUnit = unit;
				}
				else
				{
					return false;
				}
			}

			if ((requirements & spell_target_requirements::FriendlyUnitTarget) != 0 && (requirements & spell_target_requirements::HostileUnitTarget) == 0 && !unit->IsFriendlyTo(*targetUnit))
			{
				// Target unit is not friendly but spell requires a friendly unit - use fallback to ourself
				targetUnit = unit;
			}

			if ((requirements & spell_target_requirements::FriendlyUnitTarget) == 0 && (requirements & spell_target_requirements::HostileUnitTarget) != 0 && unit->IsFriendlyTo(*targetUnit))
			{
				return false;
			}

			// Set target unit
			targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			targetMap.SetUnitTarget(targetUnit ? targetUnit->GetGuid() : 0);
		}

		return true;
	}

	void SpellCast::CastSpell(uint32 spellId, GameObjectC* explicitTarget)
	{
		// Check if we are currently casting a spell
		if (IsCasting())
		{
			// TODO: Add support for delayed spell casting
			return;
		}

		std::shared_ptr<GamePlayerC> unit = ObjectMgr::GetActivePlayer();
		if (!unit)
		{
			return;
		}

		// Check if spell is known
		if (!unit->HasSpell(spellId))
		{
			FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", "SPELL_CAST_FAILED_NOT_KNOWN");
			return;
		}

		const uint64 targetUnitGuid = ObjectMgr::GetSelectedObjectGuid();

		// Is known spell?
		const auto* spell = m_spells.getById(spellId);
		if (!spell)
		{
			ELOG("Unknown spell " << spellId);
			return;
		}

		// We can't cast passive spells!
		if ((spell->attributes(0) & spell_attributes::Passive) != 0)
		{
			ELOG("Can't cast passive spells!");
			return;
		}

		SpellTargetMap targetMap{};

		// Power check
		if ((spell->powertype() != unit->GetPowerType() && spell->cost() != 0) ||
			spell->cost() > unit->GetPower(unit->GetPowerType()))
		{
			FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", "SPELL_CAST_FAILED_NO_POWER");
			return;
		}

		// Check if we need to provide a target unit
		uint64 requirements = GetSpellTargetRequirements(*spell);
		if ((requirements & spell_target_requirements::AnyUnitTarget) != 0)
		{
			// Validate if target unit exists
			std::shared_ptr<GameUnitC> targetUnit = ObjectMgr::Get<GameUnitC>(targetUnitGuid);
			if (!targetUnit)
			{
				// If friendly unit target is required and we have none, target ourself instead automatically
				if ((requirements & spell_target_requirements::FriendlyUnitTarget) != 0 && (requirements & spell_target_requirements::HostileUnitTarget) == 0)
				{
					targetUnit = unit;
				}
				else
				{
					// TODO: Instead of printing an error here we should trigger a selection mode where the user has to click on a target unit instead
					FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", "SPELL_CAST_FAILED_BAD_TARGETS");
					return;
				}
			}

			if ((requirements & spell_target_requirements::FriendlyUnitTarget) != 0 && (requirements & spell_target_requirements::HostileUnitTarget) == 0 && !unit->IsFriendlyTo(*targetUnit))
			{
				// Target unit is not friendly but spell requires a friendly unit - use fallback to ourself
				targetUnit = unit;
			}

			if ((requirements & spell_target_requirements::FriendlyUnitTarget) == 0 && (requirements & spell_target_requirements::HostileUnitTarget) != 0 && unit->IsFriendlyTo(*targetUnit))
			{
				FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", "SPELL_CAST_FAILED_TARGET_FRIENDLY");
				return;
			}

			// Set target unit
			targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			targetMap.SetUnitTarget(targetUnit ? targetUnit->GetGuid() : 0);

			// Range check
			if (targetUnit && spell->rangetype() != 0)
			{
				// Check if we are in range of the target unit
				if (const auto* range = m_ranges.getById(spell->rangetype()))
				{
					const float distanceSquared = unit->GetPosition().GetSquaredDistanceTo(targetUnit->GetPosition());
					if (distanceSquared > range->range() * range->range())
					{
						FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", "SPELL_CAST_FAILED_OUT_OF_RANGE");
						return;
					}
				}
			}

			// Can this spell target dead units, and if not, are we targeting a dead unit?
			if ((spell->attributes(0) & spell_attributes::CanTargetDead) == 0 &&
				(targetUnit && !targetUnit->IsAlive()))
			{
				FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", "SPELL_CAST_FAILED_TARGET_NOT_DEAD");
				return;
			}
		}

		if (requirements & spell_target_requirements::ObjectTarget)
		{
			if (explicitTarget)
			{
				// Range check
				if (spell->rangetype() != 0)
				{
					// Check if we are in range of the target unit
					if (const auto* range = m_ranges.getById(spell->rangetype()))
					{
						const float distanceSquared = unit->GetPosition().GetSquaredDistanceTo(explicitTarget->GetPosition());
						if (distanceSquared > range->range() * range->range())
						{
							FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", "SPELL_CAST_FAILED_OUT_OF_RANGE");
							return;
						}
					}
				}

				targetMap.SetTargetMap(spell_cast_target_flags::Object);
				targetMap.SetObjectTarget(explicitTarget->GetGuid());
			}
			else
			{
				FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "SPELL_CAST_FAILED_BAD_TARGETS");
				return;
			}
		}

		if ((spell->interruptflags() & spell_interrupt_flags::Movement) != 0)
		{
			if (unit->GetMovementInfo().IsChangingPosition())
			{
				ELOG("Can't cast spell while moving");
				return;
			}
		}

		if ((spell->attributes(0) & spell_attributes::NotInCombat) != 0 &&
			unit->IsInCombat())
		{
			ELOG("Spell not castable while in combat!");
			return;
		}

		// Does this spell require reagents?
		if (spell->reagents_size() > 0)
		{
			for (const auto& reagent : spell->reagents())
			{
				ASSERT(reagent.item() != 0);
				ASSERT(reagent.count() > 0);
				if (ObjectMgr::GetItemCount(reagent.item()) < reagent.count())
				{
					FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "SPELL_CAST_FAILED_REAGENTS");
					return;
				}
			}
		}

		// Send network packet to start casting the spell
		m_spellCastId = spellId;
		m_connector.CastSpell(spellId, targetMap);
	}

	bool SpellCast::CancelCast()
	{
		// Check if we are currently casting a spell
		if (!IsCasting())
		{
			return false;
		}

		// Send network packet to stop casting the spell
		m_connector.CancelCast();
		m_spellCastId = 0;
		return true;
	}

	bool SpellCast::IsCasting() const
	{
		return m_spellCastId != 0;
	}

	uint32 SpellCast::GetCastingSpellId() const
	{
		return m_spellCastId;
	}
}
