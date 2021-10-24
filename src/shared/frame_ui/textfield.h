// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame.h"

#include "text_component.h"


namespace mmo
{
	/// This class inherits the Frame class and extends it by some editable text field logic.
	class TextField
		: public Frame
	{
	public:
		explicit TextField(const std::string& type, const std::string& name);
		virtual ~TextField() = default;

	public:
		virtual void Copy(Frame& other) override;
		/// Determines whether the text that is rendered is masked.
		inline bool IsTextMasked() const { return m_masked; }
		/// Gets the code point to use when rendering the text masked.
		inline std::string::value_type GetMaskCodePoint() const { return m_maskCodePoint; }
		/// Sets whether the text should be masked.
		void SetTextMasked(bool value);
		/// Sets whether the text field accepts tabs as text input.
		void SetAcceptsTab(bool value) { m_acceptsTab = value; }
		/// Determines whether the text field accepts tabs as text input.
		bool AcceptsTab() const noexcept { return m_acceptsTab; }
		/// Sets the mask code point to use when rendering the text masked.
		void SetMaskCodePoint(std::string::value_type value);
		/// 
		virtual const std::string& GetVisualText() const override;
		/// Tries to find the cursor position based on the given local coordinate.
		int32 GetCursorAt(const Point& position);

		inline HorizontalAlignment GetHorzAlignmnet() const noexcept { return m_horzAlign; }
		inline VerticalAlignment GetVertAlignment() const noexcept { return m_vertAlign; }
		inline const Color& GetEnabledTextColor() const noexcept { return m_enabledColor; }
		inline const Color& GetDisabledTextColor() const noexcept { return m_disabledColor; }
		void SetHorzAlignment(HorizontalAlignment value) noexcept;
		void SetVertAlignment(VerticalAlignment value) noexcept;
		void SetEnabledTextColor(const Color& value) noexcept;
		void SetDisabledTextColor(const Color& value) noexcept;
		inline Rect GetTextAreaOffsets() const noexcept { return m_textAreaOffset; }
		float GetCursorOffset() const;

	public:
		virtual void OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;
		virtual void OnKeyDown(Key key) override;
		virtual void OnKeyChar(uint16 codepoint) override;
		virtual void OnInputCaptured() override;
		virtual void OnInputReleased() override;

	protected:
		/// Executed when the text was changed.
		virtual void OnTextChanged() override;

	private:
		/// 
		void OnMaskedPropChanged(const Property& property);
		void OnAcceptTabChanged(const Property& property);

	private:
		/// Whether the text of this textfield should be masked.
		bool m_masked;
		/// The character that is used when masking characters for rendering.
		std::string::value_type m_maskCodePoint;
		/// Whether the masked text value needs an update.
		mutable bool m_maskTextDirty;
		/// The cached mask text.
		mutable std::string m_maskText;
		/// Cursor index.
		int32 m_cursor;

		HorizontalAlignment m_horzAlign;

		VerticalAlignment m_vertAlign;

		Color m_enabledColor;
		Color m_disabledColor;

		Rect m_textAreaOffset;

		bool m_acceptsTab;
	};
}
