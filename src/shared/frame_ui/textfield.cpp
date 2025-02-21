// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "textfield.h"

#include "frame_mgr.h"
#include "text_component.h"

#include "log/default_log_levels.h"


namespace mmo
{
	TextField::TextField(const std::string& type, const std::string& name)
		: Frame(type, name)
		, m_masked(false)
		, m_maskCodePoint('*')
		, m_maskTextDirty(true)
		, m_cursor(-1)
		, m_horzAlign(HorizontalAlignment::Left)
		, m_vertAlign(VerticalAlignment::Center)
		, m_textAreaOffset(Point(10.0f, 10.0f), Size())
		, m_acceptsTab(false)
		, m_enabledColor(1.0f, 1.0f, 1.0f)
		, m_disabledColor(0.5f, 0.5f, 0.5f)
	{
		// Add the masked property
		m_propConnections += AddProperty("Masked", "false").Changed.connect(this, &TextField::OnMaskedPropChanged);
		m_propConnections += AddProperty("AcceptsTab", "false").Changed.connect(this, &TextField::OnAcceptTabChanged);
		m_propConnections += AddProperty("EnabledTextColor", "FFFFFFFF").Changed.connect(this, &TextField::OnEnabledTextColorChanged);
		m_propConnections += AddProperty("DisabledTextColor", "FF808080").Changed.connect(this, &TextField::OnDisabledTextColorChanged);

		// Text fields are focusable by default
		m_focusable = true;
	}

	void TextField::Copy(Frame & other)
	{
		Frame::Copy(other);

		const auto otherTextField = dynamic_cast<TextField*>(&other);
		if (!otherTextField)
		{
			return;
		}

		otherTextField->m_textAreaOffset = m_textAreaOffset;
		otherTextField->m_cursor = m_cursor;
		otherTextField->m_masked = m_masked;
		otherTextField->m_vertAlign = m_vertAlign;
		otherTextField->m_horzAlign = m_horzAlign;
		otherTextField->m_acceptsTab = m_acceptsTab;
		otherTextField->m_enabledColor = m_enabledColor;
		otherTextField->m_disabledColor = m_disabledColor;
	}

	void TextField::SetTextMasked(bool value)
	{
		if (m_masked != value)
		{
			m_masked = value;
			m_needsRedraw = true;
		}
	}

	void TextField::SetMaskCodePoint(std::string::value_type value)
	{
		if (m_maskCodePoint != value)
		{
			m_maskCodePoint = value;
			m_maskTextDirty = true;

			if (m_masked)
			{
				m_needsRedraw = true;
			}
		}
	}

	const std::string & TextField::GetVisualText() const
	{
		if (m_masked)
		{
			if (m_maskTextDirty)
			{
				m_maskText.clear();

				const std::string& text = GetText();
				m_maskText.reserve(text.length());

				for (const auto& c : text)
				{
					m_maskText.push_back(m_maskCodePoint);
				}

				m_maskTextDirty = false;
			}

			return m_maskText;
		}

		return Frame::GetVisualText();
	}

	int32 TextField::GetCursorAt(const Point & position)
	{
		// Get the font to use
		FontPtr font = GetFont();
		if (!font)
		{
			return -1;
		}

		const float textScale = FrameManager::Get().GetUIScale().y;

		// Check for out of bounds
		if (position.x <= m_textAreaOffset.left)
		{
			return 0;
		}
		if (position.x >= GetAbsoluteFrameRect().GetWidth() - m_textAreaOffset.right)
		{
			return m_text.length();
		}

		// Now iterate the string value
		float x = m_textAreaOffset.left;

		// Index
		int32 index = 0;

		// Iterate
		for (const auto& codepoint : m_text)
		{
			const FontGlyph* glyph = font->GetGlyphData(m_masked ? m_maskCodePoint : codepoint);
			if (glyph != nullptr)
			{				
				// Advance offset
				const float advance = glyph->GetAdvance(textScale);
				if (x + advance > position.x)
				{
					return index;
				}

				// Advance
				x += advance;
			}

			// Increase index
			index++;
		}

		return index;
	}

	void TextField::SetHorzAlignment(HorizontalAlignment value) noexcept
	{
		m_horzAlign = value;
		Invalidate();
	}

	void TextField::SetVertAlignment(VerticalAlignment value) noexcept
	{
		m_vertAlign = value;
		Invalidate();
	}

	void TextField::SetEnabledTextColor(const Color & value) noexcept
	{
		m_enabledColor = value;
		Invalidate();
	}

	void TextField::SetDisabledTextColor(const Color & value) noexcept
	{
		m_disabledColor = value;
		Invalidate();
	}

	float TextField::GetCursorOffset() const
	{
		if (m_cursor <= 0)
		{
			return m_textAreaOffset.left;
		}
		
		const float textScale = FrameManager::Get().GetUIScale().y;

		// Check for font
		const auto font = GetFont();
		if (!font)
		{
			return m_textAreaOffset.left;
		}

		float x = m_textAreaOffset.left;

		int index = 0;

		// Iterate
		for (const auto& codepoint : m_text)
		{
			if (index == m_cursor)
			{
				return x;
			}

			const FontGlyph* glyph = font->GetGlyphData(m_masked ? m_maskCodePoint : codepoint);
			if (glyph != nullptr)
			{
				// Advance offset
				const float advance = glyph->GetAdvance(textScale);

				// Advance
				x += advance;
			}

			// Increase index
			index++;
		}

		return x;
	}

	void TextField::OnMouseDown(MouseButton button, int32 buttons, const Point & position)
	{
		// If the left mouse button is pressed...
		if (button == MouseButton::Left)
		{
			// Convert position to local position
			const Point localPosition = position - GetAbsoluteFrameRect().GetPosition();
			
			// Try to find cursor position
			m_cursor = GetCursorAt(localPosition);

			// Redraw to display the cursor
			m_needsRedraw = true;
		}

		// Raise the signal from the super class
		Frame::OnMouseDown(button, buttons, position);
		abort_emission();
	}

	void TextField::OnMouseUp(MouseButton button, int32 buttons, const Point& position)
	{
		Frame::OnMouseUp(button, buttons, position);
		abort_emission();
	}

	void TextField::OnKeyDown(Key key)
	{
		abort_emission();

		if (!AcceptsTab() && key == 0x09)		// VK_TAB
		{
			return;
		}

		if (key == 0x0D)	// VK_RETURN
		{
			return;
		}
		
		if (key == 0x08)		// VK_BACKSPACE
		{
			if (m_text.empty())
				return;

			if (m_cursor == -1 || m_cursor >= m_text.length())
			{
				m_text.pop_back();
				m_cursor = m_text.length();
			}
			else if(m_cursor > 0)
			{
				m_text.erase(m_text.begin() + m_cursor - 1);
				m_cursor = std::max(0, m_cursor - 1);
			}
			else
			{
				// Nothing to do
				return;
			}

			OnTextChanged();

			m_needsLayout = true;
			m_needsRedraw = true;
		}
	}

	void TextField::OnKeyChar(uint16 codepoint)
	{
		abort_emission();

		if (!AcceptsTab() && codepoint == 0x09)		// VK_TAB
		{
			return;
		}

		if (codepoint == 0x0D)	// VK_RETURN
		{
			return;
		}
		
		if (codepoint == 0x8)
			return;

		if (m_cursor == -1 || m_cursor >= m_text.length())
		{
			m_text.push_back(static_cast<char>(codepoint & 0xFF));
			m_cursor = m_text.length();
		}
		else
		{
			m_text.insert(m_text.begin() + m_cursor, static_cast<char>(codepoint & 0xFF));
			m_cursor++;
		}

		OnTextChanged();

		m_needsLayout = true;
		m_needsRedraw = true;
	}

	void TextField::OnKeyUp(Key key)
	{
		Frame::OnKeyUp(key);

		abort_emission();
	}

	void TextField::OnInputCaptured()
	{
		m_needsRedraw = true;
	}

	void TextField::OnInputReleased()
	{
		m_needsRedraw = true;
	}

	void TextField::OnTextChanged()
	{
		// Invalidate masked text
		m_maskTextDirty = true;

		// Call superclass method
		Frame::OnTextChanged();
	}

	void TextField::OnMaskedPropChanged(const Property& property)
	{
		SetTextMasked(property.GetBoolValue());
	}

	void TextField::OnAcceptTabChanged(const Property& property)
	{
		SetAcceptsTab(property.GetBoolValue());
	}

	void TextField::OnEnabledTextColorChanged(const Property& property)
	{
		argb_t argb;

		std::stringstream colorStream;
		colorStream.str(property.GetValue());
		colorStream.clear();
		colorStream >> std::hex >> argb;

		m_enabledColor = argb;

		Invalidate(false);
	}

	void TextField::OnDisabledTextColorChanged(const Property& property)
	{
		argb_t argb;

		std::stringstream colorStream;
		colorStream.str(property.GetValue());
		colorStream.clear();
		colorStream >> std::hex >> argb;

		m_disabledColor = argb;

		Invalidate(false);
	}
}
