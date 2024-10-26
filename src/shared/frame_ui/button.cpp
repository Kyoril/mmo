// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "button.h"

#include "frame_mgr.h"
#include "log/default_log_levels.h"


namespace mmo
{
	static const std::string ButtonClickedEvent("OnClicked");

	Button::Button(const std::string & type, const std::string & name)
		: Frame(type, name)
	{
		m_propConnections += AddProperty("Checkable").Changed.connect(this, &Button::OnCheckablePropertyChanged);
		m_propConnections += AddProperty("Checked").Changed.connect(this, &Button::OnCheckedPropertyChanged);

		// Buttons are focusable by default
		m_focusable = true;
	}

	void Button::Copy(Frame& other)
	{
		Frame::Copy(other);

		const auto otherButton = dynamic_cast<Button*>(&other);
		if (!otherButton)
		{
			return;
		}

		otherButton->m_checkable = m_checkable;
		otherButton->m_checked = m_checked;
	}

	void Button::OnMouseDown(MouseButton button, int32 buttons, const Point& position)
	{
		Frame::OnMouseDown(button, buttons, position);

		SetButtonState(ButtonState::Pushed);

		abort_emission();
	}

	void Button::OnMouseUp(MouseButton button, int32 buttons, const Point & position)
	{
		if (button == MouseButton::Left)
		{
			SetButtonState(IsHovered() ? ButtonState::Hovered : ButtonState::Normal);

			// Toggle checked state if the button is checkable
			if (IsCheckable())
			{
				SetChecked(!IsChecked());
			}
		}

		// Call super class method
		Frame::OnMouseUp(button, buttons, position);
		abort_emission();
	}

	void Button::OnMouseEnter()
	{
		Frame::OnMouseEnter();

		if (m_state != button_state::Pushed)
		{
			SetButtonState(button_state::Hovered);
		}
	}

	void Button::OnMouseLeave()
	{
		Frame::OnMouseLeave();

		if (m_state != button_state::Pushed)
		{
			SetButtonState(button_state::Normal);
		}
	}

	void Button::SetButtonState(const ButtonState state)
	{
		if (state == m_state)
		{
			return;
		}

		m_state = state;
		Invalidate();
	}

	void Button::OnCheckedPropertyChanged(const Property& property)
	{
		SetChecked(property.GetBoolValue());
	}

	void Button::OnCheckablePropertyChanged(const Property& property)
	{
		SetCheckable(property.GetBoolValue());
	}
}
