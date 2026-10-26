// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file zone_names.h
 *
 * @brief Localized display names for zones.
 *
 * Zone names are game data: the base English name lives in ZoneEntry::name, translations in
 * ZoneEntry::name_loc. Anything that puts a zone name in front of the player has to go through
 * these helpers, or German clients read English zone names in an otherwise translated UI.
 */

#pragma once

#include "base/localization.h"

#include <string>

namespace mmo
{
	/// @brief Returns a zone's name in the given locale, falling back to the base English name.
	/// @tparam TZoneEntry A ZoneEntry (proto or proto_client).
	/// @param zone The zone to name.
	/// @param locale The locale to display the name in.
	template <typename TZoneEntry>
	[[nodiscard]] const std::string& GetZoneDisplayName(const TZoneEntry& zone, const LocaleIndex locale)
	{
		return GetLocalizedString(zone.name(), zone.name_loc(), locale);
	}

	/// @brief Resolves the localized zone and sub zone name shown for a zone the player is in.
	/// @tparam TZones A ZoneManager (proto or proto_client).
	/// @tparam TZoneEntry A ZoneEntry of the same package.
	/// @param zones The zone manager used to look up the parent zone.
	/// @param zone The zone the player stands in.
	/// @param locale The locale to display the names in.
	/// @param outZoneName Receives the parent zone's name if there is one, otherwise the zone's own.
	/// @param outSubZoneName Receives the zone's own name if it has a parent, otherwise an empty string.
	template <typename TZones, typename TZoneEntry>
	void ResolveZoneDisplayNames(const TZones& zones, const TZoneEntry& zone, const LocaleIndex locale,
		std::string& outZoneName, std::string& outSubZoneName)
	{
		const auto* parentZone = (zone.parentzone() != 0) ? zones.getById(zone.parentzone()) : nullptr;
		if (parentZone)
		{
			outZoneName = GetZoneDisplayName(*parentZone, locale);
			outSubZoneName = GetZoneDisplayName(zone, locale);
		}
		else
		{
			outZoneName = GetZoneDisplayName(zone, locale);
			outSubZoneName.clear();
		}
	}
}
