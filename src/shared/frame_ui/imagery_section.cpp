// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "imagery_section.h"
#include "text_component.h"

#include "base/macros.h"

#include <algorithm>


namespace mmo
{
	ImagerySection::ImagerySection(std::string name)
		: m_name(std::move(name))
		, m_firstTextComponent(nullptr)
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
		m_firstTextComponent = nullptr;

		for (const auto& component : other.m_components)
		{
			m_components.emplace_back(component->Copy());

			if (!m_firstTextComponent)
			{
				CheckForTextComponent(*m_components.back());
			}
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

		if (!m_firstTextComponent)
		{
			CheckForTextComponent(*m_components.back());
		}
	}

	void ImagerySection::RemoveComponent(uint32 index)
	{
		ASSERT(index < m_components.size());

		auto it = m_components.begin() + index;
		if (it->get() == m_firstTextComponent)
		{
			m_firstTextComponent = nullptr;
		}

		m_components.erase(it);
	}

	/// Renders this state imagery.

	void ImagerySection::RemoveAllComponent()
	{
		m_components.clear();
		m_firstTextComponent = nullptr;
	}

	void ImagerySection::Render() const
	{
		for (const auto& component : m_components)
		{
			component->Render();
		}
	}

	void ImagerySection::CheckForTextComponent(FrameComponent & component)
	{
		auto *textComponent = dynamic_cast<TextComponent*>(&component);
		if (textComponent)
		{
			m_firstTextComponent = textComponent;
		}
	}
}
