// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combo_box.h"
#include "frame_mgr.h"
#include "button.h"

#include "base/utilities.h"

#include "luabind_noboost/luabind/luabind.hpp"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cstdlib>

namespace mmo
{
	namespace
	{
		/// Height of a single dropdown item in virtual (4K space) pixels. Must match the
		/// AbsDimension height of the ComboBoxItemTemplate frame defined in ComboBoxDropdown.xml.
		constexpr float ItemHeight = 60.0f;
		/// Padding between the popup border and the item list, in virtual pixels.
		constexpr float BorderPadding = 8.0f;

		/// Names of the shared dropdown frames defined in ComboBoxDropdown.xml.
		const char* PopupFrameName = "ComboBoxDropdownFrame";
		const char* PopupContainerName = "ComboBoxDropdownContainer";
		const char* ItemTemplateName = "ComboBoxItemTemplate";
		const char* GameParentName = "GameParent";
	}

	const String ComboBox::Type("ComboBox");

	ComboBox::ComboBox(const std::string& type, const std::string& name)
		: Frame(type, name)
	{
		m_focusable = true;

		m_propConnections += AddProperty("MaxDropHeight", "600").Changed.connect(this, &ComboBox::OnMaxDropHeightChanged);
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
		dst.m_maxDropHeight = m_maxDropHeight;
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

	void ComboBox::SetItemColor(const int32 index, const std::string& color)
	{
		const int32 i = index - 1;
		if (i < 0 || i >= static_cast<int32>(m_items.size()))
		{
			return;
		}

		m_items[i].color = color;
	}

	void ComboBox::SetMaxDropHeight(const float height)
	{
		m_maxDropHeight = std::max(ItemHeight, height);
	}

	void ComboBox::OnMaxDropHeightChanged(const Property& property)
	{
		const float value = static_cast<float>(std::atof(property.GetValue().c_str()));
		if (value > 0.0f)
		{
			m_maxDropHeight = std::max(ItemHeight, value);
		}
	}

	void ComboBox::SetPopupFrame(Frame* popup)
	{
		m_popupFrame = popup ? popup->shared_from_this() : nullptr;
	}

	void ComboBox::Open()
	{
		if (m_items.empty())
		{
			return;
		}

		m_isOpen = true;
		BuildPopup();
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
		DestroyPopup();
		Invalidate();
		FrameManager::Get().ClearActiveComboBox(this);
	}

	void ComboBox::BuildPopup()
	{
		auto& frameMgr = FrameManager::Get();

		const FramePtr popup = frameMgr.Find(PopupFrameName);
		const FramePtr container = frameMgr.Find(PopupContainerName);
		const FramePtr itemTemplate = frameMgr.Find(ItemTemplateName);
		const FramePtr gameParent = frameMgr.Find(GameParentName);
		if (!popup || !container || !itemTemplate)
		{
			ELOG("ComboBox '" << GetName() << "' cannot open: dropdown popup frames are not loaded");
			return;
		}

		m_dropContainer = container;
		m_scrollOffset = 0.0f;

		// ── Item buttons ─────────────────────────────────────────
		container->RemoveAllChildren();
		m_itemFrames.clear();
		m_itemConnections.disconnect();

		const int32 itemCount = static_cast<int32>(m_items.size());
		for (int32 i = 0; i < itemCount; ++i)
		{
			const Item& item = m_items[i];

			FramePtr btn = itemTemplate->Clone();
			btn->SetText(item.text);

			// Per-item color (e.g. quality colors for the loot threshold combo).
			if (!item.color.empty())
			{
				btn->SetProperty("TextColor", item.color);
			}

			// Highlight the currently selected item.
			if (auto* button = dynamic_cast<Button*>(btn.get()))
			{
				if (i + 1 == m_selectedIndex)
				{
					button->SetCheckable(true);
					button->SetChecked(true);
				}
			}

			// Clip each item to the container so off-screen entries are scissored away.
			btn->SetClippedByParent(true);

			btn->ClearAnchors();
			btn->SetAnchor(AnchorPoint::Top, AnchorPoint::Top, nullptr, i * ItemHeight);
			btn->SetAnchor(AnchorPoint::Left, AnchorPoint::Left, nullptr, 0.0f);
			btn->SetAnchor(AnchorPoint::Right, AnchorPoint::Right, nullptr, 0.0f);
			btn->Show();

			const int32 index1Based = i + 1;
			m_itemConnections += btn->Clicked.connect([this, index1Based]()
			{
				SetSelectedIndex(index1Based);
				// Dismiss() closes C++ state and fires the Lua dismiss handler so the popup
				// owner can clear its tracking reference.
				Dismiss();
			});

			container->AddChild(btn);
			m_itemFrames.push_back(btn);
		}

		// ── Sizing & positioning ──────────────────────────────────
		// GetX / GetY return screen pixels  → divide by UIScale to get virtual 4K pixels.
		// GetWidth returns screen pixels (anchor-driven) → divide by UIScale.
		// GetHeight returns virtual pixels (AbsDimension-driven) → add directly.
		const Point scale = frameMgr.GetUIScale();

		m_contentHeight = itemCount * ItemHeight;
		m_viewportHeight = std::min(m_contentHeight, m_maxDropHeight);

		const float virtualX = static_cast<float>(GetX()) / scale.x;
		const float virtualY = static_cast<float>(GetY()) / scale.y + GetHeight();
		const float virtualWidth = GetWidth() / scale.x;
		const float popupHeight = m_viewportHeight + BorderPadding * 2.0f;

		popup->ClearAnchors();
		popup->SetAnchor(AnchorPoint::Top, AnchorPoint::Top, gameParent, virtualY);
		popup->SetAnchor(AnchorPoint::Left, AnchorPoint::Left, gameParent, virtualX);
		popup->SetWidth(virtualWidth);
		popup->SetHeight(popupHeight);
		popup->BringToFront();
		popup->Show();

		// Scroll so the currently selected item is visible when the list opens.
		if (m_selectedIndex > 0)
		{
			const float selectedBottom = static_cast<float>(m_selectedIndex) * ItemHeight;
			if (selectedBottom > m_viewportHeight)
			{
				m_scrollOffset = selectedBottom - m_viewportHeight;
			}
		}

		ApplyScrollOffset();

		// Register the popup with the FrameManager so outside-clicks dismiss the combo.
		SetPopupFrame(popup.get());
	}

	void ComboBox::DestroyPopup()
	{
		m_itemConnections.disconnect();
		m_itemFrames.clear();

		if (m_dropContainer)
		{
			m_dropContainer->RemoveAllChildren();
			m_dropContainer.reset();
		}

		if (m_popupFrame)
		{
			m_popupFrame->Hide();
		}
	}

	void ComboBox::ApplyScrollOffset()
	{
		const float maxScroll = std::max(0.0f, m_contentHeight - m_viewportHeight);
		m_scrollOffset = Clamp(m_scrollOffset, 0.0f, maxScroll);

		for (size_t i = 0; i < m_itemFrames.size(); ++i)
		{
			m_itemFrames[i]->SetAnchor(AnchorPoint::Top, AnchorPoint::Top, nullptr,
				static_cast<float>(i) * ItemHeight - m_scrollOffset);
		}
	}

	bool ComboBox::OnPopupMouseWheel(const int32 delta)
	{
		if (!m_isOpen)
		{
			return false;
		}

		// Wheel up (positive delta) scrolls towards the top of the list.
		m_scrollOffset -= static_cast<float>(delta) * ItemHeight;
		ApplyScrollOffset();

		// Always consume while the popup is open so the camera does not zoom underneath it.
		return true;
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
