// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combo_box.h"
#include "frame_mgr.h"

#include "luabind_noboost/luabind/luabind.hpp"
#include "log/default_log_levels.h"

namespace mmo
{
	const String ComboBox::Type("ComboBox");

	ComboBox::ComboBox(const std::string& type, const std::string& name)
		: Frame(type, name)
	{
		m_focusable = true;
	}

	void ComboBox::Copy(Frame& other)
	{
		Frame::Copy(other);

		const auto* src = dynamic_cast<const ComboBox*>(&other);
		if (!src)
		{
			return;
		}

		auto& dst = dynamic_cast<ComboBox&>(other);
		dst.m_items = m_items;
		dst.m_selectedIndex = m_selectedIndex;
		dst.m_isOpen = false;
	}

	void ComboBox::AddItem(const std::string& text, const std::string& userData)
	{
		m_items.push_back({ text, userData });
		Invalidate();
	}

	void ComboBox::ClearItems()
	{
		m_items.clear();
		m_selectedIndex = -1;
		SetText("");
		Invalidate();
	}

	std::string ComboBox::GetItemText(const int32 index) const
	{
		const int32 i = index - 1;
		if (i < 0 || i >= static_cast<int32>(m_items.size()))
		{
			return {};
		}

		return m_items[i].text;
	}

	std::string ComboBox::GetItemUserData(const int32 index) const
	{
		const int32 i = index - 1;
		if (i < 0 || i >= static_cast<int32>(m_items.size()))
		{
			return {};
		}

		return m_items[i].userData;
	}

	void ComboBox::SetSelectedIndex(const int32 index)
	{
		const int32 i = index - 1;
		if (i < -1 || i >= static_cast<int32>(m_items.size()))
		{
			return;
		}

		m_selectedIndex = index;

		// Sync frame text so the renderer's TextComponent shows the selected item
		if (i >= 0)
		{
			SetText(m_items[i].text);
		}
		else
		{
			SetText("");
		}

		Invalidate();

		if (m_onSelectionChanged.is_valid())
		{
			try
			{
				m_onSelectionChanged(this, index, GetSelectedText(), GetSelectedUserData());
			}
			catch (const luabind::error& e)
			{
				ELOG("ComboBox::SetSelectedIndex lua error: " << e.what());
			}
		}
	}

	std::string ComboBox::GetSelectedText() const
	{
		return GetItemText(m_selectedIndex);
	}

	std::string ComboBox::GetSelectedUserData() const
	{
		return GetItemUserData(m_selectedIndex);
	}

	void ComboBox::SetPopupFrame(Frame* popup)
	{
		m_popupFrame = popup ? popup->shared_from_this() : nullptr;
	}

	void ComboBox::Open()
	{
		m_isOpen = true;
		Invalidate();
		FrameManager::Get().SetActiveComboBox(std::static_pointer_cast<ComboBox>(shared_from_this()));
	}

	void ComboBox::Close()
	{
		if (!m_isOpen)
		{
			return;
		}

		m_isOpen = false;
		Invalidate();
		FrameManager::Get().ClearActiveComboBox(this);
	}

	void ComboBox::Dismiss()
	{
		Close();

		if (m_onDismissed.is_valid())
		{
			try
			{
				m_onDismissed(this);
			}
			catch (const luabind::error& e)
			{
				ELOG("ComboBox::Dismiss lua error: " << e.what());
			}
		}
	}

	void ComboBox::Toggle()
	{
		if (m_isOpen)
		{
			Close();
		}
		else
		{
			Open();
		}
	}

	bool ComboBox::OnMouseDown(MouseButton button, int32 buttons, const Point& position)
	{
		Frame::OnMouseDown(button, buttons, position);

		if (button == MouseButton::Left)
		{
			m_pushed = true;
			Invalidate();
		}

		abort_emission();
		return true;
	}

	bool ComboBox::OnMouseUp(MouseButton button, int32 buttons, const Point& position)
	{
		Frame::OnMouseUp(button, buttons, position);

		if (button == MouseButton::Left && m_pushed)
		{
			m_pushed = false;
			Invalidate();

			if (m_onClicked.is_valid())
			{
				try
				{
					m_onClicked(this);
				}
				catch (const luabind::error& e)
				{
					ELOG("ComboBox::OnMouseUp lua error: " << e.what());
				}
			}
		}

		abort_emission();
		return true;
	}

	void ComboBox::OnMouseEnter()
	{
		Frame::OnMouseEnter();
		m_hovered = true;
		Invalidate();
	}

	void ComboBox::OnMouseLeave()
	{
		Frame::OnMouseLeave();
		m_hovered = false;
		m_pushed = false;
		Invalidate();
	}
}
