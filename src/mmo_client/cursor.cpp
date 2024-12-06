
#include "cursor.h"

#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

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
	}

	void Cursor::SetItem(uint32 slot)
	{
		m_type = CursorItemType::Item;
		m_itemSlot = slot;
	}

	uint32 Cursor::GetCursorItem() const
	{
		return m_itemSlot;
	}
}
