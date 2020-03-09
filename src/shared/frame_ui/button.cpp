// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "button.h"


namespace mmo
{
	static const std::string ButtonClickedEvent("OnClicked");

	Button::Button(const std::string & type, const std::string & name)
		: Frame(type, name)
	{
		// Register events
		RegisterEvent(ButtonClickedEvent);
	}

	void Button::OnMouseUp(MouseButton button, int32 buttons, const Point & position)
	{
		if (button == MouseButton::Left)
		{
			const Rect frame = GetAbsoluteFrameRect();
			if (frame.IsPointInRect(position))
			{
				// Try to execute lua script event first
				const FrameEvent* clickedEvent = FindEvent(ButtonClickedEvent);
				if (clickedEvent)
				{
					(*clickedEvent)();
				}

				// Afterwards, signal the event
				Clicked();
			}
		}

		// Call super class method
		Frame::OnMouseUp(button, buttons, position);
	}
}
