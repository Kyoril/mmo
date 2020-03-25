// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "textfield.h"
#include "text_component.h"

#include "log/default_log_levels.h"


namespace mmo
{
	TextField::TextField(const std::string & type, const std::string & name)
		: Frame(type, name)
		, m_masked(false)
		, m_maskCodePoint('*')
		, m_maskTextDirty(true)
		, m_cursor(-1)
	{
		// Add the masked property
		Property& prop = AddProperty("Masked", "false");
		m_propConnections += prop.Changed.connect(this, &TextField::OnMaskedPropChanged);

		Property& textSectionProp = AddProperty("TextSection");
		m_propConnections += textSectionProp.Changed.connect(this, &TextField::OnTextSectionPropChanged);

		// Text fields are focusable by default
		m_focusable = true;
	}

	void TextField::Copy(Frame & other)
	{
		Frame::Copy(other);

		TextField& field = static_cast<TextField&>(other);
		field.m_textSectionName = m_textSectionName;
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
		// Try to obtain the text section
		auto* textSection = GetTextSection();
		if (!textSection)
		{
			return -1;
		}

		// Find the text component
		TextComponent* textComponent = textSection->GetFirstTextComponent();
		if (!textComponent)
		{
			return -1;
		}

		// Check for out of bounds
		if (position.x <= textComponent->GetInset().left)
		{
			return 0;
		}
		if (position.x >= GetAbsoluteFrameRect().GetWidth() - textComponent->GetInset().right)
		{
			return m_text.length();
		}

		// Now iterate the string value
		float x = textComponent->GetInset().left;

		// Index
		int32 index = 0;

		// Iterate
		for (const auto& codepoint : m_text)
		{
			const FontGlyph* glyph = textComponent->GetFont()->GetGlyphData(m_masked ? m_maskCodePoint : codepoint);
			if (glyph != nullptr)
			{				
				// Advance offset
				const float advance = glyph->GetAdvance(1.0f);
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

	void TextField::OnMouseDown(MouseButton button, int32 buttons, const Point & position)
	{
		// If the left mouse button is pressed...
		if (button == MouseButton::Left)
		{
			// Convert position to local position
			const Point localPosition = position - GetAbsoluteFrameRect().GetPosition();
			
			// Try to find cursor position
			m_cursor = GetCursorAt(localPosition);
			DLOG("Index: " << m_cursor);

			// Redraw to display the cursor
			m_needsRedraw = true;
		}

		// Raise the signal from the super class
		Frame::OnMouseDown(button, buttons, position);
	}

	void TextField::OnKeyDown(Key key)
	{
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

	void TextField::OnTextSectionPropChanged(const Property & property)
	{
		// Apply text section name
		m_textSectionName = property.GetValue();
	}

	ImagerySection * TextField::GetTextSection() const
	{
		return GetImagerySectionByName(m_textSectionName);
	}
}
