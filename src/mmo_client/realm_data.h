// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"


namespace mmo
{

	/// Contains realm informations.
	struct RealmData final
	{
		/// Unique id of a realm.
		uint32 id;
		/// Realm display name.
		std::string name;
		/// Realm address (ip).
		std::string address;
		/// Realm port.
		uint16 port;
		/// Coarse population band (auth::realm_population), shown to every player.
		uint8 population = 0;
		/// Players connected to the realm. Only sent to game masters; auth::HiddenRealmPlayerCount otherwise.
		uint32 playerCount = 0xFFFFFFFF;
	};
}
