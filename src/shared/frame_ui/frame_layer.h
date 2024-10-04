// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "imagery_section.h"

#include <string>
#include <memory>
#include <vector>


namespace mmo
{
	class GeometryBuffer;


	/// This class represents a layer of a frame. Layers contain assigned objects
	/// that can be rendered in order.
	class FrameLayer final
	{
		friend class Frame;

	public:
		FrameLayer();
		
	public:
		/// Adds a new object to the object list of this layer.
		void AddSection(const ImagerySection& section);
		/// Removes a section by index.
		void RemoveSection(uint32 index);
		/// Removes a section by name.
		void RemoveSection(const std::string& name);
		/// Removes all objects from this layer.
		void RemoveAllSections();
		/// Renders the frame layer, which simply means rendering all attached objects
		/// in order.
		void Render(const Rect& area, const Color& color) const;

	protected:
		/// A vector of all active sessions.
		std::vector<const ImagerySection*> m_sections;
	};
}
