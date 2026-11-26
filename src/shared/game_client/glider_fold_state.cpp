// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glider_fold_state.h"

#include <algorithm>

namespace mmo
{
	glider_event::Type GliderFoldState::Update(const bool gliding, const float deltaTime)
	{
		const float step = std::max(deltaTime, 0.0f) / FoldSeconds;

		if (gliding)
		{
			m_unfold = std::min(m_unfold + step, 1.0f);
			if (!m_visible)
			{
				m_visible = true;
				return glider_event::Opened;
			}

			return glider_event::None;
		}

		if (!m_visible)
		{
			return glider_event::None;
		}

		m_unfold = std::max(m_unfold - step, 0.0f);
		if (m_unfold <= 0.0f)
		{
			m_visible = false;
			return glider_event::Closed;
		}

		return glider_event::None;
	}
}
