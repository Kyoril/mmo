// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_cleric_capabilities.h"

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

	bool HasHealEffect(const proto::SpellEntry& spell)
	{
		return proto::SpellHasEffect(spell, spell_effects::Heal)
			|| proto::SpellHasEffect(spell, spell_effects::HealPct);
	}

	bool HasDamageEffect(const proto::SpellEntry& spell)
	{
		return proto::SpellHasEffect(spell, spell_effects::SchoolDamage)
			|| proto::SpellHasEffect(spell, spell_effects::WeaponDamage)
			|| proto::SpellHasEffect(spell, spell_effects::WeaponDamageNoSchool)
			|| proto::SpellHasEffect(spell, spell_effects::HealthLeech);
	}

	bool HasAuraEffect(const proto::SpellEntry& spell)
	{
		return proto::SpellHasEffect(spell, spell_effects::ApplyAura)
			|| proto::SpellHasEffect(spell, spell_effects::ApplyAreaAura)
			|| proto::SpellHasEffect(spell, spell_effects::PersistentAreaAura);
	}

	bool HasAreaAuraEffect(const proto::SpellEntry& spell)
	{
		return proto::SpellHasEffect(spell, spell_effects::ApplyAreaAura)
			|| proto::SpellHasEffect(spell, spell_effects::PersistentAreaAura);
	}

	int ScoreClericClassCandidate(const proto::ClassEntry& entry, const proto::Project& project)
	{
		int score = entry.spells_size() * 5;
		if (entry.powertype() == proto::ClassEntry::MANA)
		{
			score += 100;
		}

		const std::string identity = ToLower(entry.name() + " " + entry.internalname());
		if (Contains(identity, "cleric"))
		{
			score += 2000;
		}
		if (Contains(identity, "priest") || Contains(identity, "healer"))
		{
			score += 500;
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
			if (HasHealEffect(*spell) || Contains(text, "heal") || Contains(text, "healing"))
			{
				score += 60;
			}
			if (HasAreaAuraEffect(*spell) || Contains(text, "aura") || Contains(text, "group members"))
			{
				score += 40;
			}
			if (HasDamageEffect(*spell) || Contains(text, "smite"))
			{
				score += 20;
			}
		}

		return score;
	}

	const proto::ClassEntry* FindClericClass(const proto::Project& project)
	{
		const auto& classes = project.classes.getTemplates();
		const proto::ClassEntry* best = nullptr;
		int bestScore = 0;
		for (int i = 0; i < classes.entry_size(); ++i)
		{
			const auto& entry = classes.entry(i);
			const int score = ScoreClericClassCandidate(entry, project);
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

	BotClericResolvedSpell BuildResolvedSpell(
		const proto::Project& project,
		const uint32 learnLevel,
		const proto::SpellEntry& spell,
		std::vector<std::string>& issues)
	{
		BotClericResolvedSpell result;
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

		if (HasHealEffect(spell) && !result.positiveMetadataPresent)
		{
			issues.emplace_back("positive_missing:" + std::to_string(spell.id()));
		}
		if ((HasHealEffect(spell) || HasDamageEffect(spell)) && !result.targetMetadataPresent && result.hasExplicitRange)
		{
			issues.emplace_back("target_missing:" + std::to_string(spell.id()));
		}

		return result;
	}

	int ComputeRankScore(const BotClericResolvedSpell& spell, const proto::Project& project)
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

	bool HasMetadataConflict(const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, std::vector<std::string>& issues)
	{
		const bool hasHeal = HasHealEffect(spell);
		const bool hasDamage = HasDamageEffect(spell);
		if (hasHeal && hasDamage)
		{
			issues.emplace_back("effect_conflict:" + std::to_string(spell.id()));
			return true;
		}

		if (hasHeal && resolved.positiveMetadataPresent && !resolved.positive)
		{
			issues.emplace_back("positive_conflict:" + std::to_string(spell.id()));
			return true;
		}

		if (hasDamage && resolved.positiveMetadataPresent && resolved.positive)
		{
			issues.emplace_back("positive_conflict:" + std::to_string(spell.id()));
			return true;
		}

		return false;
	}

	int ScoreEmergencyHeal(const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
	{
		if ((resolved.positiveMetadataPresent && !resolved.positive) || HasDamageEffect(spell) || (resolved.requiresTarget && !resolved.hasExplicitRange))
		{
			return 0;
		}

		const bool looksLikeDirectHeal = HasHealEffect(spell)
			|| Contains(text, "healing light")
			|| Contains(text, " heals ")
			|| Contains(text, "restore your health")
			|| Contains(text, "restores your health");
		if (!looksLikeDirectHeal)
		{
			return 0;
		}

		int score = 100;
		if (HasHealEffect(spell))
		{
			score += 120;
		}
		if (Contains(text, "healing light"))
		{
			score += 80;
		}
		if (resolved.requiresTarget)
		{
			score += 10;
		}
		if (resolved.castTimeMs > 0 && resolved.castTimeMs <= 2500)
		{
			score += 30;
		}
		if (resolved.hasExplicitRange && resolved.maxRange >= 20.0f)
		{
			score += 20;
		}
		score += std::min<int>(static_cast<int>(resolved.powerCost / 2), 30);
		return score;
	}

	int ScoreEfficientHeal(const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
	{
		if ((resolved.positiveMetadataPresent && !resolved.positive) || HasDamageEffect(spell) || (resolved.requiresTarget && !resolved.hasExplicitRange))
		{
			return 0;
		}

		const bool looksLikeDirectHeal = HasHealEffect(spell)
			|| Contains(text, "healing light")
			|| Contains(text, " heals ")
			|| Contains(text, "restore your health")
			|| Contains(text, "restores your health");
		if (!looksLikeDirectHeal)
		{
			return 0;
		}

		int score = 100;
		if (HasHealEffect(spell))
		{
			score += 120;
		}
		if (Contains(text, "healing light"))
		{
			score += 80;
		}
		if (resolved.requiresTarget)
		{
			score += 10;
		}
		if (resolved.hasExplicitRange && resolved.maxRange >= 20.0f)
		{
			score += 20;
		}
		score += std::max(0, 120 - static_cast<int>(resolved.powerCost));
		if (resolved.castTimeMs >= 1500 && resolved.castTimeMs <= 3500)
		{
			score += 20;
		}
		return score;
	}

	int ScoreSupportAura(const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
	{
		if ((resolved.positiveMetadataPresent && !resolved.positive) || HasDamageEffect(spell))
		{
			return 0;
		}

		int score = 0;
		if (HasAreaAuraEffect(spell))
		{
			score += 180;
		}
		if (Contains(text, "aura"))
		{
			score += 120;
		}
		if (Contains(text, "group members"))
		{
			score += 100;
		}
		if (!resolved.requiresTarget)
		{
			score += 20;
		}
		if (Contains(text, "received healing") || Contains(text, "armor"))
		{
			score += 20;
		}
		return score;
	}

	int ScoreSupportBuff(const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
	{
		if ((resolved.positiveMetadataPresent && !resolved.positive) || HasDamageEffect(spell))
		{
			return 0;
		}

		if (Contains(text, "aura") && Contains(text, "group members"))
		{
			return 0;
		}

		if (Contains(text, "damage taken") || Contains(text, "shield"))
		{
			return 0;
		}

		int score = 0;
		if (HasAuraEffect(spell))
		{
			score += 40;
		}
		if (Contains(text, "bless"))
		{
			score += 140;
		}
		if (Contains(text, "stamina"))
		{
			score += 80;
		}
		if (Contains(text, "faith"))
		{
			score += 40;
		}
		if (Contains(text, "damage and healing done"))
		{
			score += 80;
		}
		if (resolved.requiresTarget)
		{
			score += 20;
		}
		return score;
	}

	int ScoreDefensive(const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
	{
		if ((resolved.positiveMetadataPresent && !resolved.positive) || HasDamageEffect(spell))
		{
			return 0;
		}

		int score = 0;
		if (spell.has_mechanic() && spell.mechanic() == spell_mechanic::Shield)
		{
			score += 100;
		}
		if (Contains(text, "shield"))
		{
			score += 140;
		}
		if (Contains(text, "damage taken") || Contains(text, "reducing physical damage"))
		{
			score += 120;
		}
		if (!resolved.requiresTarget)
		{
			score += 20;
		}
		return score;
	}

	int ScoreDamageFiller(const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
	{
		const bool inferredTarget = resolved.requiresTarget || (!resolved.targetMetadataPresent && resolved.hasExplicitRange);
		if ((resolved.positiveMetadataPresent && resolved.positive) || !inferredTarget || !resolved.hasExplicitRange || HasHealEffect(spell))
		{
			return 0;
		}

		int score = 0;
		if (HasDamageEffect(spell))
		{
			score += 160;
		}
		if (Contains(text, "smite"))
		{
			score += 140;
		}
		if (Contains(text, "holy damage") || Contains(text, "damage"))
		{
			score += 80;
		}
		if (resolved.maxRange >= 20.0f)
		{
			score += 20;
		}
		if (resolved.powerCost > 0)
		{
			score += 10;
		}
		return score;
	}

	template <typename Accessor>
	std::optional<BotClericResolvedSpell> PickBestSpell(const std::vector<BotClericResolvedSpell>& spells, const proto::Project& project, Accessor&& scoreAccessor)
	{
		const BotClericResolvedSpell* best = nullptr;
		int bestScore = 0;
		for (const BotClericResolvedSpell& resolved : spells)
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

		return bestScore > 0 && best ? std::optional<BotClericResolvedSpell>(*best) : std::nullopt;
	}
}

namespace mmo
{
	bool BotClericResolvedSpell::IsInRange(const float distance) const noexcept
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

	size_t BotClericCapabilities::GetResolvedCategoryCount() const noexcept
	{
		size_t count = 0;
		count += emergencyHeal.has_value() ? 1u : 0u;
		count += efficientHeal.has_value() ? 1u : 0u;
		count += supportAura.has_value() ? 1u : 0u;
		count += supportBuff.has_value() ? 1u : 0u;
		count += defensive.has_value() ? 1u : 0u;
		count += damageFiller.has_value() ? 1u : 0u;
		return count;
	}

	const BotClericResolvedSpell* BotClericCapabilities::FindSpell(const uint32 spellId) const noexcept
	{
		for (const BotClericResolvedSpell& spell : spells)
		{
			if (spell.spellId == spellId)
			{
				return &spell;
			}
		}

		return nullptr;
	}

	BotClericCapabilities ResolveClericCapabilities(const proto::Project& project)
	{
		BotClericCapabilities result;
		const proto::ClassEntry* cleric = FindClericClass(project);
		if (!cleric)
		{
			result.issues.emplace_back("cleric_class_missing");
			return result;
		}

		result.classId = cleric->id();
		result.spellFamily = cleric->spellfamily();
		if (cleric->powertype() == proto::ClassEntry::MANA)
		{
			result.powerType = power_type::Mana;
		}
		else
		{
			result.issues.emplace_back("cleric_power_type_unexpected");
		}

		std::map<uint32, std::vector<BotClericResolvedSpell>> chains;
		std::set<uint32> candidateSpellIds;
		for (int index = 0; index < cleric->spells_size(); ++index)
		{
			const proto::ClassSpell& classSpell = cleric->spells(index);
			const proto::SpellEntry* spell = project.spells.getById(classSpell.spell());
			if (!spell)
			{
				result.issues.emplace_back("spell_missing:" + std::to_string(classSpell.spell()));
				continue;
			}

			candidateSpellIds.insert(spell->id());
			BotClericResolvedSpell resolved = BuildResolvedSpell(project, classSpell.level(), *spell, result.issues);
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
				BotClericResolvedSpell resolved = BuildResolvedSpell(project, learnLevel, spell, result.issues);
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

			auto bestIt = std::max_element(chain.begin(), chain.end(), [&](const BotClericResolvedSpell& left, const BotClericResolvedSpell& right)
			{
				return ComputeRankScore(left, project) < ComputeRankScore(right, project);
			});

			const int bestScore = ComputeRankScore(*bestIt, project);
			const size_t tieCount = static_cast<size_t>(std::count_if(chain.begin(), chain.end(), [&](const BotClericResolvedSpell& spell)
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

		result.emergencyHeal = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
		{
			return ScoreEmergencyHeal(spell, resolved, text);
		});
		if (result.emergencyHeal)
		{
			result.emergencyHeal->kind = BotClericCapabilityKind::EmergencyHeal;
		}

		result.efficientHeal = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
		{
			return ScoreEfficientHeal(spell, resolved, text);
		});
		if (result.efficientHeal)
		{
			result.efficientHeal->kind = BotClericCapabilityKind::EfficientHeal;
		}

		if (!result.emergencyHeal && result.efficientHeal)
		{
			result.emergencyHeal = result.efficientHeal;
			result.emergencyHeal->kind = BotClericCapabilityKind::EmergencyHeal;
			result.issues.emplace_back("emergency_heal_fallback:" + std::to_string(result.emergencyHeal->spellId));
		}
		if (!result.efficientHeal && result.emergencyHeal)
		{
			result.efficientHeal = result.emergencyHeal;
			result.efficientHeal->kind = BotClericCapabilityKind::EfficientHeal;
			result.issues.emplace_back("efficient_heal_fallback:" + std::to_string(result.efficientHeal->spellId));
		}

		result.supportAura = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
		{
			return ScoreSupportAura(spell, resolved, text);
		});
		if (result.supportAura)
		{
			result.supportAura->kind = BotClericCapabilityKind::SupportAura;
		}

		result.supportBuff = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
		{
			return ScoreSupportBuff(spell, resolved, text);
		});
		if (result.supportBuff)
		{
			result.supportBuff->kind = BotClericCapabilityKind::SupportBuff;
		}

		result.defensive = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
		{
			return ScoreDefensive(spell, resolved, text);
		});
		if (result.defensive)
		{
			result.defensive->kind = BotClericCapabilityKind::Defensive;
		}

		result.damageFiller = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotClericResolvedSpell& resolved, const std::string& text)
		{
			return ScoreDamageFiller(spell, resolved, text);
		});
		if (result.damageFiller)
		{
			result.damageFiller->kind = BotClericCapabilityKind::DamageFiller;
		}

		if (result.spells.empty())
		{
			result.issues.emplace_back("cleric_spellbook_empty");
		}
		if (!result.emergencyHeal)
		{
			result.issues.emplace_back("emergency_heal_missing");
		}
		if (!result.efficientHeal)
		{
			result.issues.emplace_back("efficient_heal_missing");
		}
		if (!result.damageFiller)
		{
			result.issues.emplace_back("damage_filler_missing");
		}
		if (!result.supportAura && !result.supportBuff)
		{
			result.issues.emplace_back("support_upkeep_missing");
		}
		if (!result.defensive)
		{
			result.issues.emplace_back("defensive_missing");
		}

		result.resolved = !result.spells.empty()
			&& result.powerType == power_type::Mana
			&& result.emergencyHeal.has_value()
			&& result.efficientHeal.has_value()
			&& result.damageFiller.has_value();
		return result;
	}

	const char* ToString(const BotClericCapabilityKind kind) noexcept
	{
		switch (kind)
		{
		case BotClericCapabilityKind::EmergencyHeal:
			return "emergency_heal";
		case BotClericCapabilityKind::EfficientHeal:
			return "efficient_heal";
		case BotClericCapabilityKind::SupportAura:
			return "support_aura";
		case BotClericCapabilityKind::SupportBuff:
			return "support_buff";
		case BotClericCapabilityKind::Defensive:
			return "defensive";
		case BotClericCapabilityKind::DamageFiller:
			return "damage_filler";
		case BotClericCapabilityKind::None:
		default:
			return "none";
		}
	}
}
