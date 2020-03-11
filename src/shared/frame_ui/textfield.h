// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame.h"


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
		/// Determines whether the text that is rendered is masked.
		inline bool IsTextMasked() const { return m_masked; }
		/// Gets the code point to use when rendering the text masked.
		inline std::string::value_type GetMaskCodePoint() const { return m_maskCodePoint; }
		/// Sets whether the text should be masked.
		void SetTextMasked(bool value);
		/// Sets the mask code point to use when rendering the text masked.
		void SetMaskCodePoint(std::string::value_type value);
		/// 
		virtual const std::string& GetVisualText() const override;

	protected:
		/// Executed when the text was changed.
		virtual void OnTextChanged() override;

	private:
		/// 
		void OnMaskedPropChanged(const Property& property);

	private:
		/// Whether the text of this textfield should be masked.
		bool m_masked;
		/// The character that is used when masking characters for rendering.
		std::string::value_type m_maskCodePoint;
		/// Whether the masked text value needs an update.
		mutable bool m_maskTextDirty;
		/// The cached mask text.
		mutable std::string m_maskText;
	};
}
