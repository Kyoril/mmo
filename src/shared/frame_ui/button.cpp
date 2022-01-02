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
	}

	void Button::SetLuaClickedHandler(const luabind::object & fn)
	{
		m_clickedHandler = fn;
	}
}
