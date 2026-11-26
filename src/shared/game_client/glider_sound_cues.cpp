// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glider_sound_cues.h"

namespace mmo
{
	GliderSoundCues::Cue GliderSoundCues::Update(const bool gliding, const glider_event::Type event)
	{
		Cue cue;
		// A glider visual rebuilt mid-glide (display change) reports Opened again; the loop
		// already running tells that apart from a real opening.
		if (event == glider_event::Opened && !m_loopRunning)
		{
			cue.playOpen = true;
		}

		if (gliding && !m_loopRunning)
		{
			cue.startLoop = true;
			m_loopRunning = true;
		}
		else if (!gliding && m_loopRunning)
		{
			cue.playClose = true;
			cue.stopLoop = true;
			m_loopRunning = false;
		}

		return cue;
	}
}
