// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"

namespace mmo
{
	class IAudio;

	/// @brief Owns the sound-related console variables and applies their values to the
	/// audio system, both once at startup and live whenever one of them is changed.
	///
	/// Each sound category has an enable toggle and a volume cvar, plus a master pair
	/// which affects all categories at once. The cvars are persisted through the regular
	/// config serialization (saveconfig).
	class AudioSettings final : public NonCopyable
	{
	public:
		/// @brief Registers all sound cvars, connects their change signals and applies
		/// the current values to the audio system.
		explicit AudioSettings(IAudio& audio);

	private:
		/// @brief Applies all cvar values to the audio system at once.
		void ApplyAll() const;

	private:
		IAudio& m_audio;
		scoped_connection_container m_connections;
	};
}
