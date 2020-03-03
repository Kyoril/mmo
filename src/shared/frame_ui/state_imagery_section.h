// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_layer.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <string>
#include <utility>
#include <vector>


namespace mmo
{
	class GeometryBuffer;

	/// This class represents the visuals of a frame type for a single named state.
	/// It consists of layers, which again consist of frame components that actually
	/// render the frame geometry.
	class StateImagerySection final
		: public NonCopyable
	{
	public:
		/// Initializes the StateImagery class, assigning it a name.
		StateImagerySection(std::string name);

	public:
		/// Adds a new layer to this section.
		void AddLayer(std::unique_ptr<FrameLayer> layer);
		/// Removes a layer by name.
		void RemoveLayer(const std::string& name);
		/// Removes a layer by index.
		void RemoveLayer(uint32 index);

		/// Renders this state imagery.
		void Render(GeometryBuffer& buffer) const
		{
			// Iterate through each layer and call the Render method
			for (const auto& layer : m_layers)
			{
				layer->Render(buffer);
			}
		}

	public:
		/// Gets the name of this imagery.
		inline const std::string& GetName() const { return m_name; }

	private:
		/// The name of this imagery.
		std::string m_name;
		/// The layers that make up this imagery and allow components to be ordered.
		std::vector<std::unique_ptr<FrameLayer>> m_layers;
	};
}
