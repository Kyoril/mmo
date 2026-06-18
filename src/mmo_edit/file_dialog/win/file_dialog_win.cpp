// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "file_dialog/file_dialog.h"

#include <windows.h>
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

namespace mmo
{
	namespace
	{
		/// Converts a UTF-8 std::string into a UTF-16 std::wstring.
		std::wstring Utf8ToWide(const String& utf8)
		{
			if (utf8.empty())
			{
				return std::wstring();
			}

			const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
			if (length <= 0)
			{
				return std::wstring();
			}

			std::wstring result(static_cast<size_t>(length), L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), length);
			return result;
		}

		/// Converts a UTF-16 wide string into a UTF-8 std::string.
		String WideToUtf8(const wchar_t* wide)
		{
			if (wide == nullptr || wide[0] == L'\0')
			{
				return String();
			}

			const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
			if (length <= 0)
			{
				return String();
			}

			// length includes the terminating null character.
			String result(static_cast<size_t>(length - 1), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), length, nullptr, nullptr);
			return result;
		}

		/// Builds the double-null terminated filter buffer expected by the common dialogs.
		std::wstring BuildFilterString(const std::vector<FileDialogFilter>& filters)
		{
			std::wstring buffer;
			for (const auto& filter : filters)
			{
				buffer += Utf8ToWide(filter.name);
				buffer.push_back(L'\0');
				buffer += Utf8ToWide(filter.pattern);
				buffer.push_back(L'\0');
			}

			// Always offer an "All Files" entry as the last option.
			buffer += L"All Files";
			buffer.push_back(L'\0');
			buffer += L"*.*";
			buffer.push_back(L'\0');

			// Terminating double null.
			buffer.push_back(L'\0');
			return buffer;
		}
	}

	std::optional<String> FileDialog::ShowSave(const String& title,
		const std::vector<FileDialogFilter>& filters,
		const String& defaultFileName,
		const String& defaultExtension)
	{
		const std::wstring filterString = BuildFilterString(filters);
		const std::wstring titleString = Utf8ToWide(title);
		const std::wstring extensionString = Utf8ToWide(defaultExtension);

		wchar_t fileBuffer[MAX_PATH] = { 0 };
		const std::wstring defaultName = Utf8ToWide(defaultFileName);
		if (!defaultName.empty())
		{
			wcsncpy_s(fileBuffer, defaultName.c_str(), _TRUNCATE);
		}

		OPENFILENAMEW ofn = { 0 };
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = ::GetActiveWindow();
		ofn.lpstrFilter = filterString.c_str();
		ofn.lpstrFile = fileBuffer;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrTitle = titleString.empty() ? nullptr : titleString.c_str();
		ofn.lpstrDefExt = extensionString.empty() ? nullptr : extensionString.c_str();
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

		if (!::GetSaveFileNameW(&ofn))
		{
			return std::nullopt;
		}

		return WideToUtf8(fileBuffer);
	}

	std::optional<String> FileDialog::ShowOpen(const String& title,
		const std::vector<FileDialogFilter>& filters)
	{
		const std::wstring filterString = BuildFilterString(filters);
		const std::wstring titleString = Utf8ToWide(title);

		wchar_t fileBuffer[MAX_PATH] = { 0 };

		OPENFILENAMEW ofn = { 0 };
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = ::GetActiveWindow();
		ofn.lpstrFilter = filterString.c_str();
		ofn.lpstrFile = fileBuffer;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrTitle = titleString.empty() ? nullptr : titleString.c_str();
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

		if (!::GetOpenFileNameW(&ofn))
		{
			return std::nullopt;
		}

		return WideToUtf8(fileBuffer);
	}
}
