// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame.h"

namespace mmo
{
	/// A drop-down selection control.
	/// The frame displays the currently selected item.  When the user clicks it
	/// the clicked handler fires so that Lua can show a dropdown popup and call
	/// SetSelectedIndex() once the user picks an entry.
	class ComboBox : public Frame
	{
	public:
		static const String Type;

	public:
		explicit ComboBox(const std::string& type, const std::string& name);
		~ComboBox() override = default;

		void Copy(Frame& other) override;

	public:
		/// Appends an item to the item list.
		void AddItem(const std::string& text, const std::string& userData);

		/// Removes all items.
		void ClearItems();

		/// Returns the number of items.
		[[nodiscard]] int32 GetItemCount() const { return static_cast<int32>(m_items.size()); }

		/// Returns the display text of item at index (1-based).
		[[nodiscard]] std::string GetItemText(int32 index) const;

		/// Returns the userData string of item at index (1-based).
		[[nodiscard]] std::string GetItemUserData(int32 index) const;

		/// Returns the 1-based index of the selected item, or -1 if nothing is selected.
		[[nodiscard]] int32 GetSelectedIndex() const { return m_selectedIndex; }

		/// Sets the selected item by 1-based index.  Fires OnSelectionChanged.
		void SetSelectedIndex(int32 index);

		/// Returns the display text of the selected item, or an empty string.
		[[nodiscard]] std::string GetSelectedText() const;

		/// Returns the userData of the selected item, or an empty string.
		[[nodiscard]] std::string GetSelectedUserData() const;

		/// Returns true when the combo box is in the "open" state.
		[[nodiscard]] bool IsOpen() const { return m_isOpen; }

		/// Sets the open state directly (does not fire Lua callbacks).
		void Open();
		void Close();
		void Toggle();

		/// Lua callback fired when the combo bar is clicked.
		void SetOnClickedHandler(const luabind::object& fn) { m_onClicked = fn; }

		/// Lua callback fired with (self, index, text, userData) when selection changes.
		void SetOnSelectionChanged(const luabind::object& fn) { m_onSelectionChanged = fn; }

		// Frame overrides
		bool OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;
		bool OnMouseUp(MouseButton button, int32 buttons, const Point& position) override;
		void OnMouseEnter() override;
		void OnMouseLeave() override;

	private:
		struct Item
		{
			std::string text;
			std::string userData;
		};

		std::vector<Item> m_items;
		int32 m_selectedIndex{ -1 };
		bool m_isOpen{ false };
		bool m_hovered{ false };
		bool m_pushed{ false };

		luabind::object m_onClicked;
		luabind::object m_onSelectionChanged;
	};
}
