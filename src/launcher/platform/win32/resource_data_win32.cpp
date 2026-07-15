// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "resource_data.h"

#include "log/default_log_levels.h"

#include <Windows.h>

namespace mmo
{
	ResourceBlob LoadResourceBlob(const uint32 id)
	{
		const HMODULE module = GetModuleHandleW(nullptr);

		// RT_RCDATA expands to MAKEINTRESOURCE, which is the ANSI form unless UNICODE
		// is defined project-wide. The W entry points are called explicitly here, so
		// spell out the wide integer-resource form instead. It is a reinterpret cast,
		// so it cannot be constexpr.
		const LPCWSTR rcDataType = MAKEINTRESOURCEW(10);

		const HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), rcDataType);
		if (!info)
		{
			ELOG("Embedded resource " << id << " not found");
			return {};
		}

		const HGLOBAL handle = LoadResource(module, info);
		if (!handle)
		{
			ELOG("Failed to load embedded resource " << id);
			return {};
		}

		// LockResource neither locks nor needs a matching unlock: the returned pointer
		// addresses the mapped module image directly and stays valid until the module
		// is unloaded, which for an executable means process exit.
		//
		// SizeofResource may include alignment padding, but both PNG and TrueType
		// carry their own length, so trailing bytes are harmless.
		return ResourceBlob{
			static_cast<const uint8*>(LockResource(handle)),
			SizeofResource(module, info)
		};
	}
}
