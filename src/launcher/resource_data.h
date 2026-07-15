// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cstddef>

namespace mmo
{
	/// A read-only view of a resource embedded in the executable.
	///
	/// The pointed-to memory is valid for the entire process lifetime and must not be
	/// freed by the caller. That contract is what lets FT_New_Memory_Face and
	/// stbi_load_from_memory read straight out of the binary image with no copy, and
	/// every platform implementation is required to honour it.
	struct ResourceBlob
	{
		const uint8* data = nullptr;
		size_t size = 0;

		bool IsValid() const { return data != nullptr && size > 0; }
	};

	/// Looks up an embedded resource by id (see resource.h).
	///
	/// Returns an invalid blob and logs on failure; never throws. Implemented per
	/// platform: Win32 reads RCDATA out of the module image, macOS will read from the
	/// application bundle using the name table in resource_names.h.
	ResourceBlob LoadResourceBlob(uint32 id);
}
