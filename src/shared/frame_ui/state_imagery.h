// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "state_imagery_section.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <string>
#include <vector>


namespace mmo
{
	class GeometryBuffer;


	/// This class represents the visuals of a frame type for a single named state.
	/// It consists of layers, which again consist of frame components that actually
	/// render the frame geometry.
	class StateImagery final
		: public NonCopyable
	{
	public:
		/// Initializes the StateImagery class, assigning it a name. This name is equal to a
		/// control's state, as it is defined by the frame's renderer.
		StateImagery(std::string name);

	public:
		/// Adds a new section to the state imagery.
		/// @param section The section to add.
		void AddSection(std::unique_ptr<StateImagerySection> section);
		/// Removes a section by name.
		void RemoveSection(const std::string& name);
		/// Removes a section by index.
		void RemoveSection(uint32 index);

		/// Renders this state imagery.
		void Render(GeometryBuffer& buffer) const;

	public:
		/// Gets the name of this imagery.
		inline const std::string& GetName() const { return m_name; }

	private:
		/// The name of this imagery.
		std::string m_name;
		/// The sections that make up the whole frame in the current state.
		std::vector<std::unique_ptr<StateImagerySection>> m_sections;
	};
}
