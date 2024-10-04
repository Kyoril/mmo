// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
	};
}
