// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "state_imagery_section.h"

#include "base/macros.h"

#include <algorithm>


namespace mmo
{
	StateImagerySection::StateImagerySection(std::string name)
		: m_name(std::move(name))
	{
	}

	void StateImagerySection::AddLayer(std::unique_ptr<FrameLayer> layer)
	{
		m_layers.emplace_back(std::move(layer));
	}

	void StateImagerySection::RemoveLayer(const std::string & name)
	{
		auto lastIt = std::remove_if(m_layers.begin(), m_layers.end(), [&name](const std::unique_ptr<FrameLayer>& layer) {
			return layer->GetName() == name;
		});

		ASSERT(lastIt != m_layers.end());
		m_layers.erase(lastIt, m_layers.end());
	}

	void StateImagerySection::RemoveLayer(uint32 index)
	{
		ASSERT(index < m_layers.size());
		m_layers.erase(m_layers.begin() + index);
	}
}
