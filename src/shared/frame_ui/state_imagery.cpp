// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "state_imagery.h"

#include "base/macros.h"

#include <utility>
#include <algorithm>


namespace mmo
{
	StateImagery::StateImagery(std::string name)
		: m_name(std::move(name))
	{
	}

	void StateImagery::AddSection(std::unique_ptr<StateImagerySection> section)
	{
		m_sections.emplace_back(std::move(section));
	}

	void StateImagery::RemoveSection(const std::string & name)
	{
		// Remove section by name
		auto lastIt = std::remove_if(m_sections.begin(), m_sections.end(), [&name](const std::unique_ptr<StateImagerySection>& section) {
			return section->GetName() == name;
		});

		ASSERT(lastIt != m_sections.end());
		m_sections.erase(lastIt, m_sections.end());
	}

	void StateImagery::RemoveSection(uint32 index)
	{
		ASSERT(index < m_sections.size());
		m_sections.erase(m_sections.begin() + index);
	}

	void StateImagery::Render(GeometryBuffer & buffer) const
	{
		for (const auto& section : m_sections)
		{
			section->Render(buffer);
		}
	}
}
