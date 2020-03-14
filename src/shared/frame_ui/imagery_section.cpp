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

	ImagerySection::ImagerySection(const ImagerySection & other)
	{
		Copy(other);
	}

	ImagerySection & ImagerySection::operator=(const ImagerySection & other)
	{
		Copy(other);
		return *this;
	}

	void ImagerySection::Copy(const ImagerySection & other)
	{
		m_name = other.m_name;

		m_components.clear();

		for (const auto& component : other.m_components)
		{
			m_components.push_back(component->Copy());
		}
	}

	void ImagerySection::SetComponentFrame(Frame & frame)
	{
		for (const auto& component : m_components)
		{
			component->SetFrame(frame);
		}
	}

	void ImagerySection::AddComponent(std::unique_ptr<FrameComponent> component)
	{
		m_components.emplace_back(std::move(component));
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

	void ImagerySection::Render() const
	{
		for (const auto& component : m_components)
		{
			component->Render();
		}
	}
}
