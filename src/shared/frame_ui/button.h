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
		virtual void Copy(Frame& other) override;

	public:
		virtual void OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;

		virtual void OnMouseUp(MouseButton button, int32 buttons, const Point& position) override;

		bool IsChecked() const { return IsCheckable() && m_checked; }

		void SetChecked(const bool checked) { m_checked = checked; Invalidate(); }

		bool IsCheckable() const { return m_checkable; }

		void SetCheckable(const bool checkable) { m_checkable = checkable; Invalidate(); }

	public:
		void SetLuaClickedHandler(const luabind::object& fn);

	private:
		/// Executed when the Checked property was changed.
		void OnCheckedPropertyChanged(const Property& property);

		/// Executed when the Checkable property was changed.
		void OnCheckablePropertyChanged(const Property& property);


	private:
		luabind::object m_clickedHandler;

		bool m_checkable{ false };

		bool m_checked{ false };
	};
}
