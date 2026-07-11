#pragma once
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "frame.h"

namespace mmo
{
	class Thumb;

	/// Enumerates possible scroll bar orientation values.
	enum class ScrollBarOrientation : uint8
	{
		/// Horizontal scroll bar
		Horizontal,

		/// Vertical scroll bar.
		Vertical
	};

	/// This class represents a scroll bar.
	class ScrollBar : public Frame
	{
	public:
		ScrollBar(const std::string& type, const std::string& name);
		virtual ~ScrollBar() override = default;

	public:
		virtual void Copy(Frame& other) override;

		virtual void OnLoad() override;

	public:
		/// Sets the orientation of the scroll bar.
		void SetOrientation(const ScrollBarOrientation orientation) { m_orientation = orientation; Invalidate(); }

		/// Gets the orientation of the scroll bar.
		[[nodiscard]] ScrollBarOrientation GetOrientation() const { return m_orientation; }

		[[nodiscard]] float GetMinimumValue() const { return m_minimum; }

		[[nodiscard]] float GetMaximumValue() const { return m_maximum; }

		[[nodiscard]] float GetValue() const { return m_value; }

		[[nodiscard]] float GetStep() const { return m_step; }

		void SetMinimumValue(const float minimum);

		void SetMaximumValue(const float maximum);

		void SetValue(const float value);

		void SetStep(const float step);

		Thumb* GetThumb() const;

		float GetValueFromThumb() const;

		void UpdateThumb();

		void SetOnValueChangedHandler(const luabind::object& handler) { m_onValueChanged = handler; }

	protected:
		void OnAreaChanged(const Rect& newArea) override;

	protected:
		void OnOrientationPropertyChanged(const Property& property);
		void OnMinimumPropertyChanged(const Property& property);
		void OnMaximumPropertyChanged(const Property& property);
		void OnValuePropertyChanged(const Property& property);
		void OnStepPropertyChanged(const Property& property);

	private:
		/// Wires up the child button and thumb frames. Called from OnLoad for frames created
		/// by the layout loader and from Copy for frames created at runtime through Clone,
		/// which never receive an OnLoad call.
		void SetupChildFrames();

		void OnThumbPositionChanged(Thumb& thumb);

		void OnUpButtonClicked();

		void OnDownButtonClicked();

		void UpdateScrollButtons();

	private:
		ScrollBarOrientation m_orientation;
		float m_minimum = 0.0f;
		float m_maximum = 100.0f;
		float m_step = 1.0f;
		float m_value = 0.0f;

		Frame* m_upFrame = nullptr;
		Frame* m_downFrame = nullptr;
		Thumb* m_thumbFrame = nullptr;

		scoped_connection m_onThumbPositionChanged;
		scoped_connection m_onUpButtonClicked;
		scoped_connection m_onDownButtonClicked;

		luabind::object m_onValueChanged{};
	};
}
