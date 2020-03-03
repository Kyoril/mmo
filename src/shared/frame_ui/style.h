// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "state_imagery.h"

#include <string>
#include <map>
#include <memory>


namespace mmo
{
	class StateImagery;


	/// This class contains informations about how to render a frame.
	class Style final
	{
	public:
		/// Initializes a new instance of the Style class.
		/// @param name Name of this style.
		Style(std::string name);

	public:
		/// Adds a new state imagery.
		void AddStateImagery(std::unique_ptr<StateImagery> stateImagery);
		/// Removes a state imagery by name.
		void RemoveStateImagery(const std::string& name);
		/// Gets a state imagery by name. Don't keep a pointer on the result, as it is destroyed when
		/// the style instance is destroyed!
		/// @param name Name of the state imagery.
		/// @return nullptr if no such state imagery exists.
		StateImagery* GetStateImageryByName(const std::string& name) const;

	public:
		/// Gets the name of this style.
		inline const std::string& GetName() const { return m_name; }

	private:
		/// Name of this style.
		std::string m_name;
		/// Contains all state imageries of this style by name.
		std::map<std::string, std::unique_ptr<StateImagery>> m_stateImageriesByName;
	};
}
