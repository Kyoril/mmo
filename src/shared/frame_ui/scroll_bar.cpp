// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "scroll_bar.h"

#include "frame_mgr.h"
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

			// Remove anchor points from the thumb as they should only be used initially
			m_thumbFrame->ClearAnchors();
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
		UpdateThumb();
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
		UpdateThumb();
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
			UpdateThumb();

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

		// For vertical scrollbars, use the Y position
		if (m_orientation == ScrollBarOrientation::Vertical)
		{
			// Get the top position of the thumb
			const float thumbTop = thumb->GetPosition().y;
			const float availableSpace = thumb->GetVerticalMax() - thumb->GetVerticalMin() - thumb->GetHeight();
			
			// Calculate normalized position (0.0-1.0)
			if (availableSpace <= 0.0f)
			{
				return 0.0f;
			}
			
			return Clamp((thumbTop - thumb->GetVerticalMin()) / availableSpace, 0.0f, 1.0f);
		}
		// For horizontal scrollbars, use the X position
		else
		{
			// Get the left position of the thumb
			const float thumbLeft = thumb->GetPosition().x;
			const float availableSpace = thumb->GetHorizontalMax() - thumb->GetHorizontalMin() - thumb->GetWidth();
			
			// Calculate normalized position (0.0-1.0)
			if (availableSpace <= 0.0f)
			{
				return 0.0f;
			}
			
			return Clamp((thumbLeft - thumb->GetHorizontalMin()) / availableSpace, 0.0f, 1.0f);
		}
	}

	void ScrollBar::OnAreaChanged(const Rect& newArea)
	{
		Thumb* thumb = GetThumb();
		if (!thumb)
		{
			return;
		}

		// Configure thumb based on orientation
		if (m_orientation == ScrollBarOrientation::Vertical)
		{
			// Get button positions and sizes
			float topButtonBottom = 0.0f;
			float bottomButtonTop = 0.0f;
			
			if (m_upFrame)
			{
				topButtonBottom = m_upFrame->GetAbsoluteFrameRect().bottom;
			}
			else
			{
				topButtonBottom = newArea.top;
			}
			
			if (m_downFrame)
			{
				bottomButtonTop = m_downFrame->GetAbsoluteFrameRect().top;
			}
			else
			{
				bottomButtonTop = newArea.bottom;
			}

			const float invScaleY = 1.0f / FrameManager::Get().GetUIScale().y;

			// Calculate the available space for the thumb
			float minPos = topButtonBottom;
			float maxPos = bottomButtonTop;
			
			// Ensure min is less than max with enough space for the thumb
			if ((maxPos - minPos) * invScaleY < thumb->GetHeight() + 1.0f)
			{
				// Not enough space, adjust to minimum required
				float midPoint = (minPos + maxPos) / 2.0f;
				float halfThumbHeight = thumb->GetHeight() / 2.0f + 0.5f;
				
				minPos = midPoint - halfThumbHeight;
				maxPos = midPoint + halfThumbHeight;
			}

			// Set vertical range excluding button areas
			thumb->SetVerticalRange((minPos - newArea.top) * invScaleY, (maxPos - newArea.top) * invScaleY);
			thumb->SetVerticalMovement(true);
			thumb->SetHorizontalMovement(false);
		}
		else
		{
			// Get button positions and sizes
			float leftButtonLeft = 0.0f;
			float leftButtonRight = 0.0f;
			float rightButtonLeft = 0.0f;
			float rightButtonRight = 0.0f;
			
			if (m_upFrame)
			{
				leftButtonLeft = m_upFrame->GetX();
				leftButtonRight = leftButtonLeft + m_upFrame->GetWidth();
			}
			else
			{
				leftButtonLeft = newArea.left;
				leftButtonRight = newArea.left;
			}
			
			if (m_downFrame)
			{
				rightButtonLeft = m_downFrame->GetX();
				rightButtonRight = rightButtonLeft + m_downFrame->GetWidth();
			}
			else
			{
				rightButtonLeft = newArea.right;
				rightButtonRight = newArea.right;
			}
			
			// Calculate the available space for the thumb
			float minPos = leftButtonRight;
			float maxPos = rightButtonLeft;
			
			// Ensure min is less than max with enough space for the thumb
			if (maxPos - minPos < thumb->GetWidth() + 1.0f)
			{
				// Not enough space, adjust to minimum required
				float midPoint = (minPos + maxPos) / 2.0f;
				float halfThumbWidth = thumb->GetWidth() / 2.0f + 0.5f;
				
				minPos = midPoint - halfThumbWidth;
				maxPos = midPoint + halfThumbWidth;
			}
			
			// Set horizontal range excluding button areas
			thumb->SetHorizontalRange(minPos, maxPos);
			thumb->SetHorizontalMovement(true);
			thumb->SetVerticalMovement(false);
		}

		// Update thumb position based on current value
		UpdateThumb();
	}

	void ScrollBar::UpdateThumb()
	{
		Thumb* thumb = GetThumb();
		if (!thumb)
		{
			return;
		}

		// Calculate normalized value (0.0-1.0)
		float normalizedValue = 0.0f;
		if (m_maximum > m_minimum)
		{
			normalizedValue = (m_value - m_minimum) / (m_maximum - m_minimum);
		}

		// Clamp normalized value to valid range
		normalizedValue = Clamp(normalizedValue, 0.0f, 1.0f);

		Point position = thumb->GetPosition();
		position.x = (m_upFrame ? m_upFrame->GetAbsoluteFrameRect().left - GetAbsoluteFrameRect().left : 0.0f) * (1.0f / FrameManager::Get().GetUIScale().y);
		
		// Handle vertical scrollbar
		if (m_orientation == ScrollBarOrientation::Vertical)
		{
			// Calculate available space for thumb movement
			float availableSpace = thumb->GetVerticalMax() - thumb->GetVerticalMin() - thumb->GetHeight();
			if (availableSpace <= 0.0f)
			{
				// Not enough space, center the thumb
				position.y = (thumb->GetVerticalMin() + thumb->GetVerticalMax() - thumb->GetHeight()) / 2.0f;
			}
			else
			{
				// Calculate thumb position based on normalized value
				position.y = thumb->GetVerticalMin() + normalizedValue * availableSpace;

				// Ensure thumb stays within bounds
				if (position.y < thumb->GetVerticalMin())
				{
					position.y = thumb->GetVerticalMin();
				}
				else if (position.y > thumb->GetVerticalMax() - thumb->GetHeight() * FrameManager::Get().GetUIScale().y)
				{
					position.y = thumb->GetVerticalMax() - thumb->GetHeight() * FrameManager::Get().GetUIScale().y;
				}
			}
		}
		// Handle horizontal scrollbar
		else
		{
			// Calculate available space for thumb movement
			float availableSpace = thumb->GetHorizontalMax() - thumb->GetHorizontalMin() - thumb->GetWidth();
			if (availableSpace <= 0.0f)
			{
				// Not enough space, center the thumb
				position.x = (thumb->GetHorizontalMin() + thumb->GetHorizontalMax() - thumb->GetWidth()) / 2.0f;
			}
			else
			{
				// Calculate thumb position based on normalized value
				position.x = thumb->GetHorizontalMin() + normalizedValue * availableSpace;
				
				// Ensure thumb stays within bounds
				if (position.x < thumb->GetHorizontalMin())
				{
					position.x = thumb->GetHorizontalMin();
				}
				else if (position.x > thumb->GetHorizontalMax() - thumb->GetWidth())
				{
					position.x = thumb->GetHorizontalMax() - thumb->GetWidth();
				}
			}
		}
		
		thumb->SetPosition(position);
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
		const float normalizedValue = GetValueFromThumb();
		
		// Convert normalized value (0.0-1.0) to actual value range
		const float newValue = m_minimum + normalizedValue * (m_maximum - m_minimum);
		
		// Update the scroll bar value
		SetValue(newValue);
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
		if (m_maximum == m_minimum)
		{
			if (m_upFrame)
			{
				m_upFrame->Disable();
			}
			if (m_downFrame)
			{
				m_downFrame->Disable();
			}
			if (m_thumbFrame)
			{
				m_thumbFrame->Disable();
			}
		}
		else
		{
			if (m_thumbFrame)
			{
				m_thumbFrame->Enable();
			}

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
}
