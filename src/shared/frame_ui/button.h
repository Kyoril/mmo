// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "frame.h"


namespace mmo
{
	/// This class inherits the Frame class and extends it by some button logic.
	class Button
		: public Frame
	{
	public:
		/// Triggered when the button was clicked.
		signal<void()> Clicked;

	public:
		explicit Button(const std::string& type, const std::string& name);
		virtual ~Button() = default;

	public:
		virtual void OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;

		virtual void OnMouseUp(MouseButton button, int32 buttons, const Point& position) override;

	public:
		void SetLuaClickedHandler(const luabind::object& fn);

	private:
		luabind::object m_clickedHandler;
	};
}
