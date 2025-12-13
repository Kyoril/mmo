// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "proto_data/project.h"

#include <nlohmann/json.hpp>

namespace mmo
{
	/// Tools for managing spells through MCP protocol
	class SpellTools
	{
	public:
		/// Initializes the spell tools with a project reference
		/// @param project The project containing spell data
		explicit SpellTools(proto::Project &project);

	public:
		/// Lists all spells with optional filtering
		/// @param args JSON arguments containing optional filters
		/// @return JSON array of spells
		nlohmann::json ListSpells(const nlohmann::json &args);

		/// Gets detailed information about a specific spell
		/// @param args JSON arguments containing spell ID
		/// @return JSON object with spell details
		nlohmann::json GetSpellDetails(const nlohmann::json &args);

		/// Creates a new spell
		/// @param args JSON arguments with spell properties
		/// @return JSON object with the created spell
		nlohmann::json CreateSpell(const nlohmann::json &args);

		/// Updates an existing spell
		/// @param args JSON arguments with spell ID and properties to update
		/// @return JSON object with the updated spell
		nlohmann::json UpdateSpell(const nlohmann::json &args);

		/// Deletes a spell
		/// @param args JSON arguments containing spell ID
		/// @return JSON object with success status
		nlohmann::json DeleteSpell(const nlohmann::json &args);

		/// Searches for spells based on criteria
		/// @param args JSON arguments with search parameters
		/// @return JSON array of matching spells
		nlohmann::json SearchSpells(const nlohmann::json &args);

	private:
		/// Converts a protobuf SpellEntry to JSON
		/// @param entry The spell entry to convert
		/// @param includeDetails Whether to include full details
		/// @return JSON representation of the spell
		nlohmann::json SpellEntryToJson(const proto::SpellEntry &entry, bool includeDetails = false) const;

		/// Converts JSON to SpellEntry fields
		/// @param json The JSON data
		/// @param entry The entry to update
		void JsonToSpellEntry(const nlohmann::json &json, proto::SpellEntry &entry);

		/// Gets the spell school name as string
		/// @param spellSchool The spell school enum value
		/// @return String name of the school
		String GetSpellSchoolName(uint32 spellSchool) const;

		/// Gets the power type name as string
		/// @param powerType The power type enum value
		/// @return String name of the power type
		String GetPowerTypeName(int32 powerType) const;

		/// Gets the spell effect type name as string
		/// @param effectType The effect type enum value
		/// @return String name of the effect type
		String GetSpellEffectName(uint32 effectType) const;

		/// Gets the spell mechanic name as string
		/// @param mechanic The mechanic enum value
		/// @return String name of the mechanic
		String GetSpellMechanicName(uint32 mechanic) const;

	private:
		proto::Project &m_project;
	};
}
