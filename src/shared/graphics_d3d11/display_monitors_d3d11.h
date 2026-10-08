// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/graphics_device.h"

#include <Windows.h>

#include <vector>

namespace mmo
{
	/// A monitor of the desktop as seen by Win32.
	struct Win32Monitor final
	{
		DisplayMonitor info;
		/// Full monitor rectangle on the virtual desktop.
		RECT bounds{};
		/// Monitor rectangle without the task bar and other docked bars.
		RECT workArea{};
		/// GDI device name (\\.\DISPLAYn), used to query the monitor's display modes.
		std::wstring deviceName;
	};

	/// @brief Lists the desktop's monitors in GraphicsDevice::GetDisplayMonitors order: the primary
	///        monitor first, the others from left to right, then top to bottom.
	/// @remark Enumerated through Win32 rather than DXGI: on hybrid-graphics laptops the monitors hang
	///         off the integrated GPU, so the adapter we render with often reports no outputs at all.
	std::vector<Win32Monitor> EnumerateWin32Monitors();

	/// @brief Returns the monitor with the given index, falling back to the primary one when the index
	///        is out of range (e.g. the monitor was unplugged since the setting was saved).
	/// @return False if no monitor could be enumerated at all.
	bool FindWin32Monitor(uint32 index, Win32Monitor& out_monitor);

	/// @brief Lists the resolutions of a monitor's display modes, sorted ascending without duplicates.
	std::vector<std::pair<uint16, uint16>> GetWin32MonitorResolutions(const Win32Monitor& monitor);
}
