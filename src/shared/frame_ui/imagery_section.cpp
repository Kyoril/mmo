// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
			m_components.emplace_back(component->Copy());
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

		auto it = m_components.begin() + index;
		m_components.erase(it);
	}

	/// Renders this state imagery.

	void ImagerySection::RemoveAllComponent()
	{
		m_components.clear();
	}

	void ImagerySection::Render(const Rect& area, const Color& color) const
	{
		for (const auto& component : m_components)
		{
			component->Render(area, color);
		}
	}
}
