// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "button.h"

#include "log/default_log_levels.h"


namespace mmo
{
	static const std::string ButtonClickedEvent("OnClicked");

	Button::Button(const std::string & type, const std::string & name)
		: Frame(type, name)
	{
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

		if (m_clickedHandler.is_valid())
		{
			otherButton->m_clickedHandler = m_clickedHandler;
		}
	}

	void Button::OnMouseDown(MouseButton button, int32 buttons, const Point& position)
	{
		Frame::OnMouseDown(button, buttons, position);
		abort_emission();
	}

	void Button::OnMouseUp(MouseButton button, int32 buttons, const Point & position)
	{
		if (button == MouseButton::Left)
		{
			if (const Rect frame = GetAbsoluteFrameRect(); frame.IsPointInRect(position))
			{
				// Trigger lua clicked event handler if there is any
				if (m_clickedHandler.is_valid())
				{
					try
					{
						m_clickedHandler(this);
					}
					catch (const luabind::error& e)
					{
						ELOG("Lua error: " << e.what());
					}
				}

				// Afterwards, signal the event
				Clicked();
			}
		}

		// Call super class method
		Frame::OnMouseUp(button, buttons, position);
		abort_emission();
	}

	void Button::SetLuaClickedHandler(const luabind::object & fn)
	{
		m_clickedHandler = fn;
	}
}
