// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <google/protobuf/repeated_field.h>

namespace mmo
{
	namespace proto
	{
		class Project;
	}

	/// @brief Draws an editable list of loot tables assigned to a unit or object.
	///
	/// Loot tables act as reusable modules: multiple loot tables can be assigned to a single unit or
	/// object, each rolled independently when loot is generated. This lets common loot (e.g. trash
	/// gear for a level range) be shared across many creatures without duplicating the entries.
	///
	/// @param project The data project, used to resolve loot table names.
	/// @param lootEntries The repeated field holding the assigned loot table IDs. Modified in place.
	void DrawLootTableAssignment(proto::Project& project, ::google::protobuf::RepeatedField<::google::protobuf::uint32>& lootEntries);
}
