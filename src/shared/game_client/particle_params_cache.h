// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	struct ParticleSystemParameters;

	/// @brief Loads the parameters of a particle system file once and keeps them for the rest of
	///	the process, so effects spawned often (gliders, updraft columns) never re-open files.
	///	A missing or unreadable file is remembered as missing. Main thread only.
	/// @return The parameters, or nullptr if the file does not exist or cannot be read.
	const ParticleSystemParameters* LoadCachedParticleSystem(const String& file);
}
