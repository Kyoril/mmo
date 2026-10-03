// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_text_formatter.h"

#include "game/aura.h"
#include "game/spell.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace mmo
{
	namespace
	{
		bool IsPeriodicAura(const uint32 aura)
		{
			return aura == aura_type::PeriodicDamage ||
				aura == aura_type::PeriodicHeal ||
				aura == aura_type::PeriodicTriggerSpell ||
				aura == aura_type::PeriodicEnergize;
		}

		void CalculateEffectBasePoints(const proto_client::SpellEffect& effect, const proto_client::SpellEntry& spell, int32 casterLevel, int32& minBasePoints, int32& maxBasePoints)
		{
			if (casterLevel > spell.maxlevel() && spell.maxlevel() > 0)
			{
				casterLevel = spell.maxlevel();
			}
			else if (casterLevel < spell.baselevel())
			{
				casterLevel = spell.baselevel();
			}
			casterLevel -= spell.spelllevel();

			const float basePointsPerLevel = effect.pointsperlevel();
			const float randomPointsPerLevel = effect.diceperlevel();
			const int32 basePoints = effect.basepoints() + casterLevel * basePointsPerLevel;
			const int32 randomPoints = effect.diesides() + casterLevel * randomPointsPerLevel;

			minBasePoints = basePoints + effect.basedice();
			maxBasePoints = basePoints + randomPoints;
		}

		bool IsDirectAmountEffect(const uint32 type)
		{
			return type == spell_effects::SchoolDamage || type == spell_effects::Heal;
		}

		/// The total a periodic trigger effect amounts to: the triggered spell's first damage or
		/// heal effect, once per tick. Returns false when there is nothing sensible to total, so
		/// the caller can fall back to the trigger effect's own points.
		bool GetPeriodicTriggerTotal(const proto_client::SpellEntry& spell, const int32 effectIndex, const SpellTextContext& context, int32& min, int32& max)
		{
			const auto& effect = spell.effects(effectIndex);
			if (effect.aura() != aura_type::PeriodicTriggerSpell || effect.triggerspell() == 0 || !context.findSpell)
			{
				return false;
			}

			const proto_client::SpellEntry* triggered = context.findSpell(effect.triggerspell());
			if (!triggered || triggered->id() == spell.id())
			{
				return false;
			}

			for (int32 i = 0; i < triggered->effects_size(); ++i)
			{
				if (!IsDirectAmountEffect(triggered->effects(i).type()))
				{
					continue;
				}

				GetSpellEffectPoints(*triggered, context.level, i, false, min, max);

				const int32 ticks = GetSpellEffectTickCount(spell, effectIndex);
				min *= ticks;
				max *= ticks;
				return true;
			}

			return false;
		}

		void WriteRange(std::ostringstream& strm, const int32 min, const int32 max)
		{
			if (min == max)
			{
				strm << min;
			}
			else
			{
				strm << min << " - " << max;
			}
		}

		void WriteDuration(std::ostringstream& strm, const int64 milliseconds, const bool precise, const SpellTextContext& context)
		{
			// Default display value is seconds
			double displayValue = static_cast<double>(milliseconds) / 1000.0;
			std::string formatKey = "FORMAT_DURATION_SECONDS";

			if (milliseconds >= 60000 * 60)
			{
				displayValue /= 3600.0;
				formatKey = "FORMAT_DURATION_HOURS";
			}
			else if (milliseconds >= 60000)
			{
				displayValue /= 60.0;
				formatKey = "FORMAT_DURATION_MINUTES";
			}

			if (precise)
			{
				formatKey += "_PRECISE";
			}

			if (context.formatDuration)
			{
				strm << context.formatDuration(formatKey, displayValue);
			}
			else
			{
				strm << formatKey;
			}
		}

		bool TakesEffectIndex(const char token)
		{
			switch (std::tolower(static_cast<unsigned char>(token)))
			{
			case 's':
			case 'm':
			case 'o':
			case 't':
			case 'i':
				return true;
			default:
				return false;
			}
		}
	}

	void GetSpellEffectPoints(const proto_client::SpellEntry& spell, const int32 level, const int32 effectIndex, const bool includeTickCount, int32& min, int32& max)
	{
		min = 0;
		max = 0;

		if (effectIndex < 0 || effectIndex >= spell.effects_size())
		{
			return;
		}

		int32 minPoints = 0, maxPoints = 0;
		CalculateEffectBasePoints(spell.effects(effectIndex), spell, level, minPoints, maxPoints);

		if (includeTickCount)
		{
			const int32 ticks = std::max(GetSpellEffectTickCount(spell, effectIndex), 1);
			minPoints *= ticks;
			maxPoints *= ticks;
		}

		min = std::abs(minPoints);
		max = std::abs(maxPoints);
	}

	int32 GetSpellEffectTickCount(const proto_client::SpellEntry& spell, const int32 effectIndex)
	{
		if (effectIndex < 0 || effectIndex >= spell.effects_size())
		{
			return 0;
		}

		const auto& effect = spell.effects(effectIndex);
		if (!IsPeriodicAura(effect.aura()) || effect.amplitude() <= 0 || spell.duration() <= 0)
		{
			return 0;
		}

		return spell.duration() / effect.amplitude();
	}

	std::string FormatSpellText(const std::string& text, const proto_client::SpellEntry& spell, const SpellTextContext& context)
	{
		std::ostringstream strm;
		int32 effectIndex = 0;

		const size_t length = text.size();
		size_t i = 0;
		while (i < length)
		{
			if (text[i] != '$' || i + 1 >= length)
			{
				strm << text[i++];
				continue;
			}

			const size_t placeholderStart = i;
			size_t pos = i + 1;

			if (text[pos] == '$')
			{
				strm << '$';
				i = pos + 1;
				continue;
			}

			// Optional id of another spell whose values are used instead of this one's
			uint32 referencedId = 0;
			while (pos < length && std::isdigit(static_cast<unsigned char>(text[pos])))
			{
				referencedId = referencedId * 10 + static_cast<uint32>(text[pos] - '0');
				++pos;
			}

			const proto_client::SpellEntry* source = &spell;
			if (pos > placeholderStart + 1)
			{
				source = context.findSpell ? context.findSpell(referencedId) : nullptr;
			}

			if (pos >= length || !source)
			{
				// No token letter, or an unknown spell: keep the text as authored
				strm << text.substr(placeholderStart, pos - placeholderStart);
				i = pos;
				continue;
			}

			const char token = text[pos++];
			if (TakesEffectIndex(token) && pos < length && std::isdigit(static_cast<unsigned char>(text[pos])))
			{
				effectIndex = text[pos++] - '0';
			}

			int32 min = 0, max = 0;
			switch (token)
			{
			case 'd':
			case 'D':
				WriteDuration(strm, source->duration(), token == 'd', context);
				break;

			case 'i':
			case 'I':
			{
				int64 amplitude = 0;
				if (effectIndex >= 0 && effectIndex < source->effects_size() && source->effects(effectIndex).amplitude() > 0)
				{
					amplitude = source->effects(effectIndex).amplitude();
				}
				WriteDuration(strm, amplitude, token == 'i', context);
				break;
			}

			case 'm':
				GetSpellEffectPoints(*source, context.level, effectIndex, false, min, max);
				strm << min;
				break;

			case 'M':
				GetSpellEffectPoints(*source, context.level, effectIndex, false, min, max);
				strm << max;
				break;

			case 's':
			case 'S':
				GetSpellEffectPoints(*source, context.level, effectIndex, false, min, max);
				WriteRange(strm, min, max);
				break;

			case 'o':
			case 'O':
				if (effectIndex < 0 || effectIndex >= source->effects_size() || !GetPeriodicTriggerTotal(*source, effectIndex, context, min, max))
				{
					GetSpellEffectPoints(*source, context.level, effectIndex, true, min, max);
				}
				WriteRange(strm, min, max);
				break;

			case 't':
			case 'T':
				strm << GetSpellEffectTickCount(*source, effectIndex);
				break;

			default:
				// Unknown token: keep it visible so authoring mistakes show up in game
				strm << text.substr(placeholderStart, pos - placeholderStart);
				break;
			}

			i = pos;
		}

		return strm.str();
	}
}
