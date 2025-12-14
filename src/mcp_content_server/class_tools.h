// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "proto_data/project.h"

#include <nlohmann/json.hpp>

namespace mmo
{
	/// Tools for managing character classes through MCP protocol
	class ClassTools
	{
	public:
		/// Initializes the class tools with a project reference
		/// @param project The project containing class data
		explicit ClassTools(proto::Project &project);

	public:
		/// Lists all classes with optional filtering
		/// @param args JSON arguments containing optional filters
		/// @return JSON array of classes
		nlohmann::json ListClasses(const nlohmann::json &args);

		/// Gets detailed information about a specific class
		/// @param args JSON arguments containing class ID
		/// @return JSON object with class details
		nlohmann::json GetClassDetails(const nlohmann::json &args);

		/// Creates a new class
		/// @param args JSON arguments containing class properties
		/// @return JSON object with the created class ID
		nlohmann::json CreateClass(const nlohmann::json &args);

		/// Updates an existing class
		/// @param args JSON arguments containing class ID and updated properties
		/// @return JSON object confirming the update
		nlohmann::json UpdateClass(const nlohmann::json &args);

		/// Deletes a class
		/// @param args JSON arguments containing class ID
		/// @return JSON object confirming deletion
		nlohmann::json DeleteClass(const nlohmann::json &args);

		/// Searches for classes by name
		/// @param args JSON arguments containing search query
		/// @return JSON array of matching classes
		nlohmann::json SearchClasses(const nlohmann::json &args);

		/// Adds a spell to a class at a specific level
		/// @param args JSON arguments containing class ID, spell ID, and level
		/// @return JSON object confirming the addition
		nlohmann::json AddClassSpell(const nlohmann::json &args);

		/// Removes a spell from a class
		/// @param args JSON arguments containing class ID and spell ID
		/// @return JSON object confirming the removal
		nlohmann::json RemoveClassSpell(const nlohmann::json &args);

	private:
		/// Converts a ClassEntry to JSON (basic info)
		/// @param entry The class entry to convert
		/// @return JSON representation of the class
		nlohmann::json ClassEntryToJson(const proto::ClassEntry &entry, bool detailed = false);

		/// Converts JSON to ClassEntry
		/// @param json The JSON object to convert
		/// @param entry The class entry to populate
		void JsonToClassEntry(const nlohmann::json &json, proto::ClassEntry &entry);

		/// Converts power type enum to string
		/// @param powerType The power type enum value
		/// @return String representation
		String PowerTypeToString(proto::ClassEntry_PowerType powerType);

		/// Converts string to power type enum
		/// @param str The string to convert
		/// @return Power type enum value
		proto::ClassEntry_PowerType StringToPowerType(const String &str);

	private:
		proto::Project &m_project;
	};
}
