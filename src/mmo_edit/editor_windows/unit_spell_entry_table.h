// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "proto_data/project.h"

#include <google/protobuf/repeated_ptr_field.h>

namespace mmo
{
	/// @brief Draws an editable table of unit spell entries (spell, priority, cooldowns, probability) with
	///        an "Add" button and a per-row remove button. Used for creature spells and pet AI spells.
	/// @param id Unique ImGui id of the table within the current id stack.
	/// @param project The project supplying the selectable spells.
	/// @param spells The spell entries to edit in place.
	void DrawUnitSpellEntryTable(const char* id, proto::Project& project, google::protobuf::RepeatedPtrField<proto::UnitSpellEntry>& spells);
}
