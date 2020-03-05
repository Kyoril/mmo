// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "style.h"
#include "imagery_section.h"
#include "state_imagery.h"

#include "base/macros.h"

#include <utility>
#include <algorithm>


namespace mmo
{
	Style::Style(std::string name)
		: m_name(std::move(name))
	{

	}

	void Style::AddImagerySection(std::shared_ptr<ImagerySection>& section)
	{
		ASSERT(section);
		ASSERT(m_sectionsByName.find(section->GetName()) == m_sectionsByName.end());

		m_sectionsByName[section->GetName()] = section;
	}

	void Style::RemoveImagerySection(const std::string & name)
	{
		const auto it = m_sectionsByName.find(name);
		ASSERT(it != m_sectionsByName.end());

		// TODO: Removing an imagery section will result in undefined behavior if the section
		// is still referenced by a layer in a state imagery.

		m_sectionsByName.erase(it);
	}

	ImagerySection * Style::GetImagerySectionByName(const std::string & name) const
	{
		const auto it = m_sectionsByName.find(name);
		return (it == m_sectionsByName.end()) ? nullptr : it->second.get();
	}

	void Style::AddStateImagery(std::shared_ptr<StateImagery>& stateImagery)
	{
		ASSERT(stateImagery);
		ASSERT(m_stateImageriesByName.find(stateImagery->GetName()) == m_stateImageriesByName.end());

		m_stateImageriesByName[stateImagery->GetName()] = stateImagery;
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
