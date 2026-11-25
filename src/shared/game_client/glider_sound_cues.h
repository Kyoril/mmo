// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "glider_fold_state.h"

namespace mmo
{
	/// @brief Sound entries of the glider (SoundEntry ids, data/editor sounds.data).
	namespace glider_sounds
	{
		enum Type
		{
			/// Wooden frame unfolding and linen snapping taut.
			Open = 450,
			/// Wings folding, short wooden clack.
			Close = 451,
			/// Wind and fluttering linen while gliding (looped, 3D, follows the unit).
			WindLoop = 452,
			/// Rising whoosh when the own glider enters an updraft.
			Updraft = 453
		};
	}

	/// Channel priority of the glider's wind loop: above the 1.0 of one-shots, so their channel
	///	stealing never takes the loop's channel while the unit keeps moving and stopping it.
	constexpr float GliderWindLoopPriority = 1.5f;

	/// @brief Decides when a unit's glider plays its open and close sounds and runs its wind
	///	loop. Pure logic; GameUnitC turns the cues into sound entry playback.
	class GliderSoundCues final
	{
	public:
		/// @brief What to play this frame.
		struct Cue
		{
			bool playOpen{false};
			bool playClose{false};
			bool startLoop{false};
			bool stopLoop{false};
		};

	public:
		/// @brief Advances the cues.
		/// @param gliding Whether the unit glides this frame.
		/// @param event The fold edge GliderVisual reported this frame.
		/// @return The open sound and the loop start on Opened (not again for an Opened while the
		///	loop runs, which a glider visual rebuilt mid-glide reports); the close sound and the loop stop
		///	once when the glide ends (when folding starts, not when it finishes); a re-open while
		///	still folding restarts the loop without a second open sound.
		Cue Update(bool gliding, glider_event::Type event);

		/// @brief Whether the wind loop should currently be playing.
		[[nodiscard]] bool IsLoopRunning() const { return m_loopRunning; }

	private:
		bool m_loopRunning{false};
	};
}
