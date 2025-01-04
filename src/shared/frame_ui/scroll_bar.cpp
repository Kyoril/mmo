// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "scroll_bar.h"

#include "thumb.h"

namespace mmo
{
	ScrollBar::ScrollBar(const std::string& type, const std::string& name)
		: Frame(type, name)
		, m_orientation(ScrollBarOrientation::Vertical)
	{
		m_propConnections += AddProperty("Orientation").Changed.connect(this, &ScrollBar::OnOrientationPropertyChanged);
	}

	void ScrollBar::Copy(Frame& other)
	{
		Frame::Copy(other);

		// Copy scroll bar orientation property
		if (const auto otherScrollBar = dynamic_cast<ScrollBar*>(&other))
		{
			otherScrollBar->m_orientation = m_orientation;
			otherScrollBar->m_onValueChanged = m_onValueChanged;
		}
	}

	void ScrollBar::OnLoad()
	{
		Frame::OnLoad();

		// Setup child frames
		m_upFrame = dynamic_cast<Button*>(GetChild(0));
		if (m_upFrame)
		{
			m_upFrame->Clicked.connect(this, &ScrollBar::OnUpButtonClicked);
		}

		m_downFrame = dynamic_cast<Button*>(GetChild(1));
		if (m_downFrame)
		{
			m_downFrame->Clicked.connect(this, &ScrollBar::OnDownButtonClicked);
		}

		m_thumbFrame = dynamic_cast<Thumb*>(GetChild(2));
		if (m_thumbFrame)
		{
			m_onThumbPositionChanged = m_thumbFrame->thumbPositionChanged.connect(this, &ScrollBar::OnThumbPositionChanged);
		}
	}

	void ScrollBar::SetMinimumValue(const float minimum)
	{
		if (minimum == m_minimum)
		{
			return;
		}

		if (minimum > m_maximum)
		{
			ELOG("Minimum value cannot be greater than maximum value for scroll bar " << GetName());
			return;
		}

		m_minimum = minimum;
		UpdateScrollButtons();

		if (GetValue() < minimum)
		{
			SetValue(minimum);
		}

		Invalidate();
	}

	void ScrollBar::SetMaximumValue(const float maximum)
	{
		if (maximum == m_maximum)
		{
			return;
		}

		if (maximum < m_minimum)
		{
			ELOG("Maximum value cannot be less than minimum value for scroll bar " << GetName());
			return;
		}

		m_maximum = maximum;
		UpdateScrollButtons();

		if (GetValue() > maximum)
		{
			SetValue(maximum);
		}

		Invalidate();
	}

	void ScrollBar::SetValue(float value)
	{
		if (m_value == value)
		{
			return;
		}

		if (value > GetMaximumValue())
		{
			value = GetMaximumValue();
		}
		else if (value < GetMinimumValue())
		{
			value = GetMinimumValue();
		}

		if (m_value != value)
		{
			m_value = value;

			UpdateScrollButtons();

			if (m_onValueChanged.is_valid())
			{
				m_onValueChanged(this, m_value);
			}

			Invalidate();
		}
	}

	void ScrollBar::SetStep(const float step)
	{
		if (m_step == step)
		{
			return;
		}

		if (step > GetMaximumValue() - GetMinimumValue())
		{
			ELOG("Step value cannot be greater than the difference between maximum and minimum values for scroll bar " << GetName());
			return;
		}

		if (step <= 0.0f)
		{
			ELOG("Step value must be greater than zero for scroll bar " << GetName());
			return;
		}

		m_step = step;
	}

	Thumb* ScrollBar::GetThumb() const
	{
		return m_thumbFrame;
	}

	float ScrollBar::GetValueFromThumb() const
	{
		Thumb* thumb = GetThumb();
		if (!thumb)
		{
			return 0.0f;
		}

		// Check the area of the thumb
		const float y = thumb->GetY();
		if (y <= thumb->GetVerticalMin())
		{
			return 0.0f;
		}

		if (y >= thumb->GetVerticalMax())
		{
			return 1.0f;
		}

		return (y - thumb->GetVerticalMin()) / (thumb->GetVerticalMax() - thumb->GetVerticalMin());
	}

	void ScrollBar::OnAreaChanged(const Rect& newArea)
	{
		Thumb* thumb = GetThumb();
		if (!thumb)
		{
			return;
		}

		thumb->SetVerticalRange(newArea.top, newArea.bottom);
		thumb->SetVerticalMovement(true);

		// TODO: Limit thumb position and set it to the current value
	}

	void ScrollBar::OnOrientationPropertyChanged(const Property& property)
	{
		if (property.GetValue() == "HORIZONTAL")
		{
			SetOrientation(ScrollBarOrientation::Horizontal);
		}
		else if(property.GetValue() == "VERTICAL")
		{
			SetOrientation(ScrollBarOrientation::Vertical);
		}
		else
		{
			ELOG("Invalid orientation property value for scroll bar " << GetName() << ": '" << property.GetValue() << "'");
		}
	}

	void ScrollBar::OnMinimumPropertyChanged(const Property& property)
	{
		float value;

		std::istringstream strm(property.GetValue());
		strm >> value;

		SetMinimumValue(value);
	}

	void ScrollBar::OnMaximumPropertyChanged(const Property& property)
	{
		float value;

		std::istringstream strm(property.GetValue());
		strm >> value;

		SetMaximumValue(value);
	}

	void ScrollBar::OnValuePropertyChanged(const Property& property)
	{
		float value;

		std::istringstream strm(property.GetValue());
		strm >> value;

		SetValue(value);
	}

	void ScrollBar::OnStepPropertyChanged(const Property& property)
	{
		float value;

		std::istringstream strm(property.GetValue());
		strm >> value;

		SetStep(value);
	}

	void ScrollBar::OnThumbPositionChanged(Thumb& thumb)
	{
		// Derive value from thumb position

		

		if (m_onValueChanged.is_valid())
		{
			m_onValueChanged(this, m_value);
		}
	}

	void ScrollBar::OnUpButtonClicked()
	{
		SetValue(GetValue() - GetStep());
	}

	void ScrollBar::OnDownButtonClicked()
	{
		SetValue(GetValue() + GetStep());
	}

	void ScrollBar::UpdateScrollButtons()
	{
		if (m_upFrame)
		{
			if (m_value == GetMinimumValue())
			{
				m_upFrame->Disable();
			}
			else
			{
				m_upFrame->Enable();
			}
		}

		if (m_downFrame)
		{
			if (m_value == GetMaximumValue())
			{
				m_downFrame->Disable();
			}
			else
			{
				m_downFrame->Enable();
			}
		}
	}
}
