// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "mesh.h"

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	/// @brief Parses a textual list of mesh sound emitters, as `mmo_edit --set-mesh-sounds` takes it.
	/// @details Entries are separated by `;`, each `<soundEntryId>:<x>,<y>,<z>[:<volume>[:<maxDistance>]]`
	///          with the offset in mesh-local metres. Volume defaults to 1, max distance to 0 (use the
	///          entry's). An empty or whitespace-only spec is valid and means "no emitters".
	/// @param spec The text to parse.
	/// @param out_emitters Receives the emitters on success.
	/// @param out_error Receives a description of the first problem on failure.
	/// @return True when the whole spec parsed.
	bool ParseMeshSoundEmitterSpec(const String& spec, std::vector<MeshSoundEmitter>& out_emitters, String& out_error);

	/// @brief Formats emitters in the syntax ParseMeshSoundEmitterSpec reads.
	[[nodiscard]] String FormatMeshSoundEmitterSpec(const std::vector<MeshSoundEmitter>& emitters);
}
