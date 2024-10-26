// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "frame.h"


namespace mmo
{
	namespace button_state
	{
		enum Type
		{
			Normal,
			Hovered,
			Pushed
		};
	}


	typedef button_state::Type ButtonState;


	/// This class inherits the Frame class and extends it by some button logic.
	class Button
		: public Frame
	{
	public:
		explicit Button(const std::string& type, const std::string& name);
		virtual ~Button() = default;

	public:
		virtual void Copy(Frame& other) override;

	public:
		virtual void OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;

		virtual void OnMouseUp(MouseButton button, int32 buttons, const Point& position) override;

		virtual void OnMouseEnter() override;

		virtual void OnMouseLeave() override;

		void SetButtonState(const ButtonState state);

		bool IsChecked() const { return IsCheckable() && m_checked; }

		void SetChecked(const bool checked) { m_checked = checked; Invalidate(); }

		bool IsCheckable() const { return m_checkable; }

		void SetCheckable(const bool checkable) { m_checkable = checkable; Invalidate(); }

		ButtonState GetButtonState() const { return m_state; }

	private:
		/// Executed when the Checked property was changed.
		void OnCheckedPropertyChanged(const Property& property);

		/// Executed when the Checkable property was changed.
		void OnCheckablePropertyChanged(const Property& property);


	private:
		bool m_checkable{ false };

		bool m_checked{ false };

		ButtonState m_state{ ButtonState::Normal };
	};
}
