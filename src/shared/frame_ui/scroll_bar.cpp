// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "scroll_bar.h"

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

		if (GetValue() < minimum)
		{
			SetValue(minimum);
		}

		if (m_step > m_maximum - m_minimum)
		{
			SetStep(m_maximum - m_minimum);
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

		if (GetValue() < maximum)
		{
			SetValue(maximum);
		}

		if (m_step > m_maximum - m_minimum)
		{
			SetStep(m_maximum - m_minimum);
		}

		Invalidate();
	}

	void ScrollBar::SetValue(const float value)
	{
		if (m_value == value)
		{
			return;
		}

		if (value > GetMaximumValue())
		{
			m_value = GetMaximumValue();
		}
		else if (value < GetMinimumValue())
		{
			m_value = GetMinimumValue();
		}
		else
		{
			m_value = value;
		}

		Invalidate();
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
}
