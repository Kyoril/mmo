// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_mage_capabilities.h"

#include "game/aura.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace
{
	using namespace mmo;

	std::string ToLower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c)
		{
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	bool Contains(const std::string_view haystack, const std::string_view needle)
	{
		return haystack.find(needle) != std::string_view::npos;
	}

	bool HasSpellAttribute(const proto::SpellEntry& spell, const uint32 mask)
	{
		return spell.attributes_size() > 0 && (spell.attributes(0) & mask) != 0;
	}

	bool IsPassiveSpell(const proto::SpellEntry& spell)
	{
		return HasSpellAttribute(spell, spell_attributes::Passive);
	}

	bool IsHiddenSpell(const proto::SpellEntry& spell)
	{
		return HasSpellAttribute(spell, spell_attributes::HiddenClientSide);
	}

	bool HasDamageEffect(const proto::SpellEntry& spell)
	{
		return proto::SpellHasEffect(spell, spell_effects::SchoolDamage)
			|| proto::SpellHasEffect(spell, spell_effects::WeaponDamage)
			|| proto::SpellHasEffect(spell, spell_effects::WeaponDamageNoSchool)
			|| proto::SpellHasEffect(spell, spell_effects::HealthLeech);
	}

	bool HasAuraType(const proto::SpellEntry& spell, const uint32 aura)
	{
		for (int i = 0; i < spell.effects_size(); ++i)
		{
			const auto& effect = spell.effects(i);
			if (!effect.has_aura())
			{
				continue;
			}

			if (effect.aura() == aura)
			{
				return true;
			}
		}

		return false;
	}

	bool HasControlEffect(const proto::SpellEntry& spell)
	{
		if (proto::SpellHasEffect(spell, spell_effects::InterruptSpellCast))
		{
			return true;
		}

		return HasAuraType(spell, aura_type::ModRoot)
			|| HasAuraType(spell, aura_type::ModSleep)
			|| HasAuraType(spell, aura_type::ModStun)
			|| HasAuraType(spell, aura_type::ModFear);
	}

	bool HasSpacingEffect(const proto::SpellEntry& spell)
	{
		return HasControlEffect(spell)
			|| HasAuraType(spell, aura_type::ModDecreaseSpeed)
			|| (spell.has_mechanic() && (spell.mechanic() == spell_mechanic::Root
				|| spell.mechanic() == spell_mechanic::Freeze
				|| spell.mechanic() == spell_mechanic::Sleep
				|| spell.mechanic() == spell_mechanic::Fear
				|| spell.mechanic() == spell_mechanic::Stun
				|| spell.mechanic() == spell_mechanic::Polymorph
				|| spell.mechanic() == spell_mechanic::Silence
				|| spell.mechanic() == spell_mechanic::Snare));
	}

	bool IsNegativeControlSpell(const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
	{
		const bool inferredTarget = resolved.requiresTarget || (!resolved.targetMetadataPresent && resolved.hasExplicitRange);
		if (!inferredTarget)
		{
			return false;
		}

		if (resolved.positiveMetadataPresent && resolved.positive)
		{
			return false;
		}

		return HasControlEffect(spell)
			|| (spell.has_mechanic() && (spell.mechanic() == spell_mechanic::Root
				|| spell.mechanic() == spell_mechanic::Freeze
				|| spell.mechanic() == spell_mechanic::Sleep
				|| spell.mechanic() == spell_mechanic::Fear
				|| spell.mechanic() == spell_mechanic::Stun
				|| spell.mechanic() == spell_mechanic::Polymorph
				|| spell.mechanic() == spell_mechanic::Silence))
			|| Contains(text, "sleep")
			|| Contains(text, "polymorph")
			|| Contains(text, "interrupt")
			|| Contains(text, "silence");
	}

	int ScoreMageClassCandidate(const proto::ClassEntry& entry, const proto::Project& project)
	{
		int score = entry.spells_size() * 5;
		if (entry.powertype() == proto::ClassEntry::MANA)
		{
			score += 100;
		}

		const std::string identity = ToLower(entry.name() + " " + entry.internalname());
		if (Contains(identity, "mage"))
		{
			score += 2000;
		}
		if (Contains(identity, "wizard") || Contains(identity, "sorc") || Contains(identity, "arcane"))
		{
			score += 400;
		}

		for (int index = 0; index < entry.spells_size(); ++index)
		{
			const proto::ClassSpell& classSpell = entry.spells(index);
			const proto::SpellEntry* spell = project.spells.getById(classSpell.spell());
			if (!spell)
			{
				continue;
			}

			const std::string text = ToLower(spell->name() + std::string(" ") + (spell->has_description() ? spell->description() : std::string {}));
			if (Contains(text, "frostbolt"))
			{
				score += 300;
			}
			if (Contains(text, "fireball"))
			{
				score += 240;
			}
			if (Contains(text, "fire blast"))
			{
				score += 180;
			}
			if (Contains(text, "frost nova"))
			{
				score += 200;
			}
			if (Contains(text, "arcane intellect") || Contains(text, "frost armor"))
			{
				score += 80;
			}
			if (HasDamageEffect(*spell))
			{
				score += 35;
			}
			if (HasSpacingEffect(*spell) || IsNegativeControlSpell(*spell, BotMageResolvedSpell {}, text))
			{
				score += 30;
			}
		}

		return score;
	}

	const proto::ClassEntry* FindMageClass(const proto::Project& project)
	{
		const auto& classes = project.classes.getTemplates();
		const proto::ClassEntry* best = nullptr;
		int bestScore = 0;
		for (int i = 0; i < classes.entry_size(); ++i)
		{
			const auto& entry = classes.entry(i);
			const int score = ScoreMageClassCandidate(entry, project);
			if (score > bestScore)
			{
				best = &entry;
				bestScore = score;
			}
		}

		return best;
	}

	uint32 ResolveRootSpellId(const proto::Project& project, const proto::SpellEntry& spell)
	{
		if (spell.has_baseid() && spell.baseid() != 0)
		{
			return spell.baseid();
		}

		std::set<uint32> visited;
		const proto::SpellEntry* current = &spell;
		while (current && current->has_prevspell() && current->prevspell() != 0)
		{
			if (!visited.insert(current->id()).second)
			{
				break;
			}

			const proto::SpellEntry* previous = project.spells.getById(current->prevspell());
			if (!previous)
			{
				break;
			}

			current = previous;
		}

		return current ? current->id() : spell.id();
	}

	BotMageResolvedSpell BuildResolvedSpell(
		const proto::Project& project,
		const uint32 learnLevel,
		const proto::SpellEntry& spell,
		std::vector<std::string>& issues)
	{
		BotMageResolvedSpell result;
		result.spellId = spell.id();
		result.rootSpellId = ResolveRootSpellId(project, spell);
		result.rank = spell.has_rank() ? spell.rank() : 0;
		result.classLearnLevel = learnLevel;
		result.powerCost = spell.has_cost() ? spell.cost() : 0;
		result.cooldownMs = spell.has_cooldown() ? static_cast<GameTime>(spell.cooldown()) : 0;
		result.castTimeMs = spell.has_casttime() ? static_cast<GameTime>(spell.casttime()) : 0;
		result.targetMap = spell.has_targetmap() ? spell.targetmap() : 0;
		result.targetMetadataPresent = spell.has_targetmap();
		result.requiresTarget = result.targetMetadataPresent && (result.targetMap & spell_cast_target_flags::Unit) != 0;
		result.positiveMetadataPresent = spell.has_positive();
		result.positive = spell.has_positive() && spell.positive() != 0;
		result.passive = IsPassiveSpell(spell);
		result.hidden = IsHiddenSpell(spell);
		result.school = spell.has_spellschool() ? spell.spellschool() : 0;
		result.name = spell.name();
		result.description = spell.has_description() ? spell.description() : std::string {};

		if (spell.has_minrange())
		{
			result.minRange = spell.minrange();
		}

		if (spell.has_maxrange() && spell.maxrange() > 0.0f)
		{
			result.maxRange = spell.maxrange();
			result.hasExplicitRange = true;
		}
		else if (spell.has_rangetype() && spell.rangetype() != 0)
		{
			if (const proto::RangeType* range = project.ranges.getById(spell.rangetype()))
			{
				result.maxRange = range->range();
				result.hasExplicitRange = result.maxRange > 0.0f;
			}
			else
			{
				issues.emplace_back("range_missing:" + std::to_string(spell.id()));
			}
		}
		else if (result.requiresTarget)
		{
			issues.emplace_back("range_missing:" + std::to_string(spell.id()));
		}

		if (HasDamageEffect(spell) && !result.positiveMetadataPresent)
		{
			issues.emplace_back("positive_missing:" + std::to_string(spell.id()));
		}
		if ((HasDamageEffect(spell) || HasControlEffect(spell) || (spell.has_mechanic() && spell.mechanic() != spell_mechanic::None))
			&& !result.targetMetadataPresent && result.hasExplicitRange)
		{
			issues.emplace_back("target_missing:" + std::to_string(spell.id()));
		}

		return result;
	}

	int ComputeRankScore(const BotMageResolvedSpell& spell, const proto::Project& project)
	{
		int score = static_cast<int>(spell.rank) * 1000 + static_cast<int>(spell.classLearnLevel) * 100 + static_cast<int>(spell.powerCost);
		if (const proto::SpellEntry* entry = project.spells.getById(spell.spellId))
		{
			if (entry->has_spelllevel())
			{
				score += entry->spelllevel() * 10;
			}
			if (entry->has_nextspell() && entry->nextspell() == 0)
			{
				score += 5;
			}
		}
		return score;
	}

	bool HasMetadataConflict(const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, std::vector<std::string>& issues)
	{
		const std::string text = ToLower(resolved.name + " " + resolved.description);
		const bool harmful = HasDamageEffect(spell)
			|| HasSpacingEffect(spell)
			|| IsNegativeControlSpell(spell, resolved, text)
			|| Contains(text, "freeze")
			|| Contains(text, "sleep")
			|| Contains(text, "interrupt");
		const bool supportive = Contains(text, "armor")
			|| Contains(text, "intellect")
			|| Contains(text, "invisibility")
			|| Contains(text, "shield")
			|| Contains(text, "buff")
			|| (!harmful && (proto::SpellHasEffect(spell, spell_effects::ApplyAura)
				|| proto::SpellHasEffect(spell, spell_effects::ApplyAreaAura)
				|| proto::SpellHasEffect(spell, spell_effects::PersistentAreaAura)));

		if (harmful && resolved.positiveMetadataPresent && resolved.positive)
		{
			issues.emplace_back("positive_conflict:" + std::to_string(spell.id()));
			return true;
		}

		if (supportive && resolved.positiveMetadataPresent && !resolved.positive)
		{
			issues.emplace_back("positive_conflict:" + std::to_string(spell.id()));
			return true;
		}

		return false;
	}

	int ScorePrimaryNuke(const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
	{
		const bool inferredTarget = resolved.requiresTarget || (!resolved.targetMetadataPresent && resolved.hasExplicitRange);
		if ((resolved.positiveMetadataPresent && resolved.positive) || !inferredTarget || !resolved.hasExplicitRange || !HasDamageEffect(spell))
		{
			return 0;
		}
		if (resolved.castTimeMs == 0)
		{
			return 0;
		}

		int score = 100;
		if (Contains(text, "frostbolt"))
		{
			score += 260;
		}
		if (Contains(text, "fireball"))
		{
			score += 220;
		}
		if (Contains(text, "arcane missiles") || Contains(text, "arcane bolt") || Contains(text, "bolt"))
		{
			score += 100;
		}
		if (resolved.school == spell_school::Frost)
		{
			score += 70;
		}
		if (resolved.school == spell_school::Fire)
		{
			score += 50;
		}
		if (resolved.school == spell_school::Arcane)
		{
			score += 40;
		}
		if (HasAuraType(spell, aura_type::ModDecreaseSpeed) || spell.mechanic() == spell_mechanic::Snare)
		{
			score += 80;
		}
		if (resolved.hasExplicitRange && resolved.maxRange >= 20.0f)
		{
			score += 30;
		}
		if (resolved.cooldownMs == 0)
		{
			score += 10;
		}
		return score;
	}

	int ScoreInstantFallback(const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
	{
		const bool inferredTarget = resolved.requiresTarget || (!resolved.targetMetadataPresent && resolved.hasExplicitRange);
		if ((resolved.positiveMetadataPresent && resolved.positive) || !inferredTarget || !resolved.hasExplicitRange || !HasDamageEffect(spell))
		{
			return 0;
		}
		if (resolved.castTimeMs != 0)
		{
			return 0;
		}
		if (resolved.powerCost == 0 && resolved.cooldownMs == 0)
		{
			return 0;
		}
		if (resolved.school == spell_school::Normal && !Contains(text, "blast") && !Contains(text, "arcane") && !Contains(text, "fire") && !Contains(text, "frost"))
		{
			return 0;
		}

		int score = 100;
		if (Contains(text, "fire blast"))
		{
			score += 280;
		}
		if (Contains(text, "blast"))
		{
			score += 120;
		}
		if (Contains(text, "instant"))
		{
			score += 40;
		}
		if (resolved.cooldownMs > 0)
		{
			score += 20;
		}
		if (resolved.hasExplicitRange && resolved.maxRange >= 20.0f)
		{
			score += 20;
		}
		return score;
	}

	int ScoreControl(const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
	{
		if (!IsNegativeControlSpell(spell, resolved, text))
		{
			return 0;
		}

		int score = 100;
		if (Contains(text, "sleep") || spell.mechanic() == spell_mechanic::Sleep)
		{
			score += 140;
		}
		if (Contains(text, "polymorph") || spell.mechanic() == spell_mechanic::Polymorph)
		{
			score += 130;
		}
		if (Contains(text, "interrupt") || proto::SpellHasEffect(spell, spell_effects::InterruptSpellCast))
		{
			score += 120;
		}
		if (spell.has_mechanic() && (spell.mechanic() == spell_mechanic::Silence || spell.mechanic() == spell_mechanic::Freeze || spell.mechanic() == spell_mechanic::Root))
		{
			score += 70;
		}
		if (resolved.hasExplicitRange && resolved.maxRange >= 20.0f)
		{
			score += 20;
		}
		return score;
	}

	int ScoreSelfBuffEscape(const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
	{
		if (resolved.positiveMetadataPresent && !resolved.positive)
		{
			return 0;
		}

		const bool selfOrUtility = !resolved.requiresTarget || (!resolved.hasExplicitRange && !resolved.targetMetadataPresent);
		if (!selfOrUtility)
		{
			return 0;
		}

		int score = 0;
		if (resolved.positive)
		{
			score += 80;
		}
		if (Contains(text, "invisibility"))
		{
			score += 220;
		}
		if (Contains(text, "blink") || Contains(text, "teleport"))
		{
			score += 180;
		}
		if (Contains(text, "frost armor") || Contains(text, "ice armor") || Contains(text, "mana shield") || Contains(text, "shield"))
		{
			score += 170;
		}
		if (Contains(text, "arcane intellect") || Contains(text, "intellect"))
		{
			score += 110;
		}
		if (proto::SpellHasEffect(spell, spell_effects::ApplyAura) || proto::SpellHasEffect(spell, spell_effects::ApplyAreaAura) || proto::SpellHasEffect(spell, spell_effects::PersistentAreaAura))
		{
			score += 30;
		}
		return score;
	}

	int ScoreEmergencySpacing(const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
	{
		int score = 0;
		const bool selfCentered = !resolved.requiresTarget && !resolved.hasExplicitRange;
		const bool explicitEscape = Contains(text, "frost nova")
			|| Contains(text, "nova")
			|| Contains(text, "blink")
			|| Contains(text, "teleport")
			|| Contains(text, "invisibility")
			|| Contains(text, "escape");
		const bool strongControl = HasControlEffect(spell)
			|| (spell.has_mechanic() && (spell.mechanic() == spell_mechanic::Freeze
				|| spell.mechanic() == spell_mechanic::Root
				|| spell.mechanic() == spell_mechanic::Fear
				|| spell.mechanic() == spell_mechanic::Sleep
				|| spell.mechanic() == spell_mechanic::Stun));
		const bool closeRangeControl = resolved.requiresTarget && resolved.hasExplicitRange && resolved.maxRange <= 10.0f && (strongControl || HasAuraType(spell, aura_type::ModDecreaseSpeed));
		if (!selfCentered && !explicitEscape && !closeRangeControl)
		{
			return 0;
		}
		if (selfCentered && !explicitEscape && !strongControl)
		{
			return 0;
		}
		if (selfCentered && (strongControl || explicitEscape))
		{
			score += 100;
		}

		if (Contains(text, "frost nova"))
		{
			score += 320;
		}
		if (Contains(text, "nova"))
		{
			score += 160;
		}
		if (Contains(text, "blink") || Contains(text, "teleport"))
		{
			score += 220;
		}
		if (Contains(text, "invisibility"))
		{
			score += 140;
		}
		if (spell.has_mechanic() && (spell.mechanic() == spell_mechanic::Freeze || spell.mechanic() == spell_mechanic::Root || spell.mechanic() == spell_mechanic::Fear || spell.mechanic() == spell_mechanic::Sleep))
		{
			score += 100;
		}
		if (HasAuraType(spell, aura_type::ModRoot) || HasAuraType(spell, aura_type::ModDecreaseSpeed))
		{
			score += 80;
		}

		return score;
	}

	template <typename Accessor>
	std::optional<BotMageResolvedSpell> PickBestSpell(const std::vector<BotMageResolvedSpell>& spells, const proto::Project& project, Accessor&& scoreAccessor)
	{
		const BotMageResolvedSpell* best = nullptr;
		int bestScore = 0;
		for (const BotMageResolvedSpell& resolved : spells)
		{
			const proto::SpellEntry* spell = project.spells.getById(resolved.spellId);
			if (!spell)
			{
				continue;
			}

			const std::string text = ToLower(resolved.name + " " + resolved.description);
			const int score = scoreAccessor(*spell, resolved, text);
			if (score > bestScore)
			{
				bestScore = score;
				best = &resolved;
			}
		}

		return bestScore > 0 && best ? std::optional<BotMageResolvedSpell>(*best) : std::nullopt;
	}
}

namespace mmo
{
	bool BotMageResolvedSpell::IsInRange(const float distance) const noexcept
	{
		if (!requiresTarget)
		{
			return true;
		}

		if (!hasExplicitRange)
		{
			return false;
		}

		return distance + 0.001f >= minRange && distance <= maxRange + 0.001f;
	}

	size_t BotMageCapabilities::GetResolvedCategoryCount() const noexcept
	{
		size_t count = 0;
		count += primaryNuke.has_value() ? 1u : 0u;
		count += instantFallback.has_value() ? 1u : 0u;
		count += control.has_value() ? 1u : 0u;
		count += selfBuffEscape.has_value() ? 1u : 0u;
		count += emergencySpacing.has_value() ? 1u : 0u;
		return count;
	}

	const BotMageResolvedSpell* BotMageCapabilities::FindSpell(const uint32 spellId) const noexcept
	{
		for (const BotMageResolvedSpell& spell : spells)
		{
			if (spell.spellId == spellId)
			{
				return &spell;
			}
		}

		return nullptr;
	}

	BotMageCapabilities ResolveMageCapabilities(const proto::Project& project)
	{
		BotMageCapabilities result;
		const proto::ClassEntry* mage = FindMageClass(project);
		if (!mage)
		{
			result.issues.emplace_back("mage_class_missing");
			return result;
		}

		result.classId = mage->id();
		result.spellFamily = mage->spellfamily();
		if (mage->powertype() == proto::ClassEntry::MANA)
		{
			result.powerType = power_type::Mana;
		}
		else
		{
			result.issues.emplace_back("mage_power_type_unexpected");
		}

		std::map<uint32, std::vector<BotMageResolvedSpell>> chains;
		std::set<uint32> candidateSpellIds;
		for (int index = 0; index < mage->spells_size(); ++index)
		{
			const proto::ClassSpell& classSpell = mage->spells(index);
			const proto::SpellEntry* spell = project.spells.getById(classSpell.spell());
			if (!spell)
			{
				result.issues.emplace_back("spell_missing:" + std::to_string(classSpell.spell()));
				continue;
			}

			candidateSpellIds.insert(spell->id());
			BotMageResolvedSpell resolved = BuildResolvedSpell(project, classSpell.level(), *spell, result.issues);
			if (resolved.passive || resolved.hidden)
			{
				continue;
			}
			if (HasMetadataConflict(*spell, resolved, result.issues))
			{
				continue;
			}

			chains[resolved.rootSpellId].push_back(std::move(resolved));
		}

		if (result.spellFamily != 0)
		{
			const auto& allSpells = project.spells.getTemplates();
			const uint32 classMaskBit = result.classId > 0 ? (1u << (result.classId - 1u)) : 0u;
			for (int index = 0; index < allSpells.entry_size(); ++index)
			{
				const auto& spell = allSpells.entry(index);
				if (!spell.has_family() || spell.family() != result.spellFamily)
				{
					continue;
				}
				if (spell.has_classmask() && spell.classmask() != 0 && classMaskBit != 0 && (spell.classmask() & classMaskBit) == 0)
				{
					continue;
				}
				if (!candidateSpellIds.insert(spell.id()).second)
				{
					continue;
				}

				const uint32 learnLevel = spell.has_spelllevel() && spell.spelllevel() > 0
					? static_cast<uint32>(spell.spelllevel())
					: (spell.has_baselevel() && spell.baselevel() > 0 ? static_cast<uint32>(spell.baselevel()) : 0u);
				BotMageResolvedSpell resolved = BuildResolvedSpell(project, learnLevel, spell, result.issues);
				if (resolved.passive || resolved.hidden)
				{
					continue;
				}
				if (HasMetadataConflict(spell, resolved, result.issues))
				{
					continue;
				}

				chains[resolved.rootSpellId].push_back(std::move(resolved));
			}
		}

		for (auto& [rootId, chain] : chains)
		{
			if (chain.empty())
			{
				continue;
			}

			auto bestIt = std::max_element(chain.begin(), chain.end(), [&](const BotMageResolvedSpell& left, const BotMageResolvedSpell& right)
			{
				return ComputeRankScore(left, project) < ComputeRankScore(right, project);
			});

			const int bestScore = ComputeRankScore(*bestIt, project);
			const size_t tieCount = static_cast<size_t>(std::count_if(chain.begin(), chain.end(), [&](const BotMageResolvedSpell& spell)
			{
				return ComputeRankScore(spell, project) == bestScore;
			}));
			if (tieCount > 1)
			{
				result.issues.emplace_back("rank_chain_ambiguous:" + std::to_string(rootId));
			}

			bestIt->rootSpellId = rootId;
			result.spells.push_back(*bestIt);
		}

		result.primaryNuke = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
		{
			return ScorePrimaryNuke(spell, resolved, text);
		});
		if (result.primaryNuke)
		{
			result.primaryNuke->kind = BotMageCapabilityKind::PrimaryNuke;
		}

		result.instantFallback = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
		{
			return ScoreInstantFallback(spell, resolved, text);
		});
		if (result.instantFallback)
		{
			result.instantFallback->kind = BotMageCapabilityKind::InstantFallback;
		}

		result.control = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
		{
			return ScoreControl(spell, resolved, text);
		});
		if (result.control)
		{
			result.control->kind = BotMageCapabilityKind::Control;
		}

		result.selfBuffEscape = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
		{
			return ScoreSelfBuffEscape(spell, resolved, text);
		});
		if (result.selfBuffEscape)
		{
			result.selfBuffEscape->kind = BotMageCapabilityKind::SelfBuffEscape;
		}

		result.emergencySpacing = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotMageResolvedSpell& resolved, const std::string& text)
		{
			return ScoreEmergencySpacing(spell, resolved, text);
		});
		if (result.emergencySpacing)
		{
			result.emergencySpacing->kind = BotMageCapabilityKind::EmergencySpacing;
		}

		if (result.spells.empty())
		{
			result.issues.emplace_back("mage_spellbook_empty");
		}
		if (!result.primaryNuke)
		{
			result.issues.emplace_back("primary_nuke_missing");
		}
		if (!result.instantFallback)
		{
			result.issues.emplace_back("instant_fallback_missing");
		}
		if (!result.control)
		{
			result.issues.emplace_back("control_missing");
		}
		if (!result.selfBuffEscape)
		{
			result.issues.emplace_back("self_buff_escape_missing");
		}
		if (!result.emergencySpacing)
		{
			result.issues.emplace_back("emergency_spacing_missing");
		}

		result.resolved = !result.spells.empty()
			&& result.powerType == power_type::Mana
			&& result.primaryNuke.has_value()
			&& result.emergencySpacing.has_value();
		return result;
	}

	const char* ToString(const BotMageCapabilityKind kind) noexcept
	{
		switch (kind)
		{
		case BotMageCapabilityKind::PrimaryNuke:
			return "primary_nuke";
		case BotMageCapabilityKind::InstantFallback:
			return "instant_fallback";
		case BotMageCapabilityKind::Control:
			return "control";
		case BotMageCapabilityKind::SelfBuffEscape:
			return "self_buff_escape";
		case BotMageCapabilityKind::EmergencySpacing:
			return "emergency_spacing";
		case BotMageCapabilityKind::None:
		default:
			return "none";
		}
	}
}
