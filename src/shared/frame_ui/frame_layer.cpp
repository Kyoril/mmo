// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "frame_layer.h"
#include "geometry_buffer.h"

#include "base/macros.h"


namespace mmo
{
	FrameLayer::FrameLayer()
	{
	}

	void FrameLayer::AddSection(const ImagerySection & section)
	{
		m_sections.push_back(&section);
	}

	void FrameLayer::RemoveSection(uint32 index)
	{
		ASSERT(index < m_sections.size());
		m_sections.erase(m_sections.begin() + index);
	}

	void FrameLayer::RemoveSection(const std::string & name)
	{
		const auto it = std::remove_if(m_sections.begin(), m_sections.end(), [&name](const ImagerySection*& section) {
			return name == section->GetName();
		});

		// Erase all sections that have been found
		m_sections.erase(it, m_sections.end());
	}

	void FrameLayer::RemoveAllSections()
	{
		m_sections.clear();
	}

	void FrameLayer::Render(Frame& frame) const
	{
		for (const auto& section : m_sections)
		{
			section->Render(frame);
		}
	}
}
