// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "button.h"


namespace mmo
{
	Button::Button(const std::string & type, const std::string & name)
		: Frame(type, name)
	{
	}

	void Button::OnMouseUp(MouseButton button, int32 buttons, const Point & position)
	{
		if (button == MouseButton::Left)
		{
			const Rect frame = GetAbsoluteFrameRect();
			if (frame.IsPointInRect(position))
			{
				// Notify that the button has been clicked
				Clicked();
			}
		}

		// Call super class method
		Frame::OnMouseUp(button, buttons, position);
	}
}
