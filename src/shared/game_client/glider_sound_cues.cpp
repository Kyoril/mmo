// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glider_sound_cues.h"

namespace mmo
{
	GliderSoundCues::Cue GliderSoundCues::Update(const bool gliding, const glider_event::Type event)
	{
		Cue cue;
		if (event == glider_event::Opened)
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
