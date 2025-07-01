// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame.h"

#include "text_component.h"
#include "hyperlink.h"


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

		/// Override SetText to reset scroll offset when text changes
		virtual void SetText(std::string text) override;

		/// Sets whether the text field accepts tabs as text input.
		void SetAcceptsTab(bool value) { m_acceptsTab = value; }

		/// Determines whether the text field accepts tabs as text input.
		bool AcceptsTab() const { return m_acceptsTab; }

		/// Sets the mask code point to use when rendering the text masked.
		void SetMaskCodePoint(std::string::value_type value);

		/// Pixel distance from the left edge of the text‑area to the caret, after
		/// stripping inline color codes.
		float GetCaretPixelOffset(float uiScale) const;

		/// Logical position in the underlying string (the index the user is editing). Existing accessor - unchanged.
		std::size_t GetCaretIndex() const { return m_caretIndex; }

		/// 
		virtual const std::string& GetVisualText() const override;

		/// Tries to find the cursor position based on the given local coordinate.
		int32 GetCursorAt(const Point& position);

		inline HorizontalAlignment GetHorzAlignmnet() const { return m_horzAlign; }
		inline VerticalAlignment GetVertAlignment() const { return m_vertAlign; }
		inline const Color& GetEnabledTextColor() const { return m_enabledColor; }
		inline const Color& GetDisabledTextColor() const { return m_disabledColor; }
		void SetHorzAlignment(HorizontalAlignment value);
		void SetVertAlignment(VerticalAlignment value);
		void SetEnabledTextColor(const Color& value);
		void SetDisabledTextColor(const Color& value);
		void SetTextAreaOffset(const Rect& offset);
		const Rect& GetTextAreaOffset() const { return m_textAreaOffset; }
		float GetCursorOffset() const;

		/// Sets the horizontal scroll offset for text display
		void SetScrollOffset(float offset);
		/// Gets the current horizontal scroll offset
		float GetScrollOffset() const { return m_scrollOffset; }
		/// Ensures the cursor is visible by adjusting scroll offset
		void EnsureCursorVisible();
		/// Gets the visible width available for text
		float GetVisibleTextWidth() const;
		/// Gets the parsed plain text (hyperlinks show as display text only)
		const std::string& GetParsedPlainText() const;


	public:
		virtual void OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;
		virtual void OnMouseUp(MouseButton button, int32 buttons, const Point& position) override;
		virtual void OnKeyDown(Key key) override;
		virtual void OnKeyChar(uint16 codepoint) override;
		virtual void OnKeyUp(Key key) override;
		virtual void OnInputCaptured() override;
		virtual void OnInputReleased() override;

	protected:
		/// Executed when the text was changed.
		virtual void OnTextChanged() override;
		/// Override to handle hyperlink rendering and clipping
		virtual void PopulateGeometryBuffer() override;

	private:
		/// 
		void OnMaskedPropChanged(const Property& property);
		void OnAcceptTabChanged(const Property& property);
		void OnEnabledTextColorChanged(const Property& property);
		void OnDisabledTextColorChanged(const Property& property);

		/// Finds the hyperlink token at the given cursor position
		int FindHyperlinkAtPosition(std::size_t cursorPos);
		/// Deletes the hyperlink at the given index
		void DeleteHyperlink(int hyperlinkIndex);
		/// Updates the parsed text and hyperlinks
		void UpdateParsedText();
		/// Maps a plain text position to a raw text position
		std::size_t MapPlainToRawPosition(std::size_t plainPos);
		/// Rebuilds the raw text to contain only the first N plain text characters
		std::string RebuildTextForPlainLength(std::size_t targetPlainLength);
		/// Rebuilds the raw text from the given plain text position to the end
		std::string RebuildTextFromPlainPosition(std::size_t fromPlainPos);

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
		std::size_t m_caretIndex{ 0 };  // logical cursor index in m_editBuffer

		/// Horizontal scroll offset for text display
		float m_scrollOffset;
		/// Parsed text with hyperlinks and colors
		ParsedText m_parsedText;
		/// Whether the parsed text needs updating
		mutable bool m_parsedTextDirty;
	};
}
