// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/object_type_id.h"

namespace io
{
	class Reader;
}

namespace mmo
{
	/// Reads past the rest of one value update block (the part after its type, creation flag and
	/// guid) without applying it anywhere. Used for an update to an object the client does not
	/// know: a stale update from the server is no reason to drop the session, but the blocks after
	/// it in the same packet can only be read once this one has been consumed.
	/// The layout mirrors what GameObjectC::Deserialize and its overrides read for a value update.
	/// @param reader The reader positioned after the block's guid.
	/// @param typeId The object type the block was written for.
	/// @returns false if the block could not be read, or the type has no known layout.
	bool SkipObjectValueUpdate(io::Reader& reader, ObjectTypeId typeId);
}
