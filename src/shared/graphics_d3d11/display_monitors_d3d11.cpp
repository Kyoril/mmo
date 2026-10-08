// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "display_monitors_d3d11.h"

#include <algorithm>
#include <map>
#include <string>

namespace mmo
{
	namespace
	{
		std::string ToUtf8(const wchar_t* text)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 1)
			{
				return {};
			}

			std::string result(static_cast<size_t>(size - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
			return result;
		}

		/// Maps GDI device names (\\.\DISPLAYn) to the model names the monitors report over EDID.
		/// GetMonitorInfo only knows the GDI name, which means nothing to a player.
		std::map<std::wstring, std::string> QueryFriendlyMonitorNames()
		{
			std::map<std::wstring, std::string> names;

			UINT32 pathCount = 0, modeCount = 0;
			if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
			{
				return names;
			}

			std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
			{
				return names;
			}

			for (UINT32 i = 0; i < pathCount; ++i)
			{
				DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
				source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
				source.header.size = sizeof(source);
				source.header.adapterId = paths[i].sourceInfo.adapterId;
				source.header.id = paths[i].sourceInfo.id;

				DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
				target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
				target.header.size = sizeof(target);
				target.header.adapterId = paths[i].targetInfo.adapterId;
				target.header.id = paths[i].targetInfo.id;

				if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
					DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS)
				{
					names[source.viewGdiDeviceName] = ToUtf8(target.monitorFriendlyDeviceName);
				}
			}

			return names;
		}

		BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, const LPARAM userData)
		{
			auto& monitors = *reinterpret_cast<std::vector<Win32Monitor>*>(userData);

			MONITORINFOEXW info{};
			info.cbSize = sizeof(info);
			if (!GetMonitorInfoW(monitor, &info))
			{
				return TRUE;
			}

			Win32Monitor entry;
			entry.bounds = info.rcMonitor;
			entry.workArea = info.rcWork;
			entry.deviceName = info.szDevice;
			entry.info.x = info.rcMonitor.left;
			entry.info.y = info.rcMonitor.top;
			entry.info.width = static_cast<uint16>(info.rcMonitor.right - info.rcMonitor.left);
			entry.info.height = static_cast<uint16>(info.rcMonitor.bottom - info.rcMonitor.top);
			entry.info.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
			monitors.push_back(std::move(entry));
			return TRUE;
		}
	}

	std::vector<Win32Monitor> EnumerateWin32Monitors()
	{
		std::vector<Win32Monitor> monitors;
		EnumDisplayMonitors(nullptr, nullptr, &CollectMonitor, reinterpret_cast<LPARAM>(&monitors));

		std::stable_sort(monitors.begin(), monitors.end(), [](const Win32Monitor& a, const Win32Monitor& b)
		{
			if (a.info.primary != b.info.primary)
			{
				return a.info.primary;
			}

			return a.info.x != b.info.x ? a.info.x < b.info.x : a.info.y < b.info.y;
		});

		const auto friendlyNames = QueryFriendlyMonitorNames();
		for (Win32Monitor& monitor : monitors)
		{
			if (const auto it = friendlyNames.find(monitor.deviceName); it != friendlyNames.end())
			{
				monitor.info.name = it->second;
			}
		}

		return monitors;
	}

	bool FindWin32Monitor(const uint32 index, Win32Monitor& out_monitor)
	{
		std::vector<Win32Monitor> monitors = EnumerateWin32Monitors();
		if (monitors.empty())
		{
			return false;
		}

		out_monitor = std::move(monitors[index < monitors.size() ? index : 0]);
		return true;
	}

	std::vector<std::pair<uint16, uint16>> GetWin32MonitorResolutions(const Win32Monitor& monitor)
	{
		std::vector<std::pair<uint16, uint16>> result;

		DEVMODEW mode{};
		mode.dmSize = sizeof(mode);
		for (DWORD i = 0; EnumDisplaySettingsW(monitor.deviceName.c_str(), i, &mode); ++i)
		{
			// Tiny legacy modes make no sense for a game window, and modes above the current desktop
			// resolution would not fit on the monitor.
			if (mode.dmBitsPerPel < 32 || mode.dmPelsWidth < 1024 || mode.dmPelsHeight < 720 ||
				mode.dmPelsWidth > monitor.info.width || mode.dmPelsHeight > monitor.info.height)
			{
				continue;
			}

			result.emplace_back(static_cast<uint16>(mode.dmPelsWidth), static_cast<uint16>(mode.dmPelsHeight));
		}

		// The desktop resolution itself is always available, even if the driver lists no modes.
		result.emplace_back(monitor.info.width, monitor.info.height);

		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}
}
