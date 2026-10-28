// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/project.h"
#include "client_data/zone_names.h"

using namespace mmo;

namespace
{
	void AddLocalizedName(proto_client::ZoneEntry& zone, const LocaleIndex locale, const char* value)
	{
		proto_client::LocalizedString* loc = zone.add_name_loc();
		loc->set_locale(static_cast<uint32>(locale));
		loc->set_value(value);
	}
}

TEST_CASE("Zone and sub zone names are shown in the client's locale", "[zone_names]")
{
	// The zone text and the minimap title read these names, so an untranslated lookup leaves a
	// German client with English zone names in an otherwise translated interface.
	proto_client::ZoneManager zones;

	proto_client::ZoneEntry* parent = zones.add(10);
	parent->set_name("Falwyn Forest");
	AddLocalizedName(*parent, LocaleIndex::deDE, "Falwyn-Wald");

	proto_client::ZoneEntry* child = zones.add(11);
	child->set_name("Ruined Cottages");
	child->set_parentzone(10);
	AddLocalizedName(*child, LocaleIndex::deDE, "Zerstoerte Huetten");

	std::string zoneName;
	std::string subZoneName;

	ResolveZoneDisplayNames(zones, *child, LocaleIndex::deDE, zoneName, subZoneName);
	CHECK(zoneName == "Falwyn-Wald");
	CHECK(subZoneName == "Zerstoerte Huetten");

	ResolveZoneDisplayNames(zones, *parent, LocaleIndex::deDE, zoneName, subZoneName);
	CHECK(zoneName == "Falwyn-Wald");
	CHECK(subZoneName.empty());

	CHECK(GetZoneDisplayName(*child, LocaleIndex::deDE) == "Zerstoerte Huetten");
}

TEST_CASE("Zone names fall back to the base English name", "[zone_names]")
{
	proto_client::ZoneManager zones;

	proto_client::ZoneEntry* parent = zones.add(10);
	parent->set_name("Falwyn Forest");

	proto_client::ZoneEntry* child = zones.add(11);
	child->set_name("Ruined Cottages");
	child->set_parentzone(10);
	AddLocalizedName(*child, LocaleIndex::frFR, "Chalets en ruine");

	std::string zoneName;
	std::string subZoneName;

	// enUS always reads the base field, and a locale without a translation falls back to it.
	ResolveZoneDisplayNames(zones, *child, LocaleIndex::enUS, zoneName, subZoneName);
	CHECK(zoneName == "Falwyn Forest");
	CHECK(subZoneName == "Ruined Cottages");

	ResolveZoneDisplayNames(zones, *child, LocaleIndex::frFR, zoneName, subZoneName);
	CHECK(zoneName == "Falwyn Forest");
	CHECK(subZoneName == "Chalets en ruine");

	// A parent id pointing at nothing must not lose the zone's own name.
	child->set_parentzone(999);
	ResolveZoneDisplayNames(zones, *child, LocaleIndex::frFR, zoneName, subZoneName);
	CHECK(zoneName == "Chalets en ruine");
	CHECK(subZoneName.empty());
}
