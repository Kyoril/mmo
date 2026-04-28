// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_warrior_capabilities.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
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

	bool Contains(std::string_view haystack, std::string_view needle)
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

	const proto::ClassEntry* FindWarriorClass(const proto::Project& project)
	{
		const auto& classes = project.classes.getTemplates();
		const proto::ClassEntry* best = nullptr;
		int bestScore = -1;
		for (int i = 0; i < classes.entry_size(); ++i)
		{
			const auto& entry = classes.entry(i);
			if (entry.powertype() != proto::ClassEntry::RAGE)
			{
				continue;
			}

			int score = entry.spells_size();
			if (entry.has_mainhand_auto_attack_spell() && entry.mainhand_auto_attack_spell() != 0)
			{
				score += 1000;
			}
			if (!entry.internalname().empty())
			{
				score += 10;
			}
			if (!entry.name().empty())
			{
				score += 5;
			}

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

	BotWarriorResolvedSpell BuildResolvedSpell(
		const proto::Project& project,
		const proto::ClassSpell& classSpell,
		const proto::SpellEntry& spell,
		std::vector<std::string>& issues)
	{
		BotWarriorResolvedSpell result;
		result.spellId = spell.id();
		result.rootSpellId = ResolveRootSpellId(project, spell);
		result.rank = spell.has_rank() ? spell.rank() : 0;
		result.classLearnLevel = classSpell.level();
		result.powerCost = spell.has_cost() ? spell.cost() : 0;
		result.cooldownMs = spell.has_cooldown() ? static_cast<GameTime>(spell.cooldown()) : 0;
		result.requiresTarget = (spell.targetmap() & spell_cast_target_flags::Unit) != 0;
		result.positive = spell.has_positive() && spell.positive() != 0;
		result.passive = IsPassiveSpell(spell);
		result.hidden = IsHiddenSpell(spell);
		result.targetMap = spell.has_targetmap() ? spell.targetmap() : 0;
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

		return result;
	}

	int ComputeRankScore(const BotWarriorResolvedSpell& spell, const proto::Project& project)
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

	int ScoreGapCloser(const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
	{
		int score = 0;
		if (proto::SpellHasEffect(spell, spell_effects::Charge))
		{
			score += 200;
		}
		if (Contains(text, "charge"))
		{
			score += 120;
		}
		if (resolved.requiresTarget)
		{
			score += 10;
		}
		if (!resolved.positive)
		{
			score += 5;
		}
		if (resolved.maxRange >= 8.0f)
		{
			score += 20;
		}
		return score;
	}

	int ScoreThreatBuilder(const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
	{
		if (!resolved.requiresTarget || resolved.positive)
		{
			return 0;
		}

		int score = 0;
		if (proto::SpellHasEffect(spell, spell_effects::WeaponDamage) || proto::SpellHasEffect(spell, spell_effects::WeaponDamageNoSchool) || proto::SpellHasEffect(spell, spell_effects::SchoolDamage))
		{
			score += 40;
		}
		if (resolved.powerCost > 0)
		{
			score += 20;
		}
		if (spell.has_threat_multiplier() && spell.threat_multiplier() > 1.0f)
		{
			score += static_cast<int>((spell.threat_multiplier() - 1.0f) * 100.0f);
		}
		if (Contains(text, "threat"))
		{
			score += 80;
		}
		if (Contains(text, "strike"))
		{
			score += 25;
		}
		if (Contains(text, "slam"))
		{
			score += 25;
		}
		if (Contains(text, "shield"))
		{
			score += 10;
		}
		if (resolved.hasExplicitRange && resolved.maxRange <= 6.0f)
		{
			score += 10;
		}
		return score;
	}

	int ScoreArmorDebuff(const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
	{
		if (!resolved.requiresTarget || resolved.positive)
		{
			return 0;
		}

		int score = 0;
		if (proto::SpellHasEffect(spell, spell_effects::ApplyAura))
		{
			score += 20;
		}
		if (Contains(text, "armor"))
		{
			score += 70;
		}
		if (Contains(text, "rend") || Contains(text, "bleed"))
		{
			score += 80;
		}
		return score;
	}

	int ScoreRageBuilder(const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
	{
		int score = 0;
		if (proto::SpellHasEffect(spell, spell_effects::Energize))
		{
			score += 120;
		}
		if (Contains(text, "rage") || Contains(text, "enrage"))
		{
			score += 80;
		}
		if (!resolved.requiresTarget)
		{
			score += 10;
		}
		if (resolved.positive)
		{
			score += 10;
		}
		return score;
	}

	int ScorePartyBuff(const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
	{
		if (!resolved.positive)
		{
			return 0;
		}

		int score = 0;
		if (proto::SpellHasEffect(spell, spell_effects::ApplyAura) || proto::SpellHasEffect(spell, spell_effects::ApplyAreaAura))
		{
			score += 20;
		}
		if (Contains(text, "battlecry") || Contains(text, "shout"))
		{
			score += 120;
		}
		if (Contains(text, "group members"))
		{
			score += 80;
		}
		if (Contains(text, "armor increased") || Contains(text, "stamina increased"))
		{
			score += 20;
		}
		if (!resolved.requiresTarget)
		{
			score += 10;
		}
		return score;
	}

	int ScoreMitigation(const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
	{
		if (!resolved.positive)
		{
			return 0;
		}

		int score = 0;
		if (spell.has_mechanic() && spell.mechanic() == spell_mechanic::Shield)
		{
			score += 80;
		}
		if (Contains(text, "shield"))
		{
			score += 80;
		}
		if (Contains(text, "damage taken") || Contains(text, "armor by") || Contains(text, "reducing physical damage"))
		{
			score += 60;
		}
		if (!resolved.requiresTarget)
		{
			score += 10;
		}
		return score;
	}

	int ScoreRageSpender(const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
	{
		if (!resolved.requiresTarget || resolved.positive)
		{
			return 0;
		}

		int score = 0;
		if (resolved.powerCost >= 20)
		{
			score += 60;
		}
		if (proto::SpellHasEffect(spell, spell_effects::WeaponDamage) || proto::SpellHasEffect(spell, spell_effects::WeaponDamageNoSchool) || proto::SpellHasEffect(spell, spell_effects::SchoolDamage))
		{
			score += 30;
		}
		if (Contains(text, "slam"))
		{
			score += 35;
		}
		if (Contains(text, "strike"))
		{
			score += 15;
		}
		return score;
	}

	int ScoreTaunt(const BotWarriorResolvedSpell& resolved, const std::string& text)
	{
		if (!resolved.requiresTarget || resolved.positive)
		{
			return 0;
		}

		int score = 0;
		if (Contains(text, "taunt"))
		{
			score += 150;
		}
		if (Contains(text, "attack you"))
		{
			score += 80;
		}
		return score;
	}

	template <typename Accessor>
	std::optional<BotWarriorResolvedSpell> PickBestSpell(const std::vector<BotWarriorResolvedSpell>& spells, const proto::Project& project, Accessor&& scoreAccessor)
	{
		const BotWarriorResolvedSpell* best = nullptr;
		int bestScore = 0;
		for (const BotWarriorResolvedSpell& resolved : spells)
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

		return bestScore > 0 && best ? std::optional<BotWarriorResolvedSpell>(*best) : std::nullopt;
	}
}

namespace mmo
{
	bool BotWarriorResolvedSpell::IsInRange(const float distance) const noexcept
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

	size_t BotWarriorCapabilities::GetResolvedCategoryCount() const noexcept
	{
		size_t count = 0;
		count += gapCloser.has_value() ? 1u : 0u;
		count += threatBuilder.has_value() ? 1u : 0u;
		count += armorDebuff.has_value() ? 1u : 0u;
		count += rageBuilder.has_value() ? 1u : 0u;
		count += partyBuff.has_value() ? 1u : 0u;
		count += mitigation.has_value() ? 1u : 0u;
		count += rageSpender.has_value() ? 1u : 0u;
		count += taunt.has_value() ? 1u : 0u;
		return count;
	}

	const BotWarriorResolvedSpell* BotWarriorCapabilities::FindSpell(const uint32 spellId) const noexcept
	{
		for (const BotWarriorResolvedSpell& spell : spells)
		{
			if (spell.spellId == spellId)
			{
				return &spell;
			}
		}

		return nullptr;
	}

	BotWarriorCapabilities ResolveWarriorCapabilities(const proto::Project& project)
	{
		BotWarriorCapabilities result;
		const proto::ClassEntry* warrior = FindWarriorClass(project);
		if (!warrior)
		{
			result.issues.emplace_back("warrior_class_missing");
			return result;
		}

		result.classId = warrior->id();
		result.spellFamily = warrior->spellfamily();
		result.powerType = power_type::Rage;
		result.mainhandAutoAttackSpellId = warrior->has_mainhand_auto_attack_spell() ? warrior->mainhand_auto_attack_spell() : 0;

		std::map<uint32, std::vector<BotWarriorResolvedSpell>> chains;
		for (int index = 0; index < warrior->spells_size(); ++index)
		{
			const proto::ClassSpell& classSpell = warrior->spells(index);
			const proto::SpellEntry* spell = project.spells.getById(classSpell.spell());
			if (!spell)
			{
				result.issues.emplace_back("spell_missing:" + std::to_string(classSpell.spell()));
				continue;
			}

			BotWarriorResolvedSpell resolved = BuildResolvedSpell(project, classSpell, *spell, result.issues);
			if (resolved.passive || resolved.hidden)
			{
				continue;
			}

			chains[resolved.rootSpellId].push_back(std::move(resolved));
		}

		for (auto& [rootId, chain] : chains)
		{
			if (chain.empty())
			{
				continue;
			}

			auto bestIt = std::max_element(chain.begin(), chain.end(), [&](const BotWarriorResolvedSpell& left, const BotWarriorResolvedSpell& right)
			{
				return ComputeRankScore(left, project) < ComputeRankScore(right, project);
			});
			bestIt->rootSpellId = rootId;
			result.spells.push_back(*bestIt);
		}

		result.gapCloser = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
		{
			return ScoreGapCloser(spell, resolved, text);
		});
		if (result.gapCloser)
		{
			result.gapCloser->kind = BotWarriorCapabilityKind::GapCloser;
		}

		result.threatBuilder = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
		{
			return ScoreThreatBuilder(spell, resolved, text);
		});
		if (result.threatBuilder)
		{
			result.threatBuilder->kind = BotWarriorCapabilityKind::ThreatBuilder;
		}

		result.armorDebuff = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
		{
			return ScoreArmorDebuff(spell, resolved, text);
		});
		if (result.armorDebuff)
		{
			result.armorDebuff->kind = BotWarriorCapabilityKind::ArmorDebuff;
		}

		result.rageBuilder = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
		{
			return ScoreRageBuilder(spell, resolved, text);
		});
		if (result.rageBuilder)
		{
			result.rageBuilder->kind = BotWarriorCapabilityKind::RageBuilder;
		}

		result.partyBuff = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
		{
			return ScorePartyBuff(spell, resolved, text);
		});
		if (result.partyBuff)
		{
			result.partyBuff->kind = BotWarriorCapabilityKind::PartyBuff;
		}

		result.mitigation = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
		{
			return ScoreMitigation(spell, resolved, text);
		});
		if (result.mitigation)
		{
			result.mitigation->kind = BotWarriorCapabilityKind::Mitigation;
		}

		result.rageSpender = PickBestSpell(result.spells, project, [](const proto::SpellEntry& spell, const BotWarriorResolvedSpell& resolved, const std::string& text)
		{
			return ScoreRageSpender(spell, resolved, text);
		});
		if (result.rageSpender)
		{
			result.rageSpender->kind = BotWarriorCapabilityKind::RageSpender;
		}

		result.taunt = PickBestSpell(result.spells, project, [](const proto::SpellEntry&, const BotWarriorResolvedSpell& resolved, const std::string& text)
		{
			return ScoreTaunt(resolved, text);
		});
		if (result.taunt)
		{
			result.taunt->kind = BotWarriorCapabilityKind::Taunt;
		}

		result.resolved = !result.spells.empty() && result.mainhandAutoAttackSpellId != 0;
		if (!result.resolved && result.spells.empty())
		{
			result.issues.emplace_back("warrior_spellbook_empty");
		}
		if (!result.resolved && result.mainhandAutoAttackSpellId == 0)
		{
			result.issues.emplace_back("auto_attack_missing");
		}

		return result;
	}

	const char* ToString(const BotWarriorCapabilityKind kind) noexcept
	{
		switch (kind)
		{
		case BotWarriorCapabilityKind::GapCloser:
			return "gap_closer";
		case BotWarriorCapabilityKind::ThreatBuilder:
			return "threat_builder";
		case BotWarriorCapabilityKind::ArmorDebuff:
			return "armor_debuff";
		case BotWarriorCapabilityKind::RageBuilder:
			return "rage_builder";
		case BotWarriorCapabilityKind::PartyBuff:
			return "party_buff";
		case BotWarriorCapabilityKind::Mitigation:
			return "mitigation";
		case BotWarriorCapabilityKind::RageSpender:
			return "rage_spender";
		case BotWarriorCapabilityKind::Taunt:
			return "taunt";
		case BotWarriorCapabilityKind::None:
		default:
			return "none";
		}
	}
}
