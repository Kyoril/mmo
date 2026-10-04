// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_text_formatter.h"

#include "game/aura.h"
#include "game/spell.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

namespace mmo
{
	namespace
	{
		constexpr int64 Int32Min = std::numeric_limits<int32>::min();
		constexpr int64 Int32Max = std::numeric_limits<int32>::max();

		/// Spell text is data. These helpers replace <cctype>, whose behaviour depends on the
		/// C locale and is undefined for negative chars.
		bool IsDigit(const char c)
		{
			return c >= '0' && c <= '9';
		}

		char ToLower(const char c)
		{
			return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
		}

		int32 SaturateToInt32(const int64 value)
		{
			return static_cast<int32>(std::clamp(value, Int32Min, Int32Max));
		}

		/// Truncates towards zero like an implicit float to int conversion, but defined for NaN,
		/// infinity and values outside the int32 range, which data can contain.
		int64 TruncateToInt32Range(const float value)
		{
			if (std::isnan(value))
			{
				return 0;
			}

			if (value >= static_cast<float>(Int32Max))
			{
				return Int32Max;
			}

			if (value <= static_cast<float>(Int32Min))
			{
				return Int32Min;
			}

			return static_cast<int64>(value);
		}

		bool IsPeriodicAura(const uint32 aura)
		{
			return aura == aura_type::PeriodicDamage ||
				aura == aura_type::PeriodicHeal ||
				aura == aura_type::PeriodicTriggerSpell ||
				aura == aura_type::PeriodicEnergize;
		}

		bool IsDirectAmountEffect(const uint32 type)
		{
			return type == spell_effects::SchoolDamage || type == spell_effects::Heal;
		}

		/// Mirrors the server's base point calculation, including its float precision, so the
		/// tooltip shows what a cast rolls. Works in int64 so no data can overflow it.
		void CalculateEffectBasePoints(const proto_client::SpellEffect& effect, const proto_client::SpellEntry& spell, const int32 level, int64& minBasePoints, int64& maxBasePoints)
		{
			int64 casterLevel = level;
			if (casterLevel > spell.maxlevel() && spell.maxlevel() > 0)
			{
				casterLevel = spell.maxlevel();
			}
			else if (casterLevel < spell.baselevel())
			{
				casterLevel = spell.baselevel();
			}
			casterLevel -= spell.spelllevel();

			const float levelFactor = static_cast<float>(casterLevel);
			const int64 basePoints = TruncateToInt32Range(static_cast<float>(effect.basepoints()) + levelFactor * effect.pointsperlevel());
			const int64 randomPoints = TruncateToInt32Range(static_cast<float>(effect.diesides()) + levelFactor * effect.diceperlevel());

			minBasePoints = basePoints + effect.basedice();
			maxBasePoints = basePoints + randomPoints;
		}

		/// Absolute value saturated to the int32 range, defined for every int64 the point
		/// calculation can produce.
		int32 AbsoluteSaturated(const int64 value)
		{
			return SaturateToInt32(value < 0 ? -value : value);
		}

		/// The duration the reader's spell modifiers give a spell. Infinite stays infinite, like
		/// on the server.
		int32 GetEffectiveDuration(const proto_client::SpellEntry& spell, const SpellTextContext& context)
		{
			if (spell.duration() == 0 || !context.getDuration)
			{
				return spell.duration();
			}

			return context.getDuration(spell);
		}

		int32 GetTickCount(const proto_client::SpellEntry& spell, const int32 effectIndex, const int32 duration)
		{
			if (effectIndex < 0 || effectIndex >= spell.effects_size())
			{
				return 0;
			}

			const auto& effect = spell.effects(effectIndex);
			if (!IsPeriodicAura(effect.aura()) || effect.amplitude() <= 0 || duration <= 0)
			{
				return 0;
			}

			return duration / effect.amplitude();
		}

		/// Effect points, multiplied by a tick count unless it is 0.
		void CalculateEffectPoints(const proto_client::SpellEntry& spell, const int32 level, const int32 effectIndex, const int32 ticks, int32& min, int32& max)
		{
			min = 0;
			max = 0;

			if (effectIndex < 0 || effectIndex >= spell.effects_size())
			{
				return;
			}

			int64 minPoints = 0, maxPoints = 0;
			CalculateEffectBasePoints(spell.effects(effectIndex), spell, level, minPoints, maxPoints);

			if (ticks > 0)
			{
				// Saturate first: the base points fit int64 comfortably, but times a tick count of
				// up to int32 max they might not.
				minPoints = static_cast<int64>(SaturateToInt32(minPoints)) * ticks;
				maxPoints = static_cast<int64>(SaturateToInt32(maxPoints)) * ticks;
			}

			min = AbsoluteSaturated(minPoints);
			max = AbsoluteSaturated(maxPoints);
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

				// Both factors are at most int32, so the product cannot overflow int64
				const int64 ticks = GetTickCount(spell, effectIndex, GetEffectiveDuration(spell, context));
				min = SaturateToInt32(static_cast<int64>(min) * ticks);
				max = SaturateToInt32(static_cast<int64>(max) * ticks);
				return true;
			}

			return false;
		}

		void AppendNumber(std::string& out, const int32 value)
		{
			char buffer[16];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
			out.append(buffer, result.ptr);
		}

		void AppendRange(std::string& out, const int32 min, const int32 max)
		{
			AppendNumber(out, min);
			if (min != max)
			{
				out.append(" - ");
				AppendNumber(out, max);
			}
		}

		void AppendDuration(std::string& out, const int64 milliseconds, const bool precise, const SpellTextContext& context)
		{
			// Built once: the lookup takes a std::string and these keys are too long for the
			// small string buffer, so building them per call would allocate.
			static const std::string keys[] = {
				"FORMAT_DURATION_SECONDS", "FORMAT_DURATION_SECONDS_PRECISE",
				"FORMAT_DURATION_MINUTES", "FORMAT_DURATION_MINUTES_PRECISE",
				"FORMAT_DURATION_HOURS", "FORMAT_DURATION_HOURS_PRECISE",
			};

			// Default display value is seconds
			double displayValue = static_cast<double>(milliseconds) / 1000.0;
			size_t unit = 0;

			if (milliseconds >= 60000 * 60)
			{
				displayValue /= 3600.0;
				unit = 2;
			}
			else if (milliseconds >= 60000)
			{
				displayValue /= 60.0;
				unit = 1;
			}

			// The rounded token rounds only whole values: a 1.5 s channel must not read "2 seconds".
			// A fractional value takes the precise template without its trailing zeros instead.
			// The tolerance matches the precise templates' two decimals, so 59.999 s reads "60".
			const bool whole = std::abs(displayValue - std::round(displayValue)) < 0.005;
			const bool usePrecise = precise || !whole;

			const std::string& key = keys[unit * 2 + (usePrecise ? 1 : 0)];
			const std::string* format = context.findDurationFormat ? context.findDurationFormat(key) : nullptr;
			if (format)
			{
				AppendFormattedValue(out, *format, displayValue, !precise);
			}
			else
			{
				out.append(key);
			}
		}

		bool TakesEffectIndex(const char token)
		{
			switch (ToLower(token))
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

		/// Drops trailing zeros after a decimal point, and the point itself if nothing remains
		/// ("1.50" -> "1.5", "2.00" -> "2"). Output without a decimal point, or ending in the
		/// padding of a left-justified width, is left alone.
		/// @returns The new length.
		int TrimTrailingZeros(const char* number, int length)
		{
			if (std::find(number, number + length, '.') == number + length)
			{
				return length;
			}

			while (length > 0 && number[length - 1] == '0')
			{
				--length;
			}

			if (length > 0 && number[length - 1] == '.')
			{
				--length;
			}

			return length;
		}

		/// Parses "<flags><width>.<precision>[l]" after a '%' starting at pos. Widths and
		/// precisions are limited to two digits, which bounds the printf output to a fixed buffer.
		/// An 'l' length modifier ("%lf") is accepted and dropped.
		/// @param specEnd Receives the end of the spec without the length modifier.
		/// @returns The position of the conversion character, or npos if the spec is malformed.
		size_t ParseConversionSpec(const std::string& format, size_t pos, size_t& specEnd, bool& hasAlternateFlag)
		{
			hasAlternateFlag = false;

			size_t flags = 0;
			while (pos < format.size() && flags < 5)
			{
				const char c = format[pos];
				if (c != '-' && c != '+' && c != ' ' && c != '#' && c != '0')
				{
					break;
				}

				hasAlternateFlag |= (c == '#');
				++pos;
				++flags;
			}

			const auto skipDigits = [&format, &pos]() -> bool
			{
				size_t digits = 0;
				while (pos < format.size() && IsDigit(format[pos]))
				{
					++pos;
					++digits;
				}
				return digits <= 2;
			};

			if (!skipDigits())
			{
				return std::string::npos;
			}

			if (pos < format.size() && format[pos] == '.')
			{
				++pos;
				if (!skipDigits())
				{
					return std::string::npos;
				}
			}

			specEnd = pos;
			if (pos < format.size() && format[pos] == 'l')
			{
				++pos;
			}

			return pos < format.size() ? pos : std::string::npos;
		}
	}

	void GetSpellEffectPoints(const proto_client::SpellEntry& spell, const int32 level, const int32 effectIndex, const bool includeTickCount, int32& min, int32& max)
	{
		// A non-periodic effect still counts once
		const int32 ticks = includeTickCount ? std::max(GetSpellEffectTickCount(spell, effectIndex), 1) : 0;
		CalculateEffectPoints(spell, level, effectIndex, ticks, min, max);
	}

	int32 GetSpellEffectTickCount(const proto_client::SpellEntry& spell, const int32 effectIndex)
	{
		return GetTickCount(spell, effectIndex, spell.duration());
	}

	void AppendFormattedValue(std::string& out, const std::string& format, const double value, const bool trimTrailingZeros)
	{
		bool replaced = false;
		size_t pos = 0;

		while (pos < format.size())
		{
			const size_t percent = format.find('%', pos);
			if (percent == std::string::npos)
			{
				out.append(format, pos, std::string::npos);
				return;
			}

			out.append(format, pos, percent - pos);

			if (percent + 1 < format.size() && format[percent + 1] == '%')
			{
				out.push_back('%');
				pos = percent + 2;
				continue;
			}

			bool hasAlternateFlag = false;
			size_t specEnd = percent + 1;
			const size_t conversion = replaced ? std::string::npos : ParseConversionSpec(format, percent + 1, specEnd, hasAlternateFlag);
			if (conversion == std::string::npos)
			{
				// Only one value to write, or a malformed spec: keep the rest as plain text
				out.push_back('%');
				pos = percent + 1;
				continue;
			}

			// At most "%" + 5 flags + 2 width digits + "." + 2 precision digits + "ll" + conversion
			char spec[16];
			const size_t specLength = specEnd - percent;
			std::copy_n(format.data() + percent, specLength, spec);

			char buffer[256];
			int written = -1;

			const char type = format[conversion];
			switch (type)
			{
			case 'f':
			case 'F':
			case 'e':
			case 'E':
			case 'g':
			case 'G':
				spec[specLength] = type;
				spec[specLength + 1] = '\0';
				written = std::snprintf(buffer, sizeof(buffer), spec, value);
				if (trimTrailingZeros && (type == 'f' || type == 'F') && written > 0)
				{
					written = TrimTrailingZeros(buffer, std::min(written, static_cast<int>(sizeof(buffer) - 1)));
				}
				break;

			case 'd':
			case 'i':
			case 'u':
			{
				// '#' with an integer conversion is undefined behaviour
				if (hasAlternateFlag)
				{
					break;
				}

				const double rounded = std::isnan(value) ? 0.0 : std::round(std::clamp(value, -9.0e18, 9.0e18));
				spec[specLength] = 'l';
				spec[specLength + 1] = 'l';
				spec[specLength + 2] = 'd';
				spec[specLength + 3] = '\0';
				written = std::snprintf(buffer, sizeof(buffer), spec, static_cast<long long>(rounded));
				break;
			}

			default:
				break;
			}

			if (written < 0)
			{
				out.push_back('%');
				pos = percent + 1;
				continue;
			}

			out.append(buffer, std::min(static_cast<size_t>(written), sizeof(buffer) - 1));
			replaced = true;
			pos = conversion + 1;
		}
	}

	std::string FormatSpellText(const std::string& text, const proto_client::SpellEntry& spell, const SpellTextContext& context)
	{
		// Most aura texts and many descriptions have no placeholder at all
		if (text.find('$') == std::string::npos)
		{
			return text;
		}

		std::string out;
		out.reserve(text.size() + 32);

		const size_t length = text.size();
		int32 effectIndex = 0;
		size_t i = 0;

		while (i < length)
		{
			const size_t dollar = text.find('$', i);
			if (dollar == std::string::npos)
			{
				out.append(text, i, std::string::npos);
				break;
			}

			out.append(text, i, dollar - i);

			size_t pos = dollar + 1;
			if (pos >= length)
			{
				out.push_back('$');
				break;
			}

			if (text[pos] == '$')
			{
				out.push_back('$');
				i = pos + 1;
				continue;
			}

			// Optional id of another spell whose values are used instead of this one's. Too many
			// digits for a spell id mark the reference unknown instead of wrapping around to some
			// unrelated spell.
			uint64 referencedId = 0;
			bool hasReference = false;
			while (pos < length && IsDigit(text[pos]))
			{
				hasReference = true;
				if (referencedId <= std::numeric_limits<uint32>::max())
				{
					referencedId = referencedId * 10 + static_cast<uint64>(text[pos] - '0');
				}
				++pos;
			}

			const proto_client::SpellEntry* source = &spell;
			if (hasReference)
			{
				source = (referencedId <= std::numeric_limits<uint32>::max() && context.findSpell)
					? context.findSpell(static_cast<uint32>(referencedId))
					: nullptr;
			}

			if (pos >= length || !source)
			{
				// No token letter, or an unknown spell: keep the text as authored
				out.append(text, dollar, pos - dollar);
				i = pos;
				continue;
			}

			const char token = text[pos++];
			if (TakesEffectIndex(token) && pos < length && IsDigit(text[pos]))
			{
				effectIndex = text[pos++] - '0';
			}

			const bool validEffect = effectIndex >= 0 && effectIndex < source->effects_size();

			int32 min = 0, max = 0;
			switch (token)
			{
			case 'd':
			case 'D':
				AppendDuration(out, GetEffectiveDuration(*source, context), token == 'd', context);
				break;

			case 'i':
			case 'I':
			{
				const int64 amplitude = validEffect ? std::max(source->effects(effectIndex).amplitude(), 0) : 0;
				AppendDuration(out, amplitude, token == 'i', context);
				break;
			}

			case 'm':
				GetSpellEffectPoints(*source, context.level, effectIndex, false, min, max);
				AppendNumber(out, min);
				break;

			case 'M':
				GetSpellEffectPoints(*source, context.level, effectIndex, false, min, max);
				AppendNumber(out, max);
				break;

			case 's':
			case 'S':
				GetSpellEffectPoints(*source, context.level, effectIndex, false, min, max);
				AppendRange(out, min, max);
				break;

			case 'o':
			case 'O':
				if (!validEffect || !GetPeriodicTriggerTotal(*source, effectIndex, context, min, max))
				{
					const int32 ticks = GetTickCount(*source, effectIndex, GetEffectiveDuration(*source, context));
					CalculateEffectPoints(*source, context.level, effectIndex, std::max(ticks, 1), min, max);
				}
				AppendRange(out, min, max);
				break;

			case 't':
			case 'T':
				AppendNumber(out, GetTickCount(*source, effectIndex, GetEffectiveDuration(*source, context)));
				break;

			default:
				// Unknown token: keep it visible so authoring mistakes show up in game
				out.append(text, dollar, pos - dollar);
				break;
			}

			i = pos;
		}

		return out;
	}
}
