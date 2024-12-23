// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_layer.h"

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
	{
		friend class Frame;

	public:
		StateImagery() = default;
		/// Initializes the StateImagery class, assigning it a name. This name is equal to a
		/// control's state, as it is defined by the frame's renderer.
		StateImagery(std::string name);

	public:
		/// Adds a new layer to the state imagery.
		/// @param layer The layer to add.
		void AddLayer(FrameLayer& layer);
		/// Removes a layer by index.
		void RemoveLayer(uint32 index);
		/// Removes all layers.
		void RemoveAllLayers();

		/// Renders this state imagery.
		void Render(const Rect& area, const Color& color) const;

	public:
		/// Gets the name of this imagery.
		inline const std::string& GetName() const { return m_name; }

	private:
		/// The name of this imagery.
		std::string m_name;
		/// The layers that make up this state imagery.
		std::vector<FrameLayer> m_layers;
	};
}
