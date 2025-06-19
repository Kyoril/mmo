
#include "cursor.h"

#include "frame_ui/frame_mgr.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"
#include "spell_entry.h"
#include "shared/game_client/game_item_c.h"
#include "shared/game_client/game_bag_c.h"
#include "shared/game_client/item_handle.h"
#include "shared/game_client/object_mgr.h"
#include "shared/game/item.h"
#include "shared/game/object_type_id.h"
#include "shared/client_data/proto_client/spells.pb.h"

#ifdef WIN32
#	include <Windows.h>
#   include <d3d11.h>
#	include <map>
#	include "graphics_d3d11/graphics_device_d3d11.h"

namespace mmo
{
	static std::map<CursorType, HCURSOR> g_cursors;
}
#endif

namespace mmo
{
	void Cursor::Initialize(const proto_client::Project& project)
	{
		m_project = &project;
	}
	void Cursor::SetCursorType(CursorType type)
	{
#ifdef WIN32
		if (g_cursors.contains(type))
		{
			GraphicsDevice::Get().SetHardwareCursor(g_cursors[type]);
			return;
		}
#endif

		m_cursorType = type;
	}

#ifdef WIN32
	HCURSOR CreateCursorFromD3D11Texture(const TexturePtr& texture)
	{
		const PixelFormat format = texture->GetPixelFormat();

		// Only these 4 are supported right now
		if (format != PixelFormat::R8G8B8A8 &&
			format != PixelFormat::B8G8R8A8)
		{
			WLOG("Texture has unsupported format to be used as a hardware cursor!");
			return nullptr;
		}

		// Copy pixel data to a pixel data buffer
		std::vector<uint8> pixelData;
		pixelData.resize(texture->GetPixelDataSize());
		texture->CopyPixelDataTo(pixelData.data());

		// Step 4: Create an HBITMAP from the texture data
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = texture->GetWidth();
		bmi.bmiHeader.biHeight = -static_cast<LONG>(texture->GetHeight());  // Top-down DIB
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		void* bitmapData;
		HBITMAP hBitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bitmapData, nullptr, 0);
		if (hBitmap)
		{
			// Copy texture data row by row (to account for possible row pitch differences)
			uint8_t* srcData = static_cast<uint8_t*>(pixelData.data());
			uint8_t* destData = static_cast<uint8_t*>(bitmapData);

			for (UINT y = 0; y < texture->GetHeight(); ++y) {

				if (format == R8G8B8A8)
				{
					// Ensure RGBA to BGRA conversion before copy
					for (UINT x = 0; x < texture->GetWidth(); ++x)
					{
						destData[0] = srcData[2];  // B
						destData[1] = srcData[1];  // G
						destData[2] = srcData[0];  // R
						destData[3] = srcData[3];  // A

						srcData += 4;
						destData += 4;
					}
				}
				else
				{
					// We can copy data simply as is
					memcpy(destData, srcData, texture->GetWidth() * 4);

					srcData += texture->GetWidth() * 4;
					destData += texture->GetWidth() * 4;
				}
			}
		}

		// Step 6: Create a monochrome mask for transparency
		HBITMAP hMask = CreateBitmap(texture->GetWidth(), texture->GetHeight(), 1, 1, nullptr);

		// Step 7: Set up ICONINFO and create cursor
		ICONINFO iconInfo = {};
		iconInfo.fIcon = FALSE;  // Set to FALSE to create a cursor, not an icon
		iconInfo.xHotspot = 0;  // Center the hotspot
		iconInfo.yHotspot = 0;
		iconInfo.hbmMask = hMask;
		iconInfo.hbmColor = hBitmap;

		HCURSOR hCursor = CreateIconIndirect(&iconInfo);

		// Clean up
		DeleteObject(hBitmap);
		DeleteObject(hMask);

		return hCursor;
	}
#endif

	bool Cursor::LoadCursorTypeFromTexture(CursorType type, const String& textureFileName)
	{
		TexturePtr tex = TextureManager::Get().CreateOrRetrieve(textureFileName);
		if (!tex)
		{
			return false;
		}

#ifdef WIN32
		// Create HCURSOR from texture ptr
		g_cursors[type] = CreateCursorFromD3D11Texture(tex);
#endif

		return false;
	}
	void Cursor::Clear()
	{
		m_type = CursorItemType::None;
		m_itemSlot = -1;

		FrameManager::Get().SetCursorIcon(nullptr, Size(32.0f, 32.0f));
	}

	void Cursor::SetItem(uint32 slot)
	{
		m_type = CursorItemType::Item;
		m_itemSlot = slot;

		UpdateCursorIcon();
	}

	void Cursor::SetSpell(uint32 spell)
	{
		m_type = CursorItemType::Spell;
		m_itemSlot = spell;

		UpdateCursorIcon();
	}
	uint32 Cursor::GetCursorItem() const
	{
		return m_itemSlot;
	}
	std::shared_ptr<GameItemC> Cursor::ResolveItemFromSlot(uint32 slot) const
	{
		const auto player = ObjectMgr::GetActivePlayer();
		if (!player || player->GetTypeId() != ObjectTypeId::Player)
		{
			return nullptr;
		}

		// Resolve the slot to an item GUID using the same logic as in game_script.cpp
		uint64 itemGuid = 0;
		
		// Backpack?
		if ((static_cast<uint16>(slot) >> 8) == player_inventory_slots::Bag_0 && 
			(slot & 0xFF) >= player_inventory_pack_slots::Start && 
			(slot & 0xFF) < player_inventory_pack_slots::End)
		{
			const uint8 slotFieldOffset = (static_cast<uint8>(slot & 0xFF) - player_inventory_slots::End) * 2;
			itemGuid = player->Get<uint64>(object_fields::PackSlot_1 + slotFieldOffset);
		}
		else if ((static_cast<uint16>(slot) >> 8) == player_inventory_slots::Bag_0 &&
			(slot & 0xFF) >= player_equipment_slots::Start &&
			(slot & 0xFF) < player_equipment_slots::End)
		{
			const uint8 slotFieldOffset = static_cast<uint8>(slot & 0xFF) * 2;
			itemGuid = player->Get<uint64>(object_fields::InvSlotHead + slotFieldOffset);
		}
		else if ((static_cast<uint16>(slot) >> 8) == player_inventory_slots::Bag_0 &&
			(slot & 0xFF) >= player_inventory_slots::Start &&
			(slot & 0xFF) < player_inventory_slots::End)
		{
			const uint8 slotFieldOffset = static_cast<uint8>(slot & 0xFF) * 2;
			itemGuid = player->Get<uint64>(object_fields::InvSlotHead + slotFieldOffset);
		}
		else if (static_cast<uint16>(slot) >> 8 >= player_inventory_slots::Start &&
			static_cast<uint16>(slot) >> 8 < player_inventory_slots::End)
		{
			// Bag slots, get bag item first
			const uint8 slotFieldOffset = (static_cast<uint16>(slot) >> 8) * 2;
			itemGuid = player->Get<uint64>(object_fields::InvSlotHead + slotFieldOffset);

			if (itemGuid == 0)
			{
				return nullptr;
			}

			// The actual bag item
			std::shared_ptr<GameBagC> bag = ObjectMgr::Get<GameBagC>(itemGuid);
			if (!bag)
			{
				return nullptr;
			}

			// Out of bag bounds?
			if ((slot & 0xFF) >= bag->Get<uint32>(object_fields::NumSlots))
			{
				return nullptr;
			}

			itemGuid = bag->Get<uint64>(object_fields::Slot_1 + (slot & 0xFF) * 2);
			if (itemGuid == 0)
			{
				return nullptr;
			}

			return ObjectMgr::Get<GameItemC>(itemGuid);
		}

		if (itemGuid == 0)
		{
			return nullptr;
		}

		// Get item at the specified slot
		return ObjectMgr::Get<GameItemC>(itemGuid);
	}

	void Cursor::UpdateCursorIcon()
	{
		String iconPath = GetDefaultIcon();
		
		if (m_type == CursorItemType::Spell && m_project)
		{
			// Resolve spell icon
			const auto* spell = m_project->spells.getById(m_itemSlot);
			if (spell && !spell->icon().empty())
			{
				iconPath = spell->icon();
			}
		}
		else if (m_type == CursorItemType::Item && m_project)
		{
			// Resolve item icon
			const auto item = ResolveItemFromSlot(m_itemSlot);
			if (item)
			{
				ItemHandle itemHandle(*item, m_project->spells);
				const char* itemIcon = itemHandle.GetIcon();
				if (itemIcon)
				{
					iconPath = itemIcon;
				}
			}
		}

		// Set the cursor icon
		TexturePtr texture = TextureManager::Get().CreateOrRetrieve(iconPath);
		FrameManager::Get().SetCursorIcon(texture, Size(96.0f, 96.0f));
	}

	const char* Cursor::GetDefaultIcon() const
	{
		return "Interface/Icons/Spells/S_Attack.htex";
	}
}
