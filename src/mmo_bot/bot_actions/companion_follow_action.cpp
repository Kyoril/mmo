// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "companion_follow_action.h"

#include "../bot_movement_math.h"
#include "../bot_nav_service.h"
#include "../bot_unit.h"

#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace
{
	using namespace mmo;

	constexpr uint8 kWarriorClassId = 1;
	constexpr uint8 kPriestClassId = 5;
	constexpr uint8 kMageClassId = 8;
	constexpr GameTime kAutoAttackRetryDelayMs = 1000;
	constexpr GameTime kCastFailureBackoffMs = 750;

	const char* FollowDecisionTypeToString(const BotFollowDecisionType type)
	{
		switch (type)
		{
		case BotFollowDecisionType::Advance:
			return "advance";
		case BotFollowDecisionType::Repath:
			return "repath";
		case BotFollowDecisionType::Abort:
			return "abort";
		case BotFollowDecisionType::Stuck:
			return "stuck";
		case BotFollowDecisionType::Hold:
		default:
			return "hold";
		}
	}

	const char* AnchorKindToString(const BotFollowAnchorKind kind)
	{
		switch (kind)
		{
		case BotFollowAnchorKind::Leader:
			return "leader";
		case BotFollowAnchorKind::Role:
			return "role";
		case BotFollowAnchorKind::Custom:
		default:
			return "custom";
		}
	}

	bool StartsWith(std::string_view value, std::string_view prefix)
	{
		return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
	}

	[[nodiscard]] bool IsRuntimeHardBlocked(const CompanionFollowPreconditionFailure failure)
	{
		return failure == CompanionFollowPreconditionFailure::SelfUnavailable
			|| failure == CompanionFollowPreconditionFailure::InvalidSelfPosition;
	}

	std::string JoinSpellIds(const std::unordered_set<uint32>& spellIds)
	{
		if (spellIds.empty())
		{
			return "none";
		}

		std::vector<uint32> orderedIds(spellIds.begin(), spellIds.end());
		std::sort(orderedIds.begin(), orderedIds.end());
		std::ostringstream stream;
		for (size_t i = 0; i < orderedIds.size(); ++i)
		{
			if (i > 0)
			{
				stream << ',';
			}
			stream << orderedIds[i];
		}
		return stream.str();
	}

	std::string FirstIssueOrFallback(const BotWarriorCapabilities& capabilities, std::string_view fallback)
	{
		if (!capabilities.issues.empty())
		{
			return capabilities.issues.front();
		}

		return std::string(fallback);
	}

	std::string FirstIssueOrFallback(const BotClericCapabilities& capabilities, std::string_view fallback)
	{
		if (!capabilities.issues.empty())
		{
			return capabilities.issues.front();
		}

		return std::string(fallback);
	}

	std::string FirstIssueOrFallback(const BotMageCapabilities& capabilities, std::string_view fallback)
	{
		if (!capabilities.issues.empty())
		{
			return capabilities.issues.front();
		}

		return std::string(fallback);
	}

	BotCompanionRole DetermineSelfRole(const uint8 characterClass)
	{
		switch (characterClass)
		{
		case 1: // Warrior
		case 2: // Paladin
		case 11: // Druid
			return BotCompanionRole::Tank;
		case 5: // Priest
			return BotCompanionRole::Healer;
		case 3: // Hunter
		case 8: // Mage
		case 9: // Warlock
			return BotCompanionRole::RangedDps;
		case 4: // Rogue
		case 6: // Death Knight
			return BotCompanionRole::MeleeDps;
		case 7: // Shaman
		default:
			return BotCompanionRole::None;
		}
	}

	BotCompanionRole DetermineDesiredCombatAnchorRole(const BotCompanionRole selfRole)
	{
		switch (selfRole)
		{
		case BotCompanionRole::Tank:
			return BotCompanionRole::None;
		case BotCompanionRole::Healer:
		case BotCompanionRole::MeleeDps:
		case BotCompanionRole::RangedDps:
			return BotCompanionRole::Tank;
		case BotCompanionRole::None:
		default:
			return BotCompanionRole::Tank;
		}
	}

	BotCompanionMemberSnapshot BuildMemberSnapshot(
		BotContext& context,
		const uint64 guid,
		const std::string& fallbackLabel,
		const uint64 selfGuid,
		const uint64 leaderGuid,
		const BotCompanionRole selfRole)
	{
		BotCompanionMemberSnapshot member;
		member.guid = guid;
		member.isSelf = guid == selfGuid;
		member.isLeader = guid == leaderGuid;
		member.isBot = member.isSelf;
		member.role = member.isSelf
			? selfRole
			: (member.isLeader ? BotCompanionRole::Tank : BotCompanionRole::None);
		member.label = fallbackLabel;

		if (const BotUnit* unit = context.GetUnit(guid))
		{
			member.position = unit->GetPosition();
			member.hasPosition = true;
			member.facing = unit->GetFacing();
			member.hasFacing = true;
			member.isAlive = unit->IsAlive();
			member.isAware = true;
			member.isInCombat = unit->IsInCombat();
			member.targetGuid = unit->GetTargetGuid();
			if (member.label.empty())
			{
				member.label = unit->GetName();
			}
		}
		else
		{
			member.isAlive = true;
			member.isAware = false;
			member.isInCombat = false;
		}

		if (member.label.empty())
		{
			if (member.isSelf)
			{
				member.label = "self";
			}
			else if (member.isLeader)
			{
				member.label = "leader";
			}
		}

		return member;
	}

	BotWarriorCapabilities FilterKnownWarriorCapabilities(const BotWarriorCapabilities& baseCapabilities, const std::vector<uint32>& knownSpellIds)
	{
		BotWarriorCapabilities filtered = baseCapabilities;
		const std::unordered_set<uint32> knownSpellSet(knownSpellIds.begin(), knownSpellIds.end());

		auto isKnown = [&](const std::optional<BotWarriorResolvedSpell>& spell)
		{
			return spell.has_value() && knownSpellSet.find(spell->spellId) != knownSpellSet.end();
		};

		filtered.spells.clear();
		for (const BotWarriorResolvedSpell& spell : baseCapabilities.spells)
		{
			if (knownSpellSet.find(spell.spellId) != knownSpellSet.end())
			{
				filtered.spells.push_back(spell);
			}
		}

		if (!isKnown(filtered.gapCloser)) filtered.gapCloser.reset();
		if (!isKnown(filtered.threatBuilder)) filtered.threatBuilder.reset();
		if (!isKnown(filtered.armorDebuff)) filtered.armorDebuff.reset();
		if (!isKnown(filtered.rageBuilder)) filtered.rageBuilder.reset();
		if (!isKnown(filtered.partyBuff)) filtered.partyBuff.reset();
		if (!isKnown(filtered.mitigation)) filtered.mitigation.reset();
		if (!isKnown(filtered.rageSpender)) filtered.rageSpender.reset();
		if (!isKnown(filtered.taunt)) filtered.taunt.reset();

		filtered.resolved = baseCapabilities.resolved && baseCapabilities.mainhandAutoAttackSpellId != 0;
		return filtered;
	}

	BotClericCapabilities FilterKnownClericCapabilities(const BotClericCapabilities& baseCapabilities, const std::vector<uint32>& knownSpellIds)
	{
		BotClericCapabilities filtered = baseCapabilities;
		const std::unordered_set<uint32> knownSpellSet(knownSpellIds.begin(), knownSpellIds.end());

		auto isKnown = [&](const std::optional<BotClericResolvedSpell>& spell)
		{
			return spell.has_value() && knownSpellSet.find(spell->spellId) != knownSpellSet.end();
		};

		filtered.spells.clear();
		for (const BotClericResolvedSpell& spell : baseCapabilities.spells)
		{
			if (knownSpellSet.find(spell.spellId) != knownSpellSet.end())
			{
				filtered.spells.push_back(spell);
			}
		}

		if (!isKnown(filtered.emergencyHeal)) filtered.emergencyHeal.reset();
		if (!isKnown(filtered.efficientHeal)) filtered.efficientHeal.reset();
		if (!isKnown(filtered.supportAura)) filtered.supportAura.reset();
		if (!isKnown(filtered.supportBuff)) filtered.supportBuff.reset();
		if (!isKnown(filtered.defensive)) filtered.defensive.reset();
		if (!isKnown(filtered.damageFiller)) filtered.damageFiller.reset();

		filtered.resolved = baseCapabilities.resolved
			&& filtered.powerType == power_type::Mana
			&& filtered.emergencyHeal.has_value()
			&& filtered.efficientHeal.has_value()
			&& filtered.damageFiller.has_value();
		return filtered;
	}

	BotMageCapabilities FilterKnownMageCapabilities(const BotMageCapabilities& baseCapabilities, const std::vector<uint32>& knownSpellIds)
	{
		BotMageCapabilities filtered = baseCapabilities;
		const std::unordered_set<uint32> knownSpellSet(knownSpellIds.begin(), knownSpellIds.end());

		auto isKnown = [&](const std::optional<BotMageResolvedSpell>& spell)
		{
			return spell.has_value() && knownSpellSet.find(spell->spellId) != knownSpellSet.end();
		};

		filtered.spells.clear();
		for (const BotMageResolvedSpell& spell : baseCapabilities.spells)
		{
			if (knownSpellSet.find(spell.spellId) != knownSpellSet.end())
			{
				filtered.spells.push_back(spell);
			}
		}

		if (!isKnown(filtered.primaryNuke)) filtered.primaryNuke.reset();
		if (!isKnown(filtered.instantFallback)) filtered.instantFallback.reset();
		if (!isKnown(filtered.control)) filtered.control.reset();
		if (!isKnown(filtered.selfBuffEscape)) filtered.selfBuffEscape.reset();
		if (!isKnown(filtered.emergencySpacing)) filtered.emergencySpacing.reset();

		filtered.resolved = filtered.powerType == power_type::Mana
			&& filtered.primaryNuke.has_value();
		return filtered;
	}

	[[nodiscard]] bool IsValidHealthPercent(const float healthPercent)
	{
		return std::isfinite(healthPercent) && healthPercent >= 0.0f && healthPercent <= 1.0f;
	}

	struct ClericHostileTargetCandidate final
	{
		const BotUnit* unit { nullptr };
		const char* source { "none" };
	};

	ClericHostileTargetCandidate SelectClericHostileTarget(BotContext& context, const CompanionFollowControllerOutput& output)
	{
		const BotUnit* self = context.GetSelf();
		if (!self)
		{
			return {};
		}

		auto isValidTarget = [&](const uint64 guid, const char* source) -> ClericHostileTargetCandidate
		{
			if (guid == 0)
			{
				return {};
			}

			const BotUnit* candidate = context.GetUnit(guid);
			if (!candidate || !candidate->IsAlive())
			{
				return {};
			}

			if (!candidate->IsHostileTo(*self) && !candidate->IsAttackableBy(*self))
			{
				return {};
			}

			ClericHostileTargetCandidate result;
			result.unit = candidate;
			result.source = source;
			return result;
		};

		if (output.companion.hasAnchor)
		{
			if (const BotUnit* anchorUnit = context.GetUnit(output.companion.anchor.guid))
			{
				if (const ClericHostileTargetCandidate anchorTarget = isValidTarget(anchorUnit->GetTargetGuid(), "anchor_target"); anchorTarget.unit)
				{
					return anchorTarget;
				}
			}
		}

		if (const ClericHostileTargetCandidate selfTarget = isValidTarget(self->GetTargetGuid(), "self_target"); selfTarget.unit)
		{
			return selfTarget;
		}

		for (const BotUnit* unit : context.GetUnitsTargetingSelf())
		{
			if (!unit)
			{
				continue;
			}

			if (const ClericHostileTargetCandidate targetingSelf = isValidTarget(unit->GetGuid(), "targeting_self"); targetingSelf.unit)
			{
				return targetingSelf;
			}
		}

		return {};
	}

	struct WarriorTargetCandidate final
	{
		const BotUnit* unit { nullptr };
		const char* source { "none" };
	};

	WarriorTargetCandidate SelectWarriorTarget(BotContext& context, const CompanionFollowControllerOutput& output)
	{
		const BotUnit* self = context.GetSelf();
		if (!self)
		{
			return {};
		}

		auto isValidTarget = [&](const uint64 guid, const char* source) -> WarriorTargetCandidate
		{
			if (guid == 0)
			{
				return {};
			}

			const BotUnit* candidate = context.GetUnit(guid);
			if (!candidate || !candidate->IsAlive())
			{
				return {};
			}

			if (!candidate->IsHostileTo(*self) && !candidate->IsAttackableBy(*self))
			{
				return {};
			}

			WarriorTargetCandidate result;
			result.unit = candidate;
			result.source = source;
			return result;
		};

		if (context.IsAutoAttacking())
		{
			if (const WarriorTargetCandidate current = isValidTarget(context.GetAutoAttackTarget(), "auto_attack_target"); current.unit)
			{
				return current;
			}
		}

		if (output.companion.hasAnchor)
		{
			if (const BotUnit* anchorUnit = context.GetUnit(output.companion.anchor.guid))
			{
				if (const WarriorTargetCandidate anchorTarget = isValidTarget(anchorUnit->GetTargetGuid(), "anchor_target"); anchorTarget.unit)
				{
					return anchorTarget;
				}
			}
		}

		if (const WarriorTargetCandidate selfTarget = isValidTarget(self->GetTargetGuid(), "self_target"); selfTarget.unit)
		{
			return selfTarget;
		}

		for (const BotUnit* unit : context.GetUnitsTargetingSelf())
		{
			if (!unit)
			{
				continue;
			}

			if (const WarriorTargetCandidate targetingSelf = isValidTarget(unit->GetGuid(), "targeting_self"); targetingSelf.unit)
			{
				return targetingSelf;
			}
		}

		return {};
	}

	struct MageTargetCandidate final
	{
		const BotUnit* unit { nullptr };
		const char* source { "none" };
	};

	MageTargetCandidate SelectMagePrimaryTarget(BotContext& context, const CompanionFollowControllerOutput& output)
	{
		const BotUnit* self = context.GetSelf();
		if (!self)
		{
			return {};
		}

		auto isValidTarget = [&](const uint64 guid, const char* source) -> MageTargetCandidate
		{
			if (guid == 0)
			{
				return {};
			}

			const BotUnit* candidate = context.GetUnit(guid);
			if (!candidate || !candidate->IsAlive())
			{
				return {};
			}

			if (!candidate->IsHostileTo(*self) && !candidate->IsAttackableBy(*self))
			{
				return {};
			}

			MageTargetCandidate result;
			result.unit = candidate;
			result.source = source;
			return result;
		};

		if (output.companion.hasAnchor)
		{
			if (const BotUnit* anchorUnit = context.GetUnit(output.companion.anchor.guid))
			{
				if (const MageTargetCandidate anchorTarget = isValidTarget(anchorUnit->GetTargetGuid(), "anchor_target"); anchorTarget.unit)
				{
					return anchorTarget;
				}
			}
		}

		if (const MageTargetCandidate selfTarget = isValidTarget(self->GetTargetGuid(), "self_target"); selfTarget.unit)
		{
			return selfTarget;
		}

		for (const BotUnit* unit : context.GetUnitsTargetingSelf())
		{
			if (!unit)
			{
				continue;
			}

			if (const MageTargetCandidate targetingSelf = isValidTarget(unit->GetGuid(), "targeting_self"); targetingSelf.unit)
			{
				return targetingSelf;
			}
		}

		return {};
	}

	BotMageHostileSnapshot BuildMageHostileSnapshot(BotContext& context, const BotUnit& self, const BotUnit& unit)
	{
		BotMageHostileSnapshot hostile;
		hostile.guid = unit.GetGuid();
		hostile.isAlive = unit.IsAlive();
		hostile.isAware = true;
		hostile.isHostile = unit.IsHostileTo(self) || unit.IsAttackableBy(self);
		hostile.targetingSelf = unit.GetTargetGuid() == self.GetGuid();
		hostile.distanceToSelf = context.GetDistanceToUnit(unit.GetGuid());
		return hostile;
	}

	std::vector<BotMageHostileSnapshot> BuildMageNearbyHostiles(
		BotContext& context,
		const BotUnit& self,
		const MageTargetCandidate& primaryTarget)
	{
		std::vector<BotMageHostileSnapshot> hostiles;
		std::unordered_set<uint64> seenGuids;

		auto append = [&](const BotUnit* unit)
		{
			if (!unit || !seenGuids.insert(unit->GetGuid()).second)
			{
				return;
			}
			hostiles.push_back(BuildMageHostileSnapshot(context, self, *unit));
		};

		append(primaryTarget.unit);
		for (const BotUnit* unit : context.GetUnitsTargetingSelf())
		{
			append(unit);
		}
		for (const BotUnit* unit : context.GetHostilesInRange(40.0f))
		{
			append(unit);
		}

		return hostiles;
	}
}

namespace mmo
{
	CompanionFollowAction::CompanionFollowAction(BotFollowConfig config, const float moveSpeed)
		: m_controller(std::move(config))
		, m_moveSpeed(moveSpeed)
	{
	}

	std::string CompanionFollowAction::GetDescription() const
	{
		return "Follow the party leader out of combat, use role-aware anchors in combat, and execute warrior, cleric, or mage companion actions inside the same runtime";
	}

	ActionResult CompanionFollowAction::Execute(BotContext& context)
	{
		const GameTime now = context.GetServerTime();
		const CompanionFollowControllerInput input = BuildControllerInput(context, now);
		const CompanionFollowControllerOutput output = m_controller.Evaluate(input);
		const std::string details = BuildDecisionDetails(context, input, output);
		LogCompanionModeOnce(output, details);
		LogAnchorDecisionOnce(output, details);
		ObserveWarriorCastFailure(context, output);
		ObserveClericCastFailure(context, output);
		ObserveMageCastFailure(context, output);
		ApplyDecision(context, input, output);
		ExecuteWarriorRuntime(context, input, output);
		ExecuteClericRuntime(context, input, output);
		ExecuteMageRuntime(context, input, output);
		UpdatePreviousCompanionState(output);
		return ActionResult::InProgress;
	}

	void CompanionFollowAction::OnAbort(BotContext& context)
	{
		StopMovement(context);
		m_path.points.clear();
		m_state = {};
		m_previousCompanionState = {};
		m_lastHeartbeat = 0;
		m_lastModeLogKey.clear();
		m_lastAnchorLogKey.clear();
		m_lastWarriorActionLogKey.clear();
		m_lastWarriorFailureLogKey.clear();
		m_lastClericActionLogKey.clear();
		m_lastClericFailureLogKey.clear();
		m_lastMageActionLogKey.clear();
		m_lastMageFailureLogKey.clear();
		m_pendingAutoAttackTargetGuid = 0;
		m_pendingAutoAttackRequestedAt = 0;
		m_lastObservedWarriorCastFailureAt = 0;
		m_lastObservedWarriorCastFailureSpellId = 0;
		m_lastObservedWarriorCastFailureReason = spell_cast_result::CastOkay;
		m_lastObservedClericCastFailureAt = 0;
		m_lastObservedClericCastFailureSpellId = 0;
		m_lastObservedClericCastFailureReason = spell_cast_result::CastOkay;
		m_lastObservedMageCastFailureAt = 0;
		m_lastObservedMageCastFailureSpellId = 0;
		m_lastObservedMageCastFailureReason = spell_cast_result::CastOkay;
		m_lastObservedPartyHealthPercents.clear();
	}

	bool CompanionFollowAction::IsInterruptible() const
	{
		return true;
	}

	CompanionFollowControllerInput CompanionFollowAction::BuildControllerInput(BotContext& context, const GameTime now) const
	{
		CompanionFollowControllerInput input;
		input.now = now;
		input.path = m_path;
		input.state = m_state;
		input.self.position = context.GetPosition();
		input.self.facing = context.GetMovementInfo().facing;
		input.self.timestamp = now;
		input.self.isMoving = context.IsMoving();
		input.self.isValid = context.HasAuthoritativeMovementInfo() || context.GetSelf() != nullptr;

		input.snapshot.selfGuid = context.GetSelectedCharacterGuid();
		input.snapshot.leaderGuid = context.GetPartyLeaderGuid();
		const BotCompanionRole selfRole = DetermineSelfRole(context.GetConfig().characterClass);
		input.snapshot.desiredCombatRole = DetermineDesiredCombatAnchorRole(selfRole);
		input.snapshot.previousState = m_previousCompanionState;

		const BotUnit* selfUnit = context.GetSelf();
		input.snapshot.selfInCombat = selfUnit ? selfUnit->IsInCombat() : false;

		std::unordered_set<uint64> seenGuids;
		const uint32 partyMemberCount = context.GetPartyMemberCount();
		input.snapshot.partyMembers.reserve(partyMemberCount + 2);
		for (uint32 index = 0; index < partyMemberCount; ++index)
		{
			const uint64 guid = context.GetPartyMemberGuid(index);
			if (guid == 0 || !seenGuids.insert(guid).second)
			{
				continue;
			}

			input.snapshot.partyMembers.push_back(BuildMemberSnapshot(
				context,
				guid,
				context.GetPartyMemberName(index),
				input.snapshot.selfGuid,
				input.snapshot.leaderGuid,
				selfRole));
		}

		if (input.snapshot.selfGuid != 0 && seenGuids.find(input.snapshot.selfGuid) == seenGuids.end())
		{
			input.snapshot.partyMembers.push_back(BuildMemberSnapshot(
				context,
				input.snapshot.selfGuid,
				selfUnit ? selfUnit->GetName() : std::string {},
				input.snapshot.selfGuid,
				input.snapshot.leaderGuid,
				selfRole));
			seenGuids.insert(input.snapshot.selfGuid);
		}

		if (input.snapshot.leaderGuid != 0 && seenGuids.find(input.snapshot.leaderGuid) == seenGuids.end())
		{
			const BotUnit* leaderUnit = context.GetUnit(input.snapshot.leaderGuid);
			input.snapshot.partyMembers.push_back(BuildMemberSnapshot(
				context,
				input.snapshot.leaderGuid,
				leaderUnit ? leaderUnit->GetName() : std::string("leader"),
				input.snapshot.selfGuid,
				input.snapshot.leaderGuid,
				selfRole));
		}

		for (const BotCompanionMemberSnapshot& member : input.snapshot.partyMembers)
		{
			if (member.isInCombat)
			{
				input.snapshot.partyInCombat = true;
				break;
			}
		}

		if (!input.self.isValid)
		{
			input.preconditionFailure = CompanionFollowPreconditionFailure::SelfUnavailable;
			return input;
		}

		if (!IsFiniteVector(input.self.position))
		{
			input.preconditionFailure = CompanionFollowPreconditionFailure::InvalidSelfPosition;
			return input;
		}

		if (!context.GetNavService() || !context.GetNavService()->IsReady())
		{
			input.preconditionFailure = CompanionFollowPreconditionFailure::NavUnavailable;
			return input;
		}

		return input;
	}

	void CompanionFollowAction::ApplyDecision(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output)
	{
		switch (output.followDecision.type)
		{
		case BotFollowDecisionType::Hold:
			StopMovement(context);
			return;

		case BotFollowDecisionType::Abort:
			StopMovement(context);
			m_path.points.clear();
			m_state.hasLastRepathAnchorPosition = false;
			return;

		case BotFollowDecisionType::Stuck:
			StopMovement(context);
			m_path.points.clear();
			m_state.hasLastRepathAnchorPosition = false;
			return;

		case BotFollowDecisionType::Repath:
			RequestPath(context, input, output);
			return;

		case BotFollowDecisionType::Advance:
			AdvanceAlongPath(context, input, output);
			return;
		}
	}

	void CompanionFollowAction::RequestPath(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output)
	{
		BotNavService* navService = context.GetNavService();
		if (!navService)
		{
			StopMovement(context);
			m_path.points.clear();
			m_state.hasLastRepathAnchorPosition = false;
			return;
		}

		uint32 mapId = context.GetCurrentMapId();
		const float desiredDistance = (m_controller.GetConfig().holdDistance + m_controller.GetConfig().leaveDistance) * 0.5f;
		const Vector3 pathTarget = ComputeFollowStandOffTarget(
			input.self.position,
			output.companion.anchor.position,
			output.companion.anchor.facing,
			output.companion.anchor.hasFacing,
			desiredDistance);
		if (mapId == 0)
		{
			if (const std::optional<uint32> inferredMapId = navService->InferMapId(input.self.position, pathTarget); inferredMapId.has_value())
			{
				mapId = *inferredMapId;
				context.SetCurrentMapId(mapId);
			}
		}

		const BotPathResult result = navService->FindPath(mapId, input.self.position, pathTarget);
		if (!result.success)
		{
			StopMovement(context);
			m_path.points.clear();
			m_state.hasLastRepathAnchorPosition = false;
			return;
		}

		m_path.points = result.points;
		m_state.hasLastRepathAnchorPosition = true;
		m_state.lastRepathAnchorPosition = output.companion.anchor.position;
		m_state.lastRepathTime = input.now;
		m_state.hasLastDistanceToAnchor = true;
		m_state.lastDistanceToAnchor = output.followDecision.distanceToAnchor;
	}

	void CompanionFollowAction::AdvanceAlongPath(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output)
	{
		if (!output.followDecision.hasSteeringTarget)
		{
			StopMovement(context);
			return;
		}

		MovementInfo movement = context.GetMovementInfo();
		if (!m_state.isMoving && (movement.movementFlags & movement_flags::Falling))
		{
			context.SendLandedPacket();
			movement = context.GetMovementInfo();
		}

		if (output.followDecision.hasDesiredFacing)
		{
			movement.facing = output.followDecision.desiredFacing;
		}

		if (!m_state.isMoving)
		{
			movement.movementFlags |= movement_flags::Forward;
			movement.timestamp = input.now;
			context.SendMovementUpdate(game::client_realm_packet::MoveStartForward, movement);
			m_state.isMoving = true;
			m_lastHeartbeat = input.now;
			m_state.hasLastDistanceToAnchor = true;
			m_state.lastDistanceToAnchor = output.followDecision.distanceToAnchor;
			return;
		}

		if (input.now - m_lastHeartbeat < kHeartbeatIntervalMs)
		{
			return;
		}

		const Vector3 currentPosition = movement.position;
		Vector3 delta = output.followDecision.steeringTarget - currentPosition;
		delta.y = 0.0f;
		const float distance = delta.GetLength();
		const float elapsedSeconds = static_cast<float>(input.now - m_lastHeartbeat) / 1000.0f;
		if (distance > 0.001f && movement.IsChangingPosition())
		{
			const Vector3 direction = delta / distance;
			const float stepDistance = std::min(m_moveSpeed * elapsedSeconds, distance);
			movement.position = currentPosition + (direction * stepDistance);
			if (stepDistance > 0.0f)
			{
				m_state.hasLastProgressPosition = true;
				m_state.lastProgressPosition = movement.position;
				m_state.lastProgressTime = input.now;
			}
		}

		movement.timestamp = input.now;
		context.SendMovementUpdate(game::client_realm_packet::MoveHeartBeat, movement);
		m_lastHeartbeat = input.now;
		m_state.isMoving = true;
		m_state.hasLastDistanceToAnchor = true;
		m_state.lastDistanceToAnchor = output.followDecision.distanceToAnchor;
	}

	void CompanionFollowAction::StopMovement(BotContext& context)
	{
		if (!m_state.isMoving && !context.IsMoving())
		{
			return;
		}

		MovementInfo movement = context.GetMovementInfo();
		movement.movementFlags &= ~movement_flags::Moving;
		movement.timestamp = context.GetServerTime();
		context.SendMovementUpdate(game::client_realm_packet::MoveStop, movement);
		m_state.isMoving = false;
	}

	std::string CompanionFollowAction::BuildDecisionDetails(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output) const
	{
		std::ostringstream details;
		details << "mode_reason=" << output.companion.modeReason
			<< " anchor_reason=" << output.companion.anchorReason
			<< " follow_decision=" << FollowDecisionTypeToString(output.followDecision.type)
			<< " follow_reason=" << output.followDecision.reason
			<< " desired_role=" << mmo::ToString(input.snapshot.desiredCombatRole)
			<< " leader_guid=" << input.snapshot.leaderGuid
			<< " map_id=" << context.GetCurrentMapId()
			<< " path_points=" << m_path.points.size();
		if (output.companion.hasAnchor)
		{
			details << " anchor_kind=" << AnchorKindToString(output.companion.anchor.kind)
				<< " anchor_guid=" << output.companion.anchor.guid;
			if (output.companion.anchor.hasPosition)
			{
				details << " anchor_x=" << output.companion.anchor.position.x
					<< " anchor_y=" << output.companion.anchor.position.y
					<< " anchor_z=" << output.companion.anchor.position.z;
			}
		}
		else
		{
			details << " anchor_kind=none anchor_guid=0";
		}
		if (output.followDecision.hasSteeringTarget)
		{
			details << " steer_x=" << output.followDecision.steeringTarget.x
				<< " steer_y=" << output.followDecision.steeringTarget.y
				<< " steer_z=" << output.followDecision.steeringTarget.z
				<< " waypoint_index=" << output.followDecision.steeringWaypointIndex;
		}
		return details.str();
	}

	void CompanionFollowAction::LogCompanionModeOnce(const CompanionFollowControllerOutput& output, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << mmo::ToString(output.companion.mode) << '|' << output.companion.modeReason;
		const std::string key = keyBuilder.str();
		if (m_lastModeLogKey == key)
		{
			return;
		}

		m_lastModeLogKey = key;
		ILOG("companion mode=" << mmo::ToString(output.companion.mode) << " reason=" << output.companion.modeReason << ' ' << details);
	}

	void CompanionFollowAction::LogAnchorDecisionOnce(const CompanionFollowControllerOutput& output, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << output.companion.anchorReason << '|'
			<< FollowDecisionTypeToString(output.followDecision.type) << '|'
			<< output.followDecision.reason << '|'
			<< (output.companion.hasAnchor ? output.companion.anchor.guid : 0) << '|'
			<< AnchorKindToString(output.companion.anchor.kind);
		const std::string key = keyBuilder.str();
		if (m_lastAnchorLogKey == key)
		{
			return;
		}

		m_lastAnchorLogKey = key;
		if (output.followDecision.type == BotFollowDecisionType::Abort || output.followDecision.type == BotFollowDecisionType::Stuck)
		{
			ELOG("anchor decision=" << output.companion.anchorReason << " follow=" << FollowDecisionTypeToString(output.followDecision.type) << " reason=" << output.followDecision.reason << ' ' << details);
		}
		else
		{
			ILOG("anchor decision=" << output.companion.anchorReason << " follow=" << FollowDecisionTypeToString(output.followDecision.type) << " reason=" << output.followDecision.reason << ' ' << details);
		}
	}

	void CompanionFollowAction::UpdatePreviousCompanionState(const CompanionFollowControllerOutput& output)
	{
		m_previousCompanionState.mode = output.companion.mode;
		m_previousCompanionState.anchorGuid = output.companion.hasAnchor ? output.companion.anchor.guid : 0;
		m_previousCompanionState.anchorKind = output.companion.hasAnchor ? output.companion.anchor.kind : BotFollowAnchorKind::Custom;
	}

	void CompanionFollowAction::RefreshWarriorCapabilities(BotContext& context)
	{
		if (context.GetConfig().characterClass != kWarriorClassId)
		{
			m_hasWarriorCapabilities = false;
			m_warriorCapabilityIssue = "warrior_runtime_disabled";
			return;
		}

		if (m_hasWarriorCapabilities)
		{
			return;
		}

		const BotNavService* navService = context.GetNavService();
		if (!navService || !navService->IsReady() || navService->GetProject() == nullptr)
		{
			m_hasWarriorCapabilities = false;
			m_warriorCapabilityIssue = "capabilities_project_unavailable";
			return;
		}

		m_warriorCapabilities = ResolveWarriorCapabilities(*navService->GetProject());
		m_hasWarriorCapabilities = m_warriorCapabilities.resolved;
		m_warriorCapabilityIssue = m_hasWarriorCapabilities
			? std::string {}
			: FirstIssueOrFallback(m_warriorCapabilities, "capabilities_unresolved");
	}

	void CompanionFollowAction::ExecuteWarriorRuntime(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output)
	{
		if (context.GetConfig().characterClass != kWarriorClassId)
		{
			return;
		}

		if (m_pendingAutoAttackTargetGuid != 0 && context.IsAutoAttacking() && context.GetAutoAttackTarget() == m_pendingAutoAttackTargetGuid)
		{
			m_pendingAutoAttackTargetGuid = 0;
			m_pendingAutoAttackRequestedAt = 0;
		}

		if (IsRuntimeHardBlocked(input.preconditionFailure))
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " warrior_decision=skipped"
				<< " warrior_reason=follow_runtime_blocked";
			LogWarriorFailureOnce("follow_runtime_blocked", details.str());
			return;
		}

		RefreshWarriorCapabilities(context);
		if (!m_hasWarriorCapabilities)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " warrior_decision=hold"
				<< " capability_issue=" << m_warriorCapabilityIssue;
			LogWarriorFailureOnce("capabilities_unresolved", details.str());
			return;
		}

		const BotUnit* self = context.GetSelf();
		if (!self)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " warrior_decision=hold"
				<< " capability_issue=self_unavailable";
			LogWarriorFailureOnce("self_unavailable", details.str());
			return;
		}

		const BotUnit::CastState lastCastState = context.GetLastCastState();
		if (lastCastState.status == BotUnit::CastState::Status::Pending || lastCastState.status == BotUnit::CastState::Status::Started)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " warrior_decision=hold"
				<< " runtime_issue=cast_in_flight"
				<< " spell_id=" << lastCastState.spellId;
			LogWarriorFailureOnce("cast_in_flight", details.str());
			return;
		}

		if (lastCastState.status == BotUnit::CastState::Status::Failed
			&& lastCastState.updatedAtMs > 0
			&& input.now >= lastCastState.updatedAtMs
			&& input.now - lastCastState.updatedAtMs < kCastFailureBackoffMs)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " warrior_decision=hold"
				<< " runtime_issue=cast_failure_backoff"
				<< " spell_id=" << lastCastState.spellId
				<< " failure_code=" << static_cast<uint32>(lastCastState.failureReason);
			LogWarriorFailureOnce("cast_failure_backoff", details.str());
			return;
		}

		const std::vector<uint32> knownSpellIds = context.GetKnownSpellIds();
		const BotWarriorCapabilities knownCapabilities = FilterKnownWarriorCapabilities(m_warriorCapabilities, knownSpellIds);
		const WarriorTargetCandidate target = SelectWarriorTarget(context, output);
		const std::vector<BotUnit::CooldownState> activeCooldowns = context.GetActiveSpellCooldowns();
		const std::vector<BotUnit::AuraState> visibleAuras = context.GetVisibleAuras();
		const std::string lastSpellIssue = context.GetLastSpellStateIssue();
		std::unordered_set<uint32> blockedCooldownSpellIds;
		blockedCooldownSpellIds.reserve(activeCooldowns.size());
		for (const BotUnit::CooldownState& cooldown : activeCooldowns)
		{
			blockedCooldownSpellIds.insert(cooldown.spellId);
		}

		std::unordered_set<uint32> activeAuraSpellIds;
		activeAuraSpellIds.reserve(visibleAuras.size());
		for (const BotUnit::AuraState& aura : visibleAuras)
		{
			activeAuraSpellIds.insert(aura.spellId);
		}

		BotWarriorDecisionInput warriorInput;
		warriorInput.capabilities = &knownCapabilities;
		warriorInput.companionMode = output.companion.mode;
		warriorInput.spellbookReady = !knownSpellIds.empty()
			&& !StartsWith(lastSpellIssue, "initial_spells_")
			&& !StartsWith(lastSpellIssue, "learned_spell_")
			&& !StartsWith(lastSpellIssue, "unlearned_spell_");
		warriorInput.cooldownsReady = !StartsWith(lastSpellIssue, "spell_cooldown_");
		warriorInput.powerReady = context.GetSelfPowerType() == knownCapabilities.powerType && context.GetSelfMaxPower() > 0;
		warriorInput.selfInCombat = self->IsInCombat();
		warriorInput.hasValidTarget = target.unit != nullptr;
		warriorInput.targetAlive = target.unit && target.unit->IsAlive();
		warriorInput.targetHostile = target.unit && (target.unit->IsHostileTo(*self) || target.unit->IsAttackableBy(*self));
		warriorInput.targetTargetingSelf = target.unit && target.unit->GetTargetGuid() == self->GetGuid();
		warriorInput.targetGuid = target.unit ? target.unit->GetGuid() : 0;
		warriorInput.targetDistance = target.unit ? context.GetDistanceToUnit(target.unit->GetGuid()) : 0.0f;
		warriorInput.rage = context.GetSelfPower();
		warriorInput.selfHealthPercent = self->GetHealthPercent();
		warriorInput.autoAttackActive = context.IsAutoAttacking();
		warriorInput.autoAttackTargetGuid = context.GetAutoAttackTarget();
		warriorInput.activeAuraSpellIds = std::move(activeAuraSpellIds);
		warriorInput.cooldownBlockedSpellIds = std::move(blockedCooldownSpellIds);

		const BotWarriorDecision decision = m_warriorController.Evaluate(warriorInput);
		std::ostringstream details;
		details << BuildDecisionDetails(context, input, output)
			<< " warrior_decision=" << ToString(decision.type)
			<< " warrior_reason=" << decision.reason
			<< " target_source=" << target.source
			<< " target_guid=" << warriorInput.targetGuid
			<< " target_distance=" << warriorInput.targetDistance
			<< " spellbook_ready=" << (warriorInput.spellbookReady ? 1 : 0)
			<< " cooldowns_ready=" << (warriorInput.cooldownsReady ? 1 : 0)
			<< " power_ready=" << (warriorInput.powerReady ? 1 : 0)
			<< " rage=" << warriorInput.rage
			<< " auto_attack_active=" << (warriorInput.autoAttackActive ? 1 : 0)
			<< " auto_attack_target=" << warriorInput.autoAttackTargetGuid
			<< " capability_count=" << knownCapabilities.GetResolvedCategoryCount()
			<< " cooldown_blocked=" << JoinSpellIds(warriorInput.cooldownBlockedSpellIds);
		if (!lastSpellIssue.empty())
		{
			details << " runtime_issue=" << lastSpellIssue;
		}
		if (!m_warriorCapabilityIssue.empty())
		{
			details << " capability_issue=" << m_warriorCapabilityIssue;
		}

		switch (decision.type)
		{
		case BotWarriorDecisionType::Hold:
			LogWarriorFailureOnce(decision.reason, details.str());
			return;

		case BotWarriorDecisionType::EnsureAutoAttack:
			if (decision.autoAttackTargetGuid == 0)
			{
				LogWarriorFailureOnce("invalid_auto_attack_target", details.str());
				return;
			}

			if (m_pendingAutoAttackTargetGuid == decision.autoAttackTargetGuid
				&& input.now >= m_pendingAutoAttackRequestedAt
				&& input.now - m_pendingAutoAttackRequestedAt < kAutoAttackRetryDelayMs)
			{
				LogWarriorFailureOnce("auto_attack_pending", details.str());
				return;
			}

			context.StartAutoAttack(decision.autoAttackTargetGuid);
			m_pendingAutoAttackTargetGuid = decision.autoAttackTargetGuid;
			m_pendingAutoAttackRequestedAt = input.now;
			LogWarriorActionOnce(ToString(decision.type), decision.reason, details.str());
			return;

		case BotWarriorDecisionType::CastSpell:
		{
			SpellTargetMap targetMap;
			if (decision.castOnSelf)
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Self);
			}
			else
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);
				targetMap.SetUnitTarget(decision.castTargetGuid);
			}

			if (!context.CastSpell(decision.spellId, targetMap))
			{
				std::ostringstream rejectedDetails;
				rejectedDetails << details.str()
					<< " spell_id=" << decision.spellId
					<< " cast_target=" << decision.castTargetGuid
					<< " cast_on_self=" << (decision.castOnSelf ? 1 : 0)
					<< " runtime_issue=" << context.GetLastSpellStateIssue();
				LogWarriorFailureOnce("cast_rejected", rejectedDetails.str());
				return;
			}

			std::ostringstream castDetails;
			castDetails << details.str()
				<< " spell_id=" << decision.spellId
				<< " cast_target=" << decision.castTargetGuid
				<< " cast_on_self=" << (decision.castOnSelf ? 1 : 0);
			LogWarriorActionOnce(ToString(decision.type), decision.reason, castDetails.str());
			return;
		}
		}
	}

	void CompanionFollowAction::ObserveWarriorCastFailure(BotContext& context, const CompanionFollowControllerOutput& output)
	{
		if (context.GetConfig().characterClass != kWarriorClassId)
		{
			return;
		}

		const BotUnit::CastState lastCastState = context.GetLastCastState();
		if (lastCastState.status != BotUnit::CastState::Status::Failed || lastCastState.updatedAtMs == 0)
		{
			return;
		}

		if (m_lastObservedWarriorCastFailureAt == lastCastState.updatedAtMs
			&& m_lastObservedWarriorCastFailureSpellId == lastCastState.spellId
			&& m_lastObservedWarriorCastFailureReason == lastCastState.failureReason)
		{
			return;
		}

		m_lastObservedWarriorCastFailureAt = lastCastState.updatedAtMs;
		m_lastObservedWarriorCastFailureSpellId = lastCastState.spellId;
		m_lastObservedWarriorCastFailureReason = lastCastState.failureReason;

		std::ostringstream details;
		details << "mode=" << ToString(output.companion.mode)
			<< " mode_reason=" << output.companion.modeReason
			<< " anchor_reason=" << output.companion.anchorReason
			<< " spell_id=" << lastCastState.spellId
			<< " failure_code=" << static_cast<uint32>(lastCastState.failureReason)
			<< " updated_at=" << lastCastState.updatedAtMs;
		LogWarriorFailureOnce("cast_failed", details.str());
	}

	void CompanionFollowAction::LogWarriorActionOnce(std::string_view action, std::string_view reason, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << action << '|' << reason << '|' << details;
		const std::string key = keyBuilder.str();
		if (m_lastWarriorActionLogKey == key)
		{
			return;
		}

		m_lastWarriorActionLogKey = key;
		ILOG("warrior action=" << action << " reason=" << reason << ' ' << details);
	}

	void CompanionFollowAction::LogWarriorFailureOnce(std::string_view reason, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << reason << '|' << details;
		const std::string key = keyBuilder.str();
		if (m_lastWarriorFailureLogKey == key)
		{
			return;
		}

		m_lastWarriorFailureLogKey = key;
		WLOG("warrior failure=" << reason << ' ' << details);
	}

	void CompanionFollowAction::RefreshClericCapabilities(BotContext& context)
	{
		if (context.GetConfig().characterClass != kPriestClassId)
		{
			m_hasClericCapabilities = false;
			m_clericCapabilityIssue = "cleric_runtime_disabled";
			return;
		}

		if (m_hasClericCapabilities)
		{
			return;
		}

		const BotNavService* navService = context.GetNavService();
		if (!navService || !navService->IsReady() || navService->GetProject() == nullptr)
		{
			m_hasClericCapabilities = false;
			m_clericCapabilityIssue = "capabilities_project_unavailable";
			return;
		}

		m_clericCapabilities = ResolveClericCapabilities(*navService->GetProject());
		m_hasClericCapabilities = m_clericCapabilities.resolved;
		m_clericCapabilityIssue = m_hasClericCapabilities
			? std::string {}
			: FirstIssueOrFallback(m_clericCapabilities, "capabilities_unresolved");
	}

	void CompanionFollowAction::ExecuteClericRuntime(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output)
	{
		if (context.GetConfig().characterClass != kPriestClassId)
		{
			return;
		}

		if (IsRuntimeHardBlocked(input.preconditionFailure))
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " cleric_decision=skipped"
				<< " cleric_reason=follow_runtime_blocked";
			LogClericFailureOnce("follow_runtime_blocked", details.str());
			return;
		}

		RefreshClericCapabilities(context);
		if (!m_hasClericCapabilities)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " cleric_decision=hold"
				<< " capability_issue=" << m_clericCapabilityIssue;
			LogClericFailureOnce("capabilities_unresolved", details.str());
			return;
		}

		const BotUnit* self = context.GetSelf();
		if (!self)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " cleric_decision=hold"
				<< " capability_issue=self_unavailable";
			LogClericFailureOnce("self_unavailable", details.str());
			return;
		}

		const BotUnit::CastState lastCastState = context.GetLastCastState();
		if (lastCastState.status == BotUnit::CastState::Status::Pending || lastCastState.status == BotUnit::CastState::Status::Started)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " cleric_decision=hold"
				<< " runtime_issue=cast_in_flight"
				<< " spell_id=" << lastCastState.spellId;
			LogClericFailureOnce("cast_in_flight", details.str());
			return;
		}

		if (lastCastState.status == BotUnit::CastState::Status::Failed
			&& lastCastState.updatedAtMs > 0
			&& input.now >= lastCastState.updatedAtMs
			&& input.now - lastCastState.updatedAtMs < kCastFailureBackoffMs)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " cleric_decision=hold"
				<< " runtime_issue=cast_failure_backoff"
				<< " spell_id=" << lastCastState.spellId
				<< " failure_code=" << static_cast<uint32>(lastCastState.failureReason);
			LogClericFailureOnce("cast_failure_backoff", details.str());
			return;
		}

		const std::vector<uint32> knownSpellIds = context.GetKnownSpellIds();
		BotClericCapabilities knownCapabilities = FilterKnownClericCapabilities(m_clericCapabilities, knownSpellIds);
		knownCapabilities.supportBuff.reset();
		knownCapabilities.resolved = knownCapabilities.powerType == power_type::Mana
			&& knownCapabilities.emergencyHeal.has_value()
			&& knownCapabilities.efficientHeal.has_value()
			&& knownCapabilities.damageFiller.has_value();
		if (!knownCapabilities.resolved)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " cleric_decision=hold"
				<< " capability_issue=known_spells_incomplete";
			LogClericFailureOnce("capabilities_unresolved", details.str());
			return;
		}

		const ClericHostileTargetCandidate hostileTarget = SelectClericHostileTarget(context, output);
		const std::vector<BotUnit::CooldownState> activeCooldowns = context.GetActiveSpellCooldowns();
		const std::vector<BotUnit::AuraState> visibleAuras = context.GetVisibleAuras();
		const std::string lastSpellIssue = context.GetLastSpellStateIssue();
		std::unordered_set<uint32> blockedCooldownSpellIds;
		blockedCooldownSpellIds.reserve(activeCooldowns.size());
		for (const BotUnit::CooldownState& cooldown : activeCooldowns)
		{
			blockedCooldownSpellIds.insert(cooldown.spellId);
		}

		BotClericDecisionInput clericInput;
		clericInput.capabilities = &knownCapabilities;
		clericInput.companionMode = (output.companion.mode == BotCompanionMode::Hold || output.companion.mode == BotCompanionMode::Regroup)
			? ((input.snapshot.selfInCombat || input.snapshot.partyInCombat) ? BotCompanionMode::CombatAnchor : output.companion.mode)
			: output.companion.mode;
		clericInput.spellbookReady = !knownSpellIds.empty()
			&& !StartsWith(lastSpellIssue, "initial_spells_")
			&& !StartsWith(lastSpellIssue, "learned_spell_")
			&& !StartsWith(lastSpellIssue, "unlearned_spell_");
		clericInput.cooldownsReady = !StartsWith(lastSpellIssue, "spell_cooldown_");
		clericInput.powerReady = context.GetSelfPowerType() == knownCapabilities.powerType && context.GetSelfMaxPower() > 0;
		clericInput.hasSelf = true;
		clericInput.self.guid = self->GetGuid();
		clericInput.self.role = BotCompanionRole::Healer;
		clericInput.self.isAlive = self->IsAlive();
		clericInput.self.isAware = true;
		clericInput.self.isInCombat = self->IsInCombat();
		clericInput.self.healthPercent = self->GetHealthPercent();
		clericInput.self.distanceToSelf = 0.0f;
		clericInput.self.targetGuid = self->GetTargetGuid();
		for (const BotUnit::AuraState& aura : visibleAuras)
		{
			clericInput.self.activeAuraSpellIds.insert(aura.spellId);
		}
		clericInput.mana = context.GetSelfPower();
		clericInput.maxMana = context.GetSelfMaxPower();
		clericInput.hasHostileTarget = hostileTarget.unit != nullptr;
		clericInput.hostileTargetAlive = hostileTarget.unit && hostileTarget.unit->IsAlive();
		clericInput.hostileTargetHostile = hostileTarget.unit && (hostileTarget.unit->IsHostileTo(*self) || hostileTarget.unit->IsAttackableBy(*self));
		clericInput.hostileTargetGuid = hostileTarget.unit ? hostileTarget.unit->GetGuid() : 0;
		clericInput.hostileTargetDistance = hostileTarget.unit ? context.GetDistanceToUnit(hostileTarget.unit->GetGuid()) : 0.0f;
		clericInput.cooldownBlockedSpellIds = blockedCooldownSpellIds;

		std::unordered_set<uint64> seenGuids;
		uint64 focusTargetGuid = 0;
		float focusTargetHealthPercent = 1.1f;
		auto appendPartyMember = [&](const uint64 guid, const BotCompanionRole role)
		{
			if (guid == 0 || !seenGuids.insert(guid).second || guid == clericInput.self.guid)
			{
				return;
			}

			BotClericUnitSnapshot member;
			member.guid = guid;
			member.role = role;
			if (const BotUnit* unit = context.GetUnit(guid))
			{
				member.isAware = true;
				member.isAlive = unit->IsAlive();
				member.isInCombat = unit->IsInCombat();
				member.healthPercent = unit->GetHealthPercent();
				member.distanceToSelf = context.GetDistanceToUnit(guid);
				member.targetGuid = unit->GetTargetGuid();
				if (IsValidHealthPercent(member.healthPercent))
				{
					m_lastObservedPartyHealthPercents[guid] = member.healthPercent;
				}
			}
			else
			{
				member.isAware = false;
				member.isAlive = true;
				member.isInCombat = false;
				const auto it = m_lastObservedPartyHealthPercents.find(guid);
				member.healthPercent = it != m_lastObservedPartyHealthPercents.end() ? it->second : 1.0f;
				member.distanceToSelf = 0.0f;
				member.targetGuid = 0;
			}

			if (IsValidHealthPercent(member.healthPercent) && member.healthPercent < focusTargetHealthPercent)
			{
				focusTargetHealthPercent = member.healthPercent;
				focusTargetGuid = member.guid;
			}

			clericInput.partyMembers.push_back(std::move(member));
		};

		const uint32 partyMemberCount = context.GetPartyMemberCount();
		for (uint32 index = 0; index < partyMemberCount; ++index)
		{
			const uint64 guid = context.GetPartyMemberGuid(index);
			appendPartyMember(guid, guid == input.snapshot.leaderGuid ? BotCompanionRole::Tank : BotCompanionRole::None);
		}
		appendPartyMember(input.snapshot.leaderGuid, BotCompanionRole::Tank);

		const BotClericDecision decision = m_clericController.Evaluate(clericInput);
		const uint64 targetGuid = decision.castOnSelf
			? clericInput.self.guid
			: (decision.castTargetGuid != 0 ? decision.castTargetGuid : focusTargetGuid);

		std::ostringstream details;
		details << BuildDecisionDetails(context, input, output)
			<< " cleric_decision=" << ToString(decision.type)
			<< " cleric_reason=" << decision.reason
			<< " target_source=" << hostileTarget.source
			<< " target_guid=" << targetGuid
			<< " hostile_target_guid=" << clericInput.hostileTargetGuid
			<< " hostile_target_distance=" << clericInput.hostileTargetDistance
			<< " spellbook_ready=" << (clericInput.spellbookReady ? 1 : 0)
			<< " cooldowns_ready=" << (clericInput.cooldownsReady ? 1 : 0)
			<< " power_ready=" << (clericInput.powerReady ? 1 : 0)
			<< " self_health_pct=" << clericInput.self.healthPercent
			<< " mana=" << clericInput.mana
			<< " max_mana=" << clericInput.maxMana
			<< " party_members=" << clericInput.partyMembers.size()
			<< " cooldown_blocked=" << JoinSpellIds(clericInput.cooldownBlockedSpellIds);
		if (!lastSpellIssue.empty())
		{
			details << " runtime_issue=" << lastSpellIssue;
		}
		if (!m_clericCapabilityIssue.empty())
		{
			details << " capability_issue=" << m_clericCapabilityIssue;
		}
		if (!m_clericCapabilities.supportBuff.has_value())
		{
			details << " support_buff_state=unavailable";
		}

		switch (decision.type)
		{
		case BotClericDecisionType::Hold:
			LogClericFailureOnce(decision.reason, details.str());
			return;

		case BotClericDecisionType::CastSpell:
		{
			SpellTargetMap targetMap;
			if (decision.castOnSelf)
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Self);
			}
			else
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);
				targetMap.SetUnitTarget(decision.castTargetGuid);
			}

			if (!context.CastSpell(decision.spellId, targetMap))
			{
				std::ostringstream rejectedDetails;
				rejectedDetails << details.str()
					<< " spell_id=" << decision.spellId
					<< " cast_target=" << decision.castTargetGuid
					<< " cast_on_self=" << (decision.castOnSelf ? 1 : 0)
					<< " runtime_issue=" << context.GetLastSpellStateIssue();
				LogClericFailureOnce("cast_rejected", rejectedDetails.str());
				return;
			}

			std::ostringstream castDetails;
			castDetails << details.str()
				<< " spell_id=" << decision.spellId
				<< " cast_target=" << decision.castTargetGuid
				<< " cast_on_self=" << (decision.castOnSelf ? 1 : 0);
			LogClericActionOnce(ToString(decision.type), decision.reason, castDetails.str());
			return;
		}
		}
	}

	void CompanionFollowAction::ObserveClericCastFailure(BotContext& context, const CompanionFollowControllerOutput& output)
	{
		if (context.GetConfig().characterClass != kPriestClassId)
		{
			return;
		}

		const BotUnit::CastState lastCastState = context.GetLastCastState();
		if (lastCastState.status != BotUnit::CastState::Status::Failed || lastCastState.updatedAtMs == 0)
		{
			return;
		}

		if (m_lastObservedClericCastFailureAt == lastCastState.updatedAtMs
			&& m_lastObservedClericCastFailureSpellId == lastCastState.spellId
			&& m_lastObservedClericCastFailureReason == lastCastState.failureReason)
		{
			return;
		}

		m_lastObservedClericCastFailureAt = lastCastState.updatedAtMs;
		m_lastObservedClericCastFailureSpellId = lastCastState.spellId;
		m_lastObservedClericCastFailureReason = lastCastState.failureReason;

		std::ostringstream details;
		details << "mode=" << ToString(output.companion.mode)
			<< " mode_reason=" << output.companion.modeReason
			<< " anchor_reason=" << output.companion.anchorReason
			<< " spell_id=" << lastCastState.spellId
			<< " failure_code=" << static_cast<uint32>(lastCastState.failureReason)
			<< " updated_at=" << lastCastState.updatedAtMs;
		LogClericFailureOnce("cast_failed", details.str());
	}

	void CompanionFollowAction::LogClericActionOnce(std::string_view action, std::string_view reason, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << action << '|' << reason << '|' << details;
		const std::string key = keyBuilder.str();
		if (m_lastClericActionLogKey == key)
		{
			return;
		}

		m_lastClericActionLogKey = key;
		ILOG("cleric action=" << action << " reason=" << reason << ' ' << details);
	}

	void CompanionFollowAction::LogClericFailureOnce(std::string_view reason, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << reason << '|' << details;
		const std::string key = keyBuilder.str();
		if (m_lastClericFailureLogKey == key)
		{
			return;
		}

		m_lastClericFailureLogKey = key;
		WLOG("cleric failure=" << reason << ' ' << details);
	}

	void CompanionFollowAction::RefreshMageCapabilities(BotContext& context)
	{
		if (context.GetConfig().characterClass != kMageClassId)
		{
			m_hasMageCapabilities = false;
			m_mageCapabilityIssue = "mage_runtime_disabled";
			return;
		}

		if (m_hasMageCapabilities)
		{
			return;
		}

		const BotNavService* navService = context.GetNavService();
		if (!navService || !navService->IsReady() || navService->GetProject() == nullptr)
		{
			m_hasMageCapabilities = false;
			m_mageCapabilityIssue = "capabilities_project_unavailable";
			return;
		}

		m_mageCapabilities = ResolveMageCapabilities(*navService->GetProject());
		m_hasMageCapabilities = m_mageCapabilities.powerType == power_type::Mana
			&& m_mageCapabilities.primaryNuke.has_value();
		m_mageCapabilityIssue = m_hasMageCapabilities
			? std::string {}
			: FirstIssueOrFallback(m_mageCapabilities, "capabilities_unresolved");
	}

	void CompanionFollowAction::ExecuteMageRuntime(BotContext& context, const CompanionFollowControllerInput& input, const CompanionFollowControllerOutput& output)
	{
		if (context.GetConfig().characterClass != kMageClassId)
		{
			return;
		}

		if (IsRuntimeHardBlocked(input.preconditionFailure))
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " mage_decision=skipped"
				<< " mage_reason=follow_runtime_blocked";
			LogMageFailureOnce("follow_runtime_blocked", details.str());
			return;
		}

		RefreshMageCapabilities(context);
		if (!m_hasMageCapabilities)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " mage_decision=hold"
				<< " capability_issue=" << m_mageCapabilityIssue;
			LogMageFailureOnce("capabilities_unresolved", details.str());
			return;
		}

		const BotUnit* self = context.GetSelf();
		if (!self)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " mage_decision=hold"
				<< " capability_issue=self_unavailable";
			LogMageFailureOnce("self_unavailable", details.str());
			return;
		}

		const BotUnit::CastState lastCastState = context.GetLastCastState();
		if (lastCastState.status == BotUnit::CastState::Status::Pending || lastCastState.status == BotUnit::CastState::Status::Started)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " mage_decision=hold"
				<< " runtime_issue=cast_in_flight"
				<< " spell_id=" << lastCastState.spellId;
			LogMageFailureOnce("cast_in_flight", details.str());
			return;
		}

		if (lastCastState.status == BotUnit::CastState::Status::Failed
			&& lastCastState.updatedAtMs > 0
			&& input.now >= lastCastState.updatedAtMs
			&& input.now - lastCastState.updatedAtMs < kCastFailureBackoffMs)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " mage_decision=hold"
				<< " runtime_issue=cast_failure_backoff"
				<< " spell_id=" << lastCastState.spellId
				<< " failure_code=" << static_cast<uint32>(lastCastState.failureReason);
			LogMageFailureOnce("cast_failure_backoff", details.str());
			return;
		}

		const std::vector<uint32> knownSpellIds = context.GetKnownSpellIds();
		const BotMageCapabilities knownCapabilities = FilterKnownMageCapabilities(m_mageCapabilities, knownSpellIds);
		if (!knownCapabilities.resolved)
		{
			std::ostringstream details;
			details << BuildDecisionDetails(context, input, output)
				<< " mage_decision=hold"
				<< " capability_issue=known_spells_incomplete";
			LogMageFailureOnce("capabilities_unresolved", details.str());
			return;
		}

		const MageTargetCandidate target = SelectMagePrimaryTarget(context, output);
		const std::vector<BotUnit::CooldownState> activeCooldowns = context.GetActiveSpellCooldowns();
		const std::vector<BotUnit::AuraState> visibleAuras = context.GetVisibleAuras();
		const std::string lastSpellIssue = context.GetLastSpellStateIssue();
		std::unordered_set<uint32> blockedCooldownSpellIds;
		blockedCooldownSpellIds.reserve(activeCooldowns.size());
		for (const BotUnit::CooldownState& cooldown : activeCooldowns)
		{
			blockedCooldownSpellIds.insert(cooldown.spellId);
		}

		BotMageDecisionInput mageInput;
		mageInput.capabilities = &knownCapabilities;
		mageInput.companionMode = output.companion.mode;
		mageInput.spellbookReady = !knownSpellIds.empty()
			&& !StartsWith(lastSpellIssue, "initial_spells_")
			&& !StartsWith(lastSpellIssue, "learned_spell_")
			&& !StartsWith(lastSpellIssue, "unlearned_spell_");
		mageInput.cooldownsReady = !StartsWith(lastSpellIssue, "spell_cooldown_");
		mageInput.powerReady = context.GetSelfPowerType() == knownCapabilities.powerType && context.GetSelfMaxPower() > 0;
		mageInput.hasSelf = true;
		mageInput.self.guid = self->GetGuid();
		mageInput.self.isAlive = self->IsAlive();
		mageInput.self.healthPercent = self->GetHealthPercent();
		mageInput.self.mana = context.GetSelfPower();
		mageInput.self.maxMana = context.GetSelfMaxPower();
		for (const BotUnit::AuraState& aura : visibleAuras)
		{
			mageInput.self.activeAuraSpellIds.insert(aura.spellId);
		}
		mageInput.hasPrimaryTarget = target.unit != nullptr;
		if (target.unit)
		{
			mageInput.primaryTarget = BuildMageHostileSnapshot(context, *self, *target.unit);
		}
		mageInput.nearbyHostiles = BuildMageNearbyHostiles(context, *self, target);
		mageInput.cooldownBlockedSpellIds = std::move(blockedCooldownSpellIds);

		const BotMageDecision decision = m_mageController.Evaluate(mageInput);
		const BotMageResolvedSpell* resolvedSpell = knownCapabilities.FindSpell(decision.spellId);
		std::ostringstream details;
		details << BuildDecisionDetails(context, input, output)
			<< " mage_decision=" << ToString(decision.type)
			<< " mage_reason=" << decision.reason
			<< " target_source=" << target.source
			<< " target_guid=" << (target.unit ? target.unit->GetGuid() : 0)
			<< " primary_target_distance=" << (target.unit ? context.GetDistanceToUnit(target.unit->GetGuid()) : 0.0f)
			<< " nearby_hostiles=" << mageInput.nearbyHostiles.size()
			<< " spellbook_ready=" << (mageInput.spellbookReady ? 1 : 0)
			<< " cooldowns_ready=" << (mageInput.cooldownsReady ? 1 : 0)
			<< " power_ready=" << (mageInput.powerReady ? 1 : 0)
			<< " self_health_pct=" << mageInput.self.healthPercent
			<< " mana=" << mageInput.self.mana
			<< " max_mana=" << mageInput.self.maxMana
			<< " capability_count=" << knownCapabilities.GetResolvedCategoryCount()
			<< " cooldown_blocked=" << JoinSpellIds(mageInput.cooldownBlockedSpellIds);
		if (!lastSpellIssue.empty())
		{
			details << " runtime_issue=" << lastSpellIssue;
		}
		if (!m_mageCapabilities.issues.empty())
		{
			details << " capability_issue=" << FirstIssueOrFallback(m_mageCapabilities, "capabilities_unresolved");
		}

		switch (decision.type)
		{
		case BotMageDecisionType::Hold:
			LogMageFailureOnce(decision.reason, details.str());
			return;

		case BotMageDecisionType::CastSpell:
		case BotMageDecisionType::EmergencySpacing:
		{
			SpellTargetMap targetMap;
			if (decision.castOnSelf)
			{
				targetMap.SetTargetMap(spell_cast_target_flags::Self);
			}
			else
			{
				if (decision.castTargetGuid == 0)
				{
					LogMageFailureOnce("invalid_cast_target", details.str());
					return;
				}
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);
				targetMap.SetUnitTarget(decision.castTargetGuid);
			}

			if (!context.CastSpell(decision.spellId, targetMap))
			{
				std::ostringstream rejectedDetails;
				rejectedDetails << details.str()
					<< " spell_id=" << decision.spellId
					<< " spell_name=" << (resolvedSpell ? resolvedSpell->name : std::string("unknown"))
					<< " cast_target=" << decision.castTargetGuid
					<< " cast_on_self=" << (decision.castOnSelf ? 1 : 0)
					<< " runtime_issue=" << context.GetLastSpellStateIssue();
				LogMageFailureOnce("cast_rejected", rejectedDetails.str());
				return;
			}

			std::ostringstream castDetails;
			castDetails << details.str()
				<< " spell_id=" << decision.spellId
				<< " spell_name=" << (resolvedSpell ? resolvedSpell->name : std::string("unknown"))
				<< " cast_target=" << decision.castTargetGuid
				<< " cast_on_self=" << (decision.castOnSelf ? 1 : 0);
			LogMageActionOnce(ToString(decision.type), decision.reason, castDetails.str());
			return;
		}
		}
	}

	void CompanionFollowAction::ObserveMageCastFailure(BotContext& context, const CompanionFollowControllerOutput& output)
	{
		if (context.GetConfig().characterClass != kMageClassId)
		{
			return;
		}

		const BotUnit::CastState lastCastState = context.GetLastCastState();
		if (lastCastState.status != BotUnit::CastState::Status::Failed || lastCastState.updatedAtMs == 0)
		{
			return;
		}

		if (m_lastObservedMageCastFailureAt == lastCastState.updatedAtMs
			&& m_lastObservedMageCastFailureSpellId == lastCastState.spellId
			&& m_lastObservedMageCastFailureReason == lastCastState.failureReason)
		{
			return;
		}

		m_lastObservedMageCastFailureAt = lastCastState.updatedAtMs;
		m_lastObservedMageCastFailureSpellId = lastCastState.spellId;
		m_lastObservedMageCastFailureReason = lastCastState.failureReason;

		std::ostringstream details;
		details << "mode=" << ToString(output.companion.mode)
			<< " mode_reason=" << output.companion.modeReason
			<< " anchor_reason=" << output.companion.anchorReason
			<< " spell_id=" << lastCastState.spellId
			<< " failure_code=" << static_cast<uint32>(lastCastState.failureReason)
			<< " updated_at=" << lastCastState.updatedAtMs;
		LogMageFailureOnce("cast_failed", details.str());
	}

	void CompanionFollowAction::LogMageActionOnce(std::string_view action, std::string_view reason, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << action << '|' << reason << '|' << details;
		const std::string key = keyBuilder.str();
		if (m_lastMageActionLogKey == key)
		{
			return;
		}

		m_lastMageActionLogKey = key;
		ILOG("mage action=" << action << " reason=" << reason << ' ' << details);
	}

	void CompanionFollowAction::LogMageFailureOnce(std::string_view reason, const std::string& details)
	{
		std::ostringstream keyBuilder;
		keyBuilder << reason << '|' << details;
		const std::string key = keyBuilder.str();
		if (m_lastMageFailureLogKey == key)
		{
			return;
		}

		m_lastMageFailureLogKey = key;
		WLOG("mage failure=" << reason << ' ' << details);
	}
}
