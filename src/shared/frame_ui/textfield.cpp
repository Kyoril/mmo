// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "textfield.h"


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
