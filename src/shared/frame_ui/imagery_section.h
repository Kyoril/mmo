// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_component.h"

#include "base/typedefs.h"

#include <string>
#include <utility>
#include <vector>
#include <memory>


namespace mmo
{
	class Frame;


	/// This class represents the visuals of a frame type for a single named state.
	/// It consists of layers, which again consist of frame components that actually
	/// render the frame geometry.
	class ImagerySection final
	{
	public:
		ImagerySection() = default;
		/// Initializes the StateImagery class, assigning it a name.
		ImagerySection(std::string name);
		ImagerySection(const ImagerySection& other);
		ImagerySection& operator=(const ImagerySection& other);

	private:
		void Copy(const ImagerySection& other);

	public:
		/// Sets the frame for all components.
		void SetComponentFrame(Frame& frame);

		/// Adds a new layer to this section.
		void AddComponent(std::unique_ptr<FrameComponent> component);

		/// Removes a layer by index.
		void RemoveComponent(uint32 index);

		/// Removes a layer by index.
		void RemoveAllComponent();

		/// Renders this state imagery.
		void Render(const Rect& area, const Color& color) const;

	public:
		/// Gets the name of this imagery.
		inline const std::string& GetName() const { return m_name; }

	private:
		/// The name of this imagery.
		std::string m_name;
		/// The components that this section contains.
		std::vector<std::unique_ptr<FrameComponent>> m_components;
	};
}
