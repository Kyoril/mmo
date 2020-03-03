// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "style.h"

#include "base/macros.h"

#include <utility>
#include <algorithm>


namespace mmo
{
	Style::Style(std::string name)
		: m_name(std::move(name))
	{

	}

	void Style::AddImagerySection(std::shared_ptr<StateImagerySection>& section)
	{
		ASSERT(section);
		ASSERT(m_sectionsByName.find(section->GetName()) == m_sectionsByName.end());

		m_sectionsByName[section->GetName()] = std::move(section);
	}

	void Style::RemoveImagerySection(const std::string & name)
	{
		const auto it = m_sectionsByName.find(name);
		ASSERT(it != m_sectionsByName.end());

		m_sectionsByName.erase(it);
	}

	StateImagerySection * Style::GetImagerySectionByName(const std::string & name) const
	{
		const auto it = m_sectionsByName.find(name);
		return (it == m_sectionsByName.end()) ? nullptr : it->second.get();
	}

	void Style::AddStateImagery(std::shared_ptr<StateImagery>& stateImagery)
	{
		ASSERT(stateImagery);
		ASSERT(m_stateImageriesByName.find(stateImagery->GetName()) == m_stateImageriesByName.end());

		m_stateImageriesByName[stateImagery->GetName()] = std::move(stateImagery);
	}

	void Style::RemoveStateImagery(const std::string & name)
	{
		const auto it = m_stateImageriesByName.find(name);
		ASSERT(it != m_stateImageriesByName.end());

		m_stateImageriesByName.erase(it);
	}

	StateImagery * Style::GetStateImageryByName(const std::string & name) const
	{
		const auto it = m_stateImageriesByName.find(name);
		return (it == m_stateImageriesByName.end()) ? nullptr : it->second.get();
	}
}
