// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "imagery_section.h"

#include "base/macros.h"

#include <algorithm>


namespace mmo
{
	ImagerySection::ImagerySection(std::string name)
		: m_name(std::move(name))
	{
	}

	void ImagerySection::AddComponent(std::shared_ptr<FrameComponent>& component)
	{
		m_components.emplace_back(component);
	}

	void ImagerySection::RemoveComponent(uint32 index)
	{
		ASSERT(index < m_components.size());
		m_components.erase(m_components.begin() + index);
	}

	/// Renders this state imagery.

	void ImagerySection::RemoveAllComponent()
	{
		m_components.clear();
	}

	void ImagerySection::Render(Frame & frame) const
	{
		for (const auto& component : m_components)
		{
			component->Render(frame);
		}
	}
}
