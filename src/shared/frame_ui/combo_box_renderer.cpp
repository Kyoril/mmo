// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combo_box_renderer.h"
#include "combo_box.h"
#include "state_imagery.h"
#include "frame.h"

namespace mmo
{
	ComboBoxRenderer::ComboBoxRenderer(const std::string& name)
		: FrameRenderer(name)
	{
	}

	void ComboBoxRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		std::string activeState = "Disabled";

		if (m_frame->IsEnabled(false) && m_comboBox)
		{
			if (m_comboBox->IsOpen())
			{
				activeState = "Open";
			}
			else if (m_frame->IsHovered())
			{
				activeState = "Hovered";
			}
			else
			{
				activeState = "Normal";
			}
		}

		const auto* imagery = m_frame->GetStateImageryByName(activeState);
		if (!imagery)
		{
			imagery = m_frame->GetStateImageryByName("Normal");
		}

		if (imagery)
		{
			imagery->Render(m_frame->GetAbsoluteFrameRect(), colorOverride.value_or(Color::White));
		}
	}

	void ComboBoxRenderer::NotifyFrameAttached()
	{
		FrameRenderer::NotifyFrameAttached();
		m_comboBox = dynamic_cast<ComboBox*>(m_frame);
		ASSERT(m_comboBox);
	}

	void ComboBoxRenderer::NotifyFrameDetached()
	{
		m_comboBox = nullptr;
		FrameRenderer::NotifyFrameDetached();
	}
}
