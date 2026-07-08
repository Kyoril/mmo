// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file localization.h
 *
 * @brief Package-neutral locale identifier and helpers for localized game data.
 *
 * Game-data strings (item names, quest text, gossip text, ...) are stored with a
 * base English field plus an additive set of per-locale overrides. This header
 * provides the canonical @ref LocaleIndex used to key those overrides, along with
 * header-only accessors that work with any protobuf @c LocalizedString message
 * (both @c mmo::proto and @c mmo::proto_client define one), so the same logic
 * serves the realm/world servers, the client and the editor.
 */

#pragma once

#include "base/typedefs.h"

#include <string>

namespace mmo
{
	/**
	 * @brief Canonical, dense locale index used to key localized game-data strings.
	 *
	 * This is distinct from the wire-level auth::AuthLocale (a FourCC): it is a
	 * compact, contiguous index that is stable across the storage/lookup layer.
	 * enUS is index 0 and always means "use the untouched base English field".
	 *
	 * New locales may be appended before Count without disturbing existing values,
	 * because the keyed LocalizedString storage records the index explicitly.
	 */
	enum class LocaleIndex : uint32
	{
		enUS = 0,
		deDE = 1,
		enGB = 2,
		frFR = 3,
		ruRU = 4,

		Count
	};

	/// Returns the four-letter locale code (e.g. "deDE") for a locale index, or "enUS" for unknown values.
	inline const char* LocaleCode(const LocaleIndex locale)
	{
		switch (locale)
		{
		case LocaleIndex::deDE: return "deDE";
		case LocaleIndex::enGB: return "enGB";
		case LocaleIndex::frFR: return "frFR";
		case LocaleIndex::ruRU: return "ruRU";
		case LocaleIndex::enUS:
		default:                return "enUS";
		}
	}

	/// Maps a four-letter locale code (e.g. "deDE") to a locale index, defaulting to enUS for unknown codes.
	inline LocaleIndex LocaleIndexFromString(const std::string& code)
	{
		if (code == "deDE") { return LocaleIndex::deDE; }
		if (code == "enGB") { return LocaleIndex::enGB; }
		if (code == "frFR") { return LocaleIndex::frFR; }
		if (code == "ruRU") { return LocaleIndex::ruRU; }
		return LocaleIndex::enUS;
	}

	/**
	 * @brief Returns the WoW-style FourCC wire value for a locale index.
	 *
	 * This is the 4-byte code exchanged on the network (LogonChallenge / AuthSession).
	 * Defaults to the enUS FourCC for unknown values.
	 */
	inline uint32 LocaleFourCC(const LocaleIndex locale)
	{
		switch (locale)
		{
		case LocaleIndex::deDE: return 0x64654445;
		case LocaleIndex::enGB: return 0x656e4742;
		case LocaleIndex::frFR: return 0x66724652;
		case LocaleIndex::ruRU: return 0x72755255;
		case LocaleIndex::enUS:
		default:                return 0x656e5553;
		}
	}

	/// Converts a raw integer (e.g. from an internal server-to-server packet) into a locale index, defaulting to enUS if out of range.
	inline LocaleIndex LocaleIndexFromValue(const uint32 value)
	{
		return (value < static_cast<uint32>(LocaleIndex::Count)) ? static_cast<LocaleIndex>(value) : LocaleIndex::enUS;
	}

	/// Maps a WoW-style FourCC wire value back to a locale index, defaulting to enUS for unknown/unsupported values.
	inline LocaleIndex LocaleIndexFromFourCC(const uint32 fourCC)
	{
		switch (fourCC)
		{
		case 0x64654445: return LocaleIndex::deDE;
		case 0x656e4742: return LocaleIndex::enGB;
		case 0x66724652: return LocaleIndex::frFR;
		case 0x72755255: return LocaleIndex::ruRU;
		default:         return LocaleIndex::enUS;	// includes enUS (0x656e5553) and any unsupported locale
		}
	}

	/**
	 * @brief Selects the localized value for a string, falling back to the base English text.
	 *
	 * @tparam LocArray A protobuf RepeatedPtrField of a LocalizedString-like message
	 *         exposing locale() and value() accessors.
	 * @param base The authoritative base (English) string; returned on any miss.
	 * @param locArray The array of per-locale overrides.
	 * @param locale The requested locale.
	 * @return The override for @p locale if present and non-empty, otherwise @p base.
	 */
	template <class LocArray>
	const std::string& GetLocalizedString(const std::string& base, const LocArray& locArray, const LocaleIndex locale)
	{
		// enUS is the base field by definition - never stored as an override.
		if (locale == LocaleIndex::enUS)
		{
			return base;
		}

		const auto localeValue = static_cast<uint32>(locale);
		for (const auto& entry : locArray)
		{
			if (entry.locale() == localeValue && !entry.value().empty())
			{
				return entry.value();
			}
		}

		return base;
	}

	/**
	 * @brief Finds the override entry for a locale, creating it if missing.
	 *
	 * Used by the editor import path to write translations back additively. The
	 * base English field is never touched by this helper.
	 *
	 * @tparam RepeatedField A mutable protobuf RepeatedPtrField of LocalizedString.
	 * @param locArray The array of per-locale overrides (must not be null).
	 * @param locale The locale to find or add.
	 * @return Pointer to the existing or newly added entry (its locale is set).
	 */
	template <class RepeatedField>
	auto FindOrAddLoc(RepeatedField* locArray, const LocaleIndex locale) -> decltype(locArray->Add())
	{
		const auto localeValue = static_cast<uint32>(locale);
		for (auto& entry : *locArray)
		{
			if (entry.locale() == localeValue)
			{
				return &entry;
			}
		}

		auto* entry = locArray->Add();
		entry->set_locale(localeValue);
		return entry;
	}
}
