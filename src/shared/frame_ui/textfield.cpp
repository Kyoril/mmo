// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "textfield.h"

#include "log/default_log_levels.h"


namespace mmo
{
	TextField::TextField(const std::string & type, const std::string & name)
		: Frame(type, name)
		, m_masked(false)
		, m_maskCodePoint('*')
		, m_maskTextDirty(true)
	{
		// Add the masked property
		Property& prop = AddProperty("Masked", "false");
		m_propConnections += prop.Changed.connect(this, &TextField::OnMaskedPropChanged);

		// Text fields are focusable by default
		m_focusable = true;
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

	void TextField::OnMouseDown(MouseButton button, int32 buttons, const Point & position)
	{
		// If the left mouse button is pressed...
		if (button == MouseButton::Left)
		{
			// Convert position to local position
			const Point localPosition = position - GetAbsoluteFrameRect().GetPosition();
			
			// TODO: Calculate the cursor position in the textfield
			DLOG("Position: " << localPosition.x << ", " << localPosition.y);
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

			m_text.pop_back();

			OnTextChanged();

			m_needsLayout = true;
			m_needsRedraw = true;
		}
	}

	void TextField::OnKeyChar(uint16 codepoint)
	{
		if (codepoint == 0x8)
			return;

		m_text.push_back(static_cast<char>(codepoint & 0xFF));

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
}
