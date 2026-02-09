// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "clipboard.h"

#include "base/macros.h"

#if PLATFORM_WINDOWS

#include <Windows.h>
#include <cstring>


namespace mmo
{
	namespace
	{
		class WindowsClipboard final : public IClipboard
		{
		public:
			bool SetText(std::string_view text) override
			{
				const int wideLength = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
				if (wideLength <= 0)
				{
					return false;
				}

				std::wstring wideText;
				wideText.resize(static_cast<std::size_t>(wideLength));
				const int converted = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wideText.data(), wideLength);
				if (converted <= 0)
				{
					return false;
				}

				if (!::OpenClipboard(nullptr))
				{
					return false;
				}

				::EmptyClipboard();

				const std::size_t bytes = (wideText.size() + 1) * sizeof(wchar_t);
				HGLOBAL memoryHandle = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
				if (!memoryHandle)
				{
					::CloseClipboard();
					return false;
				}

				void* memoryPtr = ::GlobalLock(memoryHandle);
				if (!memoryPtr)
				{
					::GlobalFree(memoryHandle);
					::CloseClipboard();
					return false;
				}

				std::memcpy(memoryPtr, wideText.c_str(), bytes);
				::GlobalUnlock(memoryHandle);

				if (!::SetClipboardData(CF_UNICODETEXT, memoryHandle))
				{
					::GlobalFree(memoryHandle);
					::CloseClipboard();
					return false;
				}

				::CloseClipboard();
				return true;
			}

			std::optional<std::string> GetText() override
			{
				if (!::OpenClipboard(nullptr))
				{
					return std::nullopt;
				}

				HANDLE clipboardData = ::GetClipboardData(CF_UNICODETEXT);
				if (!clipboardData)
				{
					::CloseClipboard();
					return std::nullopt;
				}

				const wchar_t* wideText = static_cast<const wchar_t*>(::GlobalLock(clipboardData));
				if (!wideText)
				{
					::CloseClipboard();
					return std::nullopt;
				}

				const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideText, -1, nullptr, 0, nullptr, nullptr);
				if (utf8Length <= 0)
				{
					::GlobalUnlock(clipboardData);
					::CloseClipboard();
					return std::nullopt;
				}

				std::string utf8Text;
				utf8Text.resize(static_cast<std::size_t>(utf8Length));
				const int converted = WideCharToMultiByte(
					CP_UTF8,
					0,
					wideText,
					-1,
					utf8Text.data(),
					utf8Length,
					nullptr,
					nullptr);

				::GlobalUnlock(clipboardData);
				::CloseClipboard();

				if (converted <= 0)
				{
					return std::nullopt;
				}

				if (!utf8Text.empty())
				{
					utf8Text.pop_back();
				}

				return utf8Text;
			}
		};
	}

	std::unique_ptr<IClipboard> CreateClipboardImplementation()
	{
		return std::make_unique<WindowsClipboard>();
	}
}

#endif
